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

        // Flags and file targets for stdout and stderr redirections
        bool redirect_output = false;
        bool append_output = false; // Flag specifically for >> and 1>>
        string redirect_file = "";
        bool redirect_error = false;
        string error_file = "";
        vector<string> clean_args;

        for (size_t i = 0; i < args.size(); ++i) {
            if ((args[i] == ">" || args[i] == "1>") && (i + 1 < args.size())) {
                redirect_output = true;
                append_output = false;
                redirect_file = args[i + 1];
                i++; 
            } else if ((args[i] == ">>" || args[i] == "1>>") && (i + 1 < args.size())) {
                redirect_output = true;
                append_output = true;
                redirect_file = args[i + 1];
                i++; 
            } else if (args[i] == "2>" && (i + 1 < args.size())) {
                redirect_error = true;
                error_file = args[i + 1];
                i++; 
            } else {
                clean_args.push_back(args[i]);
            }
        }

        if (clean_args.empty()) {
            continue;
        }

        string cmd = clean_args[0];

        // Setup stream redirection buffers for builtins
        streambuf* old_cout = cout.rdbuf();
        ofstream out_file;
        if (redirect_output) {
            if (append_output) {
                out_file.open(redirect_file, ios::out | ios::app);
            } else {
                out_file.open(redirect_file, ios::out | ios::trunc);
            }
            if (out_file.is_open()) {
                cout.rdbuf(out_file.rdbuf());
            }
        }

        streambuf* old_cerr = cerr.rdbuf();
        ofstream err_file;
        if (redirect_error) {
            err_file.open(error_file, ios::out | ios::trunc);
            if (err_file.is_open()) {
                cerr.rdbuf(err_file.rdbuf());
            }
        }

        if (cmd == "exit") {
            cout.rdbuf(old_cout);
            cerr.rdbuf(old_cerr);
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
                if (redirect_error) cerr.rdbuf(old_cerr);
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
                    // Child standard output redirection
                    if (redirect_output) {
                        int flags = O_WRONLY | O_CREAT;
                        if (append_output) {
                            flags |= O_APPEND;
                        } else {
                            flags |= O_TRUNC;
                        }
                        int fd_out = open(redirect_file.c_str(), flags, 0644);
                        if (fd_out != -1) {
                            dup2(fd_out, STDOUT_FILENO);
                            close(fd_out);
                        }
                    }
                    // Child standard error redirection
                    if (redirect_error) {
                        int fd_err = open(error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd_err != -1) {
                            dup2(fd_err, STDERR_FILENO);
                            close(fd_err);
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

        // Restore stream targets for the next loop execution
        if (redirect_output) {
            cout.rdbuf(old_cout);
            out_file.close();
        }
        if (redirect_error) {
            cerr.rdbuf(old_cerr);
            err_file.close();
        }
    }
    return 0;
}