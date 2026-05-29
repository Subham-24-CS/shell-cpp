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
#include <iomanip>

// Include Readline headers
#include <readline/readline.h>
#include <readline/history.h>

using namespace std;

// Structure to track individual background job metrics
struct BackgroundJob {
    int job_id;
    pid_t pid;
    string command;
    string status;
};

// List of builtins we want to support autocomplete and classification for
const vector<string> builtins = {"echo", "exit", "complete", "jobs", "pwd", "cd", "type", "history"};

// Global registry for programmable completions: maps a command name to its completer script path
map<string, string> programmable_completions;

// Global tracking infrastructure for active background jobs
vector<BackgroundJob> background_jobs;

// Non-blocking loop iteration worker to check for exited jobs
void reap_background_jobs(bool print_inline) {
    size_t total_jobs = background_jobs.size();
    
    for (size_t i = 0; i < total_jobs; ++i) {
        if (background_jobs[i].status == "Running") {
            int status;
            pid_t res = waitpid(background_jobs[i].pid, &status, WNOHANG);
            if (res > 0 && WIFEXITED(status)) {
                background_jobs[i].status = "Done";
                
                // Drop trailing ampersand token notation if present on final report lines
                if (background_jobs[i].command.size() >= 2 && 
                    background_jobs[i].command.substr(background_jobs[i].command.size() - 2) == " &") {
                    background_jobs[i].command = background_jobs[i].command.substr(0, background_jobs[i].command.size() - 2);
                }

                // Only print immediately if we are automatic reaping BEFORE a prompt.
                if (print_inline) {
                    char marker = ' ';
                    if (i == total_jobs - 1) marker = '+';
                    else if (i == total_jobs - 2) marker = '-';

                    cout << "[" << background_jobs[i].job_id << "]" << marker << "  " 
                         << left << setw(24) << background_jobs[i].status 
                         << background_jobs[i].command << endl;
                }
            }
        }
    }

    // If we printed them inline before a prompt, erase them now.
    if (print_inline) {
        background_jobs.erase(
            remove_if(background_jobs.begin(), background_jobs.end(), 
                      [](const BackgroundJob& j) { return j.status == "Done"; }), 
            background_jobs.end()
        );
    }
}

