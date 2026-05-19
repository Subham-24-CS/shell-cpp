#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <limits.h>
#include <filesystem>
#include <sys/wait.h>
#include <fstream>
#include <fcntl.h>

using namespace std;

// Parses the command line string handling single quotes, double quotes, and backslashes contextually.
vector<string> parse_arguments(const string& cmd_line) {
    vector<string> args;
    string current_arg = "";
    bool in_single_quotes = false;
    bool in_double_quotes = false;
    bool has_content = false; // Tracks if we are building an argument

    for (size_t i = 0; i < cmd_line.length(); ++i) {
        char c = cmd_line[i];

        if (in_single_quotes) {
            if (c == '\'') {
                in_single_quotes = false;
                has_content = true; 
            } else {
                current_arg += c;
                has_content = true;
            }
        } else if (in_double_quotes) {
            if (c == '\\') {
                if (i + 1 < cmd_line.length()) {
                    char next_c = cmd_line[i + 1];
                    if (next_c == '"' || next_c == '\\') {
                        current_arg += next_c;
                        i++; 
                    } else {
                        current_arg += c;
                    }
                    has_content = true;
                } else {
                    current_arg += c;
                    has_content = true;
                }
            } else if (c == '"') {
                in_double_quotes = false;
                has_content = true;
            } else {
                current_arg += c;
                has_content = true;
            }
        } else {
            if (c == '\\') {
                if (i + 1 < cmd_line.length()) {
                    current_arg += cmd_line[i + 1];
                    has_content = true;
                    i++; 
                }
            } else if (c == '\'') {
                in_single_quotes = true;
                has_content = true;
            } else if (c == '"') {
                in_double_quotes = true;
                has_content = true;
            } else if (c == ' ' || c == '\t') {
                if (has_content) {
                    args.push_back(current_arg);
                    current_arg = "";
                    has_content = false;
                }
            } else {
                current_arg += c;
                has_content = true;
            }
        }
    }

    if (has_content) {
        args.push_back(current_arg);
    }

    return args;
}

int main() {
    cout << unitbuf;
    cerr << unitbuf;

    while (true) {
        cout << "$ ";
        string command_line;
        if (!getline(cin, command_line)) {
            break; 
        }

        if (command_line.empty()) {
            continue;
        }

        vector<string> args = parse_arguments(command_line);
        if (args.empty()) {
            continue;
        }

        // Check for redirection operators ('>' or '1>')
        bool redirect_output = false;
        string redirect_file = "";
        vector<string> clean_args;

        for (size_t i = 0; i < args.size(); ++i) {
            if ((args[i] == ">" || args[i] == "1>") && (i + 1 < args.size())) {
                redirect_output = true;
                redirect_file = args[i + 1];
                // Skip both the operator and the filename from execution arguments
                i++; 
            } else {
                clean_args.push_back(args[i]);
            }
        }

        // If a trailing delimiter leaves us with no actual command, skip
        if (clean_args.empty()) {
            continue;
        }

        string cmd = clean_args[0];

        // Setup redirection for builtins using C++ streambufs
        streambuf* old_cout = cout.rdbuf();
        ofstream out_file;
        if (redirect_output) {
            out_file.open(redirect_file, ios::out | ios::trunc);
            if (out_file.is_open()) {
                cout.rdbuf(out_file.rdbuf());
            }
        }

        if (cmd == "exit") {
            // Restore stdout before exiting just in case
            cout.rdbuf(old_cout);
            break;
        }
        else if (cmd == "pwd") {
            cout << filesystem::current_path().string() << endl;
        }
        else if (cmd == "cd") {
            string clean_path = (clean_args.size() > 1) ? clean_args[1] : "~";

            if (clean_path == "~") {
                char* home = getenv("HOME");
                if (home) {
                    clean_path = string(home);
                }
            }

            if (chdir(clean_path.c_str()) != 0) {
                cout << "cd: " << clean_path << ": No such file or directory" << endl;
            }
        }
        else if (cmd == "echo") {
            for (size_t i = 1; i < clean_args.size(); ++i) {
                cout << clean_args[i];
                if (i + 1 < clean_args.size()) cout << " ";
            }
            cout << endl;
        }
        else if (cmd == "type") {
            if (clean_args.size() < 2) {
                if (redirect_output) cout.rdbuf(old_cout);
                continue;
            }
            string com = clean_args[1];
            
            if (com == "exit" || com == "type" || com == "echo" || com == "pwd" || com == "cd") {
                cout << com << " is a shell builtin" << endl;
            }
            else {
                bool found = false;
                char* path_env = getenv("PATH");
                if (path_env) {
                    stringstream ss_path(path_env);
                    string path;
                    while (getline(ss_path, path, ':')) {
                        string full_path = path + '/' + com;
                        if (access(full_path.c_str(), X_OK) == 0) {
                            cout << com << " is " << full_path << endl;
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    cout << com << ": not found" << endl;
                }
            }
        }
        else {
            // Check for external commands in PATH
            bool found = false;
            string executable_path;
            char* path_env = getenv("PATH");

            if (path_env) {
                stringstream ss_path(path_env);
                string path_dir;
                while (getline(ss_path, path_dir, ':')) {
                    string full_path = path_dir + '/' + cmd;
                    if (access(full_path.c_str(), X_OK) == 0) {
                        executable_path = full_path;
                        found = true;
                        break;
                    }
                }
            }

            if (found) {
                vector<char*> c_args;
                for (auto& s : clean_args) {
                    c_args.push_back(&s[0]);
                }
                c_args.push_back(nullptr);

                pid_t pid = fork();
                if (pid == 0) {
                    // Child process output redirection via low-level system call
                    if (redirect_output) {
                        int fd = open(redirect_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd != -1) {
                            dup2(fd, STDOUT_FILENO);
                            close(fd);
                        }
                    }
                    execvp(c_args[0], c_args.data());
                    exit(1); 
                } else {
                    // Parent process
                    waitpid(pid, nullptr, 0);
                }
            } else {
                cout << cmd << ": command not found" << endl;
            }
        }

        // Restore standard C++ output routing after executing any statement
        if (redirect_output) {
            cout.rdbuf(old_cout);
            out_file.close();
        }
    }
    return 0;
}