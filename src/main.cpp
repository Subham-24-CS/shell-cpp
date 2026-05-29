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
#include <set>
#include <map>
#include <algorithm>
#include <cstring>

// Include Readline headers
#include <readline/readline.h>
#include <readline/history.h>

using namespace std;

// List of builtins we want to support autocomplete for
const vector<string> builtins = {"echo", "exit", "complete"};

// Global registry for programmable completions: maps a command name to its completer script path
map<string, string> programmable_completions;

// Custom completion generator function called repeatedly by readline for COMMANDS.
char* command_generator(const char* text, int state) {
    static vector<string> current_matches;
    static size_t match_index = 0;
    
    if (!state) {
        current_matches.clear();
        match_index = 0;
        size_t len = strlen(text);
        set<string> unique_matches;

        // 1. Check Builtins
        for (const string& cmd : builtins) {
            if (cmd.compare(0, len, text) == 0) {
                unique_matches.insert(cmd);
            }
        }

        // 2. Scan PATH for External Executables
        char* path_env = getenv("PATH");
        if (path_env) {
            stringstream ss_path(path_env);
            string dir_path;
            
            while (getline(ss_path, dir_path, ':')) {
                if (dir_path.empty()) continue;
                
                try {
                    if (filesystem::exists(dir_path) && filesystem::is_directory(dir_path)) {
                        for (const auto& entry : filesystem::directory_iterator(dir_path)) {
                            string filename = entry.path().filename().string();
                            
                            if (filename.compare(0, len, text) == 0) {
                                string full_path = entry.path().string();
                                if (filesystem::is_regular_file(entry.status()) && access(full_path.c_str(), X_OK) == 0) {
                                    unique_matches.insert(filename);
                                }
                            }
                        }
                    }
                } catch (...) {}
            }
        }

        for (const string& match_str : unique_matches) {
            current_matches.push_back(match_str);
        }
    }

    if (match_index < current_matches.size()) {
        const string& match_str = current_matches[match_index++];
        char* match = (char*)malloc(match_str.length() + 1);
        strcpy(match, match_str.c_str());
        return match;
    }

    return nullptr;
}

// Custom completion generator function called repeatedly by readline for FILENAMES and DIRECTORIES.
char* filename_generator(const char* text, int state) {
    static vector<string> file_matches;
    static size_t file_index = 0;

    if (!state) {
        file_matches.clear();
        file_index = 0;

        string text_str(text);
        string dir_to_search = "."; 
        string prefix = text_str;   
        string dir_prefix = "";     

        size_t last_slash = text_str.find_last_of('/');
        if (last_slash != string::npos) {
            dir_to_search = text_str.substr(0, last_slash + 1);
            prefix = text_str.substr(last_slash + 1);
            dir_prefix = dir_to_search;
        }

        size_t len = prefix.length();

        try {
            if (filesystem::exists(dir_to_search) && filesystem::is_directory(dir_to_search)) {
                for (const auto& entry : filesystem::directory_iterator(dir_to_search)) {
                    string filename = entry.path().filename().string();
                    
                    if (filename.compare(0, len, prefix) == 0) {
                        bool is_dir = filesystem::is_directory(entry.status());
                        bool is_reg = filesystem::is_regular_file(entry.status());
                        
                        if (is_reg || is_dir) {
                            file_matches.push_back(dir_prefix + filename);
                        }
                    }
                }
            }
        } catch (...) {}
    }

    if (file_index < file_matches.size()) {
        const string& match_str = file_matches[file_index++];
        char* match = (char*)malloc(match_str.length() + 1);
        strcpy(match, match_str.c_str());
        return match;
    }

    return nullptr;
}

