#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <limits.h>
#include <filesystem>
#include <sys/wait.h>

using namespace std;

// Parses the command line string handling single quotes, double quotes, and unquoted backslashes.
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
            if (c == '"') {
                in_double_quotes = false;
                has_content = true;
            } else {
                current_arg += c;
                has_content = true;
            }
        } else {
            if (c == '\\') {
                // Unquoted backslash: escape the very next character if it exists
                if (i + 1 < cmd_line.length()) {
                    current_arg += cmd_line[i + 1];
                    has_content = true;
                    i++; // Skip the next character since we processed it literally
                }
            } else if (c == '\'') {
                in_single_quotes = true;
                has_content = true;
            } else if (c == '"') {
                in_double_quotes = true;
                has_content = true;
            } else if (c == ' ' || c == '\t') {
                // Unquoted whitespace acts as an argument delimiter
                if (has_content) {
                    args.push_back(current_arg);
                    current_arg = "";
                    has_content = false;
                }
            } else {
                // Normal unquoted character
                current_arg += c;
                has_content = true;
            }
        }
    }

    // Don't forget the last argument if the line didn't end in an unquoted space
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

        // Tokenize the string using our updated quote-and-backslash parser
        vector<string> args = parse_arguments(command_line);
        if (args.empty()) {
            continue;
        }

        string cmd = args[0];

        if (cmd == "exit") {
            break;
        }
        else if (cmd == "pwd") {
            cout << filesystem::current_path().string() << endl;
        }
        else if (cmd == "cd") {
            string clean_path = (args.size() > 1) ? args[1] : "~";

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
            // Print all arguments separated by a single space
            for (size_t i = 1; i < args.size(); ++i) {
                cout << args[i];
                if (i + 1 < args.size()) cout << " ";
            }
            cout << endl;
        }
        else if (cmd == "type") {
            if (args.size() < 2) {
                continue;
            }
            string com = args[1];
            
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
                // Build argument vector for execvp safely
                vector<char*> c_args;
                for (auto& s : args) {
                    c_args.push_back(&s[0]);
                }
                c_args.push_back(nullptr);

                pid_t pid = fork();
                if (pid == 0) {
                    // Child process
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
    }
    return 0;
}