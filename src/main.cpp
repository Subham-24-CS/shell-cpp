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

// Include Readline headers
#include <readline/readline.h>
#include <readline/history.h>

using namespace std;

// List of builtins we want to support autocomplete for in this stage
const vector<string> builtins = {"echo", "exit"};

// Custom completion generator function called repeatedly by readline.
// 'text' is the partial word typed so far.
// 'state' is 0 on the first call, and non-zero on subsequent calls.
char* command_generator(const char* text, int state) {
    static size_t list_index, len;
    
    // First time initialized for this word completion match group
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    // Return the next match in our builtin command array
    while (list_index < builtins.size()) {
        const string& cmd = builtins[list_index];
        list_index++;

        // Check if the prefix matches what the user typed
        if (cmd.compare(0, len, text) == 0) {
            // Readline expects a dynamically allocated C-string copy
            char* match = (char*)malloc(cmd.length() + 1);
            strcpy(match, cmd.c_str());
            return match;
        }
    }

    // No more matches left
    return nullptr;
}

// Custom completion bridge function hooked into readline's completion engine
char** shell_completion(const char* text, int start, int end) {
    // Disable readline's default behavior of falling back to filename completion 
    // when our custom builtin generator yields no results.
    rl_attempted_completion_over = 1;
    // We only want to autocomplete the first token (the command itself)
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }
    
    return nullptr;
}

// Parses the command line string handling single quotes, double quotes, and backslashes contextually.
vector<string> parse_arguments(const string& cmd_line) {
    vector<string> args;
    string current_arg = "";
    bool in_single_quotes = false;
    bool in_double_quotes = false;
    bool has_content = false; 

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

    // Register our custom tab completion callback function
    rl_attempted_completion_function = shell_completion;

    while (true) {
        // Use readline instead of raw cout/getline to accept input and track tabs
        char* input_raw = readline("$ ");
        
        // Handle EOF condition (like hitting Ctrl+D)
        if (!input_raw) {
            break; 
        }

        string command_line(input_raw);
        free(input_raw); // Free memory allocated by readline

        if (command_line.empty()) {
            continue;
        }

        vector<string> args = parse_arguments(command_line);
        if (args.empty()) {
            continue;
        }

        // Flags and file targets for stdout and stderr redirections
        bool redirect_output = false;
        bool append_output = false; 
        string redirect_file = "";
        bool redirect_error = false;
        bool append_error = false; 
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
                append_error = false;
                error_file = args[i + 1];
                i++; 
            } else if (args[i] == "2>>" && (i + 1 < args.size())) {
                redirect_error = true;
                append_error = true;
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
            if (append_error) {
                err_file.open(error_file, ios::out | ios::app);
            } else {
                err_file.open(error_file, ios::out | ios::trunc);
            }
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
                        int flags = O_WRONLY | O_CREAT;
                        if (append_error) {
                            flags |= O_APPEND;
                        } else {
                            flags |= O_TRUNC;
                        }
                        int fd_err = open(error_file.c_str(), flags, 0644);
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