// Custom completion generator function called by readline when a programmable completion is registered
char* programmable_generator(const char* text, int state) {
    static string completion_candidate;
    static bool dynamic_match_found = false;

    if (!state) {
        completion_candidate = "";
        dynamic_match_found = false;

        string current_line(rl_line_buffer);
        stringstream ss(current_line);
        string base_cmd;
        ss >> base_cmd;

        if (programmable_completions.count(base_cmd)) {
            string script_path = programmable_completions[base_cmd];

            // Determine context arguments: argv[2] (current) and argv[3] (previous)
            string current_word = string(text);
            string prev_word = "";

            // Parse the line up to the current completion token position to isolate the previous word
            string partial_line = current_line.substr(0, rl_point);
            // Trim tracking trailing whitespaces if any to locate the preceding token safely
            size_t end_pos = partial_line.find_last_not_of(" \t");
            if (end_pos != string::npos) {
                partial_line = partial_line.substr(0, end_pos + 1);
                // If the current word isn't empty, peel it off to find the word before it
                if (!current_word.empty()) {
                    size_t word_start = partial_line.find_last_of(" \t");
                    if (word_start != string::npos) {
                        partial_line = partial_line.substr(0, word_start);
                    } else {
                        partial_line = ""; // No word before this one
                    }
                }
                // Extract the final leftover token as our argv[3] previous word
                size_t prev_start = partial_line.find_last_not_of(" \t");
                if (prev_start != string::npos) {
                    partial_line = partial_line.substr(0, prev_start + 1);
                    size_t last_space = partial_line.find_last_of(" \t");
                    if (last_space != string::npos) {
                        prev_word = partial_line.substr(last_space + 1);
                    } else {
                        prev_word = partial_line;
                    }
                }
            }

            // Ensure we do not use the base command itself as the preceding argument context
            if (prev_word == base_cmd) {
                prev_word = "";
            }

            int pipe_fds[2];
            if (pipe(pipe_fds) == 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    // Child process setup redirection
                    close(pipe_fds[0]);
                    dup2(pipe_fds[1], STDOUT_FILENO);
                    close(pipe_fds[1]);

                    char* c_script = const_cast<char*>(script_path.c_str());
                    char* c_arg1 = const_cast<char*>(base_cmd.c_str());
                    char* c_arg2 = const_cast<char*>(current_word.c_str());
                    char* c_arg3 = const_cast<char*>(prev_word.c_str());
                    
                    char* c_args[] = {c_script, c_arg1, c_arg2, c_arg3, nullptr};
                    execv(c_script, c_args);
                    exit(1); 
                } else if (pid > 0) {
                    // Parent process reads output securely
                    close(pipe_fds[1]);
                    char buffer[1024];
                    string output = "";
                    ssize_t bytes_read;
                    while ((bytes_read = read(pipe_fds[0], buffer, sizeof(buffer) - 1)) > 0) {
                        buffer[bytes_read] = '\0';
                        output += buffer;
                    }
                    close(pipe_fds[0]);
                    waitpid(pid, nullptr, 0);

                    // Normalize line endings and get the candidate line
                    if (!output.empty()) {
                        size_t pos = output.find_first_of("\r\n");
                        if (pos != string::npos) {
                            completion_candidate = output.substr(0, pos);
                        } else {
                            completion_candidate = output;
                        }
                        dynamic_match_found = !completion_candidate.empty();
                    }
                }
            }
        }
    }

    if (dynamic_match_found && state == 0) {
        char* match = (char*)malloc(completion_candidate.length() + 1);
        strcpy(match, completion_candidate.c_str());
        return match;
    }

    return nullptr;
}

// Custom display hook used to print out matches sequentially on a new line when multiple are found.
void display_completion_matches(char** matches, int num_matches, int max_length) {
    vector<string> items;
    for (int i = 1; i <= num_matches; ++i) {
        items.push_back(string(matches[i]));
    }
    
    sort(items.begin(), items.end());

    cout << endl;
    for (size_t i = 0; i < items.size(); ++i) {
        string display_name = items[i];
        
        if (filesystem::exists(display_name) && filesystem::is_directory(display_name)) {
            if (display_name.back() != '/') {
                display_name += "/";
            }
        }
        
        cout << display_name;
        if (i + 1 < items.size()) {
            cout << "  "; 
        }
    }
    cout << endl;

    rl_forced_update_display();
}

// Custom completion bridge function hooked into readline's completion engine
char** shell_completion(const char* text, int start, int end) {
    rl_attempted_completion_over = 1;

    rl_completion_append_character = ' '; 
    rl_completion_suppress_append = 0;

    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    } else {
        // Step 1: Detect the active initial command typed on the line buffer
        string current_line(rl_line_buffer);
        stringstream ss(current_line);
        string base_cmd;
        ss >> base_cmd;

        // Step 2: Check if a programmable completion is registered for this command
        if (programmable_completions.count(base_cmd)) {
            return rl_completion_matches(text, programmable_generator);
        }

        // Fallback context: handle file/directory searches
        char** matches = rl_completion_matches(text, filename_generator);
        if (matches && matches[0] && !matches[1]) {
            string match_str(matches[0]);
            if (filesystem::exists(match_str) && filesystem::is_directory(match_str)) {
                rl_completion_append_character = '/';
            }
        }
        return matches;
    }
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

    // Register our custom tab completion hooks
    rl_attempted_completion_function = shell_completion;
    rl_completion_display_matches_hook = display_completion_matches;

    while (true) {
        char* input_raw = readline("$ ");
        
        if (!input_raw) {
            break; 
        }

        string command_line(input_raw);
        free(input_raw); 

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
        else if (cmd == "complete") {
            if (clean_args.size() >= 3 && clean_args[1] == "-p") {
                string target_cmd = clean_args[2];
                auto it = programmable_completions.find(target_cmd);
                if (it != programmable_completions.end()) {
                    cout << "complete -C '" << it->second << "' " << target_cmd << endl;
                } else {
                    cout << "complete: " << target_cmd << ": no completion specification" << endl;
                }
            } 
            else if (clean_args.size() >= 4 && clean_args[1] == "-C") {
                string script_path = clean_args[2];
                string target_cmd = clean_args[3];
                programmable_completions[target_cmd] = script_path;
            }
        }
        else if (cmd == "type") {
            if (clean_args.size() < 2) {
                if (redirect_output) cout.rdbuf(old_cout);
                if (redirect_error) cerr.rdbuf(old_cerr);
                continue;
            }
            string com = clean_args[1];
            
            if (com == "exit" || com == "type" || com == "echo" || com == "pwd" || com == "cd" || com == "complete") {
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