// Helper function to handle executing builtins anywhere (main shell or inside pipe forks)
bool execute_builtin(const string& cmd, const vector<string>& clean_args, bool &should_exit) {
    should_exit = false;
    if (cmd == "exit") {
        should_exit = true;
        return true;
    }
    else if (cmd == "pwd") {
        cout << filesystem::current_path().string() << endl;
        return true;
    }
    else if (cmd == "cd") {
        string clean_path = (clean_args.size() > 1) ? clean_args[1] : "~";
        if (clean_path == "~") {
            char* home = getenv("HOME");
            if (home) clean_path = string(home);
        }
        if (chdir(clean_path.c_str()) != 0) {
            cout << "cd: " << clean_path << ": No such file or directory" << endl;
        }
        return true;
    }
    else if (cmd == "echo") {
        for (size_t i = 1; i < clean_args.size(); ++i) {
            cout << clean_args[i];
            if (i + 1 < clean_args.size()) cout << " ";
        }
        cout << endl;
        return true;
    }
    else if (cmd == "jobs") {
        reap_background_jobs(false);
        size_t total_jobs = background_jobs.size();
        for (size_t i = 0; i < total_jobs; ++i) {
            char marker = ' ';
            if (i == total_jobs - 1) marker = '+';
            else if (i == total_jobs - 2) marker = '-';

            cout << "[" << background_jobs[i].job_id << "]" << marker << "  " 
                 << left << setw(24) << background_jobs[i].status 
                 << background_jobs[i].command << endl;
        }
        background_jobs.erase(
            remove_if(background_jobs.begin(), background_jobs.end(), 
                      [](const BackgroundJob& j) { return j.status == "Done"; }), 
            background_jobs.end()
        );
        return true;
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
        else if (clean_args.size() >= 3 && clean_args[1] == "-r") {
            string target_cmd = clean_args[2];
            programmable_completions.erase(target_cmd);
        }
        else if (clean_args.size() >= 4 && clean_args[1] == "-C") {
            string script_path = clean_args[2];
            string target_cmd = clean_args[3];
            programmable_completions[target_cmd] = script_path;
        }
        return true;
    }
    else if (cmd == "type") {
        if (clean_args.size() < 2) return true;
        string com = clean_args[1];
        if (find(builtins.begin(), builtins.end(), com) != builtins.end()) {
            cout << com << " is a shell builtin" << endl;
        } else {
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
            if (!found) cout << com << ": not found" << endl;
        }
        return true;
    }
    else if (cmd == "history") {
        int start_idx = 0;
        
        // Handle optional numerical limit parameter to display only the last N entries
        if (clean_args.size() > 1) {
            try {
                int limit = stoi(clean_args[1]);
                if (limit > 0 && limit < history_length) {
                    start_idx = history_length - limit;
                }
            } catch (...) {
                // If the parameter is non-numeric, fallback gracefully to printing all history
                start_idx = 0;
            }
        }

        for (int i = start_idx; i < history_length; ++i) {
            HIST_ENTRY* entry = history_get(i + history_base);
            if (entry) {
                cout << setw(5) << (i + 1) << "  " << entry->line << endl;
            }
        }
        return true;
    }
    return false;
}

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
    static vector<string> programmable_matches;
    static size_t programmable_index = 0;

    if (!state) {
        programmable_matches.clear();
        programmable_index = 0;

        string current_line(rl_line_buffer);
        stringstream ss(current_line);
        string base_cmd;
        ss >> base_cmd;

        if (programmable_completions.count(base_cmd)) {
            string script_path = programmable_completions[base_cmd];

            // Capture state parameters for target context injection variables
            string comp_line_val = current_line;
            string comp_point_val = to_string(rl_point);

            // Determine context arguments: argv[2] (current) and argv[3] (previous)
            string current_word = string(text);
            string prev_word = "";

            // Robust token splitting up to the current cursor position
            string current_line_up_to_point = string(rl_line_buffer).substr(0, rl_point);
            vector<string> words_before_cursor;
            stringstream line_ss(current_line_up_to_point);
            string temp_word;

            while (line_ss >> temp_word) {
                words_before_cursor.push_back(temp_word);
            }

            // Calculate previous word depending on if the user started writing the token or not
            if (!current_word.empty()) {
                if (words_before_cursor.size() >= 2) {
                    prev_word = words_before_cursor[words_before_cursor.size() - 2];
                }
            } else {
                if (!words_before_cursor.empty()) {
                    prev_word = words_before_cursor.back();
                }
            }

            int pipe_fds[2];
            if (pipe(pipe_fds) == 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    // Child process setup redirection
                    close(pipe_fds[0]);
                    dup2(pipe_fds[1], STDOUT_FILENO);
                    close(pipe_fds[1]);

                    // Set environment variables context exclusively for this isolated child scope
                    setenv("COMP_LINE", comp_line_val.c_str(), 1);
                    setenv("COMP_POINT", comp_point_val.c_str(), 1);

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

                    // Parse all newline-separated completion lines returned by the script
                    if (!output.empty()) {
                        stringstream output_ss(output);
                        string line;
                        while (getline(output_ss, line)) {
                            // Strip any trailing carriage returns if present
                            if (!line.empty() && line.back() == '\r') {
                                line.pop_back();
                            }
                            if (!line.empty()) {
                                programmable_matches.push_back(line);
                            }
                        }
                    }
                }
            }
        }
    }

    // Sequentially return every parsed match candidate line back to Readline
    if (programmable_index < programmable_matches.size()) {
        const string& match_str = programmable_matches[programmable_index++];
        char* match = (char*)malloc(match_str.length() + 1);
        strcpy(match, match_str.c_str());
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
        // Automatic reaping cycle checkpoint directly preceding the visual prompt invocation
        reap_background_jobs(true);

        char* input_raw = readline("$ ");
        
        if (!input_raw) {
            break; 
        }

        string command_line(input_raw);
        free(input_raw); 

        if (command_line.empty()) {
            continue;
        }

        // Commit all parsed entries safely to history
        add_history(command_line.c_str());

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

        // Check if the command should be run in the background
        bool run_in_background = false;
        if (clean_args.back() == "&") {
            run_in_background = true;
            clean_args.pop_back();
        }

        if (clean_args.empty()) {
            continue;
        }

        // Reconstruct the full string command value representation for reporting
        string full_cmd_string = "";
        for (size_t i = 0; i < clean_args.size(); ++i) {
            full_cmd_string += clean_args[i];
            if (i + 1 < clean_args.size()) {
                full_cmd_string += " ";
            }
        }
        if (run_in_background) {
            full_cmd_string += " &";
        }

        string cmd = clean_args[0];

        // Check for basic or multi-stage pipeline syntax (cmd1 | cmd2 | cmd3 ...)
        auto pipe_it = find(clean_args.begin(), clean_args.end(), "|");
        if (pipe_it != clean_args.end()) {
            // Group segments separated by '|'
            vector<vector<string>> pipeline_cmds;
            vector<string> current_sub_cmd;
            for (const auto& token : clean_args) {
                if (token == "|") {
                    if (!current_sub_cmd.empty()) {
                        pipeline_cmds.push_back(current_sub_cmd);
                        current_sub_cmd.clear();
                    }
                } else {
                    current_sub_cmd.push_back(token);
                }
            }
            if (!current_sub_cmd.empty()) {
                pipeline_cmds.push_back(current_sub_cmd);
            }

            size_t num_cmds = pipeline_cmds.size();
            int infile_fd = STDIN_FILENO; 
            vector<pid_t> child_pids;

            for (size_t i = 0; i < num_cmds; ++i) {
                int pipe_fds[2];
                if (i < num_cmds - 1) {
                    if (pipe(pipe_fds) < 0) {
                        perror("pipe");
                        break;
                    }
                }

                pid_t pid = fork();
                if (pid == 0) {
                    if (infile_fd != STDIN_FILENO) {
                        dup2(infile_fd, STDIN_FILENO);
                        close(infile_fd);
                    }

                    if (i < num_cmds - 1) {
                        dup2(pipe_fds[1], STDOUT_FILENO);
                        close(pipe_fds[0]);
                        close(pipe_fds[1]);
                    } else {
                        if (redirect_output) {
                            int flags = O_WRONLY | O_CREAT | (append_output ? O_APPEND : O_TRUNC);
                            int fd_out = open(redirect_file.c_str(), flags, 0644);
                            if (fd_out != -1) { dup2(fd_out, STDOUT_FILENO); close(fd_out); }
                        }
                        if (redirect_error) {
                            int flags = O_WRONLY | O_CREAT | (append_error ? O_APPEND : O_TRUNC);
                            int fd_err = open(error_file.c_str(), flags, 0644);
                            if (fd_err != -1) { dup2(fd_err, STDERR_FILENO); close(fd_err); }
                        }
                    }

                    bool dummy_exit;
                    if (!execute_builtin(pipeline_cmds[i][0], pipeline_cmds[i], dummy_exit)) {
                        vector<char*> c_sub_args;
                        for (auto& s : pipeline_cmds[i]) c_sub_args.push_back(&s[0]);
                        c_sub_args.push_back(nullptr);
                        execvp(c_sub_args[0], c_sub_args.data());
                    }
                    exit(0);
                } else if (pid > 0) {
                    child_pids.push_back(pid);
                    
                    if (infile_fd != STDIN_FILENO) {
                        close(infile_fd);
                    }
                    if (i < num_cmds - 1) {
                        close(pipe_fds[1]);
                        infile_fd = pipe_fds[0];
                    }
                }
            }

            if (run_in_background) {
                set<int> active_ids;
                for (const auto& job : background_jobs) active_ids.insert(job.job_id);
                int assigned_id = 1;
                while (active_ids.count(assigned_id)) assigned_id++;

                pid_t monitored_pid = child_pids.empty() ? -1 : child_pids.back();
                cout << "[" << assigned_id << "] " << monitored_pid << endl;

                BackgroundJob new_job;
                new_job.job_id = assigned_id;
                new_job.pid = monitored_pid;
                new_job.command = full_cmd_string;
                new_job.status = "Running";

                background_jobs.push_back(new_job);
                sort(background_jobs.begin(), background_jobs.end(), [](const BackgroundJob& a, const BackgroundJob& b) {
                    return a.job_id < b.job_id;
                });
            } else {
                for (pid_t p : child_pids) {
                    waitpid(p, nullptr, 0);
                }
            }
            continue;
        }

        // Setup sequential stream redirection buffers for foreground single commands/builtins
        streambuf* old_cout = cout.rdbuf();
        ofstream out_file;
        if (redirect_output) {
            out_file.open(redirect_file, ios::out | (append_output ? ios::app : ios::trunc));
            if (out_file.is_open()) cout.rdbuf(out_file.rdbuf());
        }

        streambuf* old_cerr = cerr.rdbuf();
        ofstream err_file;
        if (redirect_error) {
            err_file.open(error_file, ios::out | (append_error ? ios::app : ios::trunc));
            if (err_file.is_open()) cerr.rdbuf(err_file.rdbuf());
        }

        bool should_exit = false;
        if (execute_builtin(cmd, clean_args, should_exit)) {
            if (should_exit) {
                cout.rdbuf(old_cout);
                cerr.rdbuf(old_cerr);
                break;
            }
        } else {
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
                for (auto& s : clean_args) c_args.push_back(&s[0]);
                c_args.push_back(nullptr);

                pid_t pid = fork();
                if (pid == 0) {
                    if (redirect_output) {
                        int flags = O_WRONLY | O_CREAT | (append_output ? O_APPEND : O_TRUNC);
                        int fd_out = open(redirect_file.c_str(), flags, 0644);
                        if (fd_out != -1) { dup2(fd_out, STDOUT_FILENO); close(fd_out); }
                    }
                    if (redirect_error) {
                        int flags = O_WRONLY | O_CREAT | (append_error ? O_APPEND : O_TRUNC);
                        int fd_err = open(error_file.c_str(), flags, 0644);
                        if (fd_err != -1) { dup2(fd_err, STDERR_FILENO); close(fd_err); }
                    }
                    execvp(c_args[0], c_args.data());
                    exit(1); 
                } else {
                    if (run_in_background) {
                        set<int> active_ids;
                        for (const auto& job : background_jobs) active_ids.insert(job.job_id);
                        int assigned_id = 1;
                        while (active_ids.count(assigned_id)) assigned_id++;

                        cout << "[" << assigned_id << "] " << pid << endl;
                        
                        BackgroundJob new_job;
                        new_job.job_id = assigned_id;
                        new_job.pid = pid;
                        new_job.command = full_cmd_string;
                        new_job.status = "Running";
                        
                        background_jobs.push_back(new_job);
                        sort(background_jobs.begin(), background_jobs.end(), [](const BackgroundJob& a, const BackgroundJob& b) {
                            return a.job_id < b.job_id;
                        });
                    } else {
                        waitpid(pid, nullptr, 0);
                    }
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