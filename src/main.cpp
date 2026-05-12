#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

int main() {

  cout << unitbuf;
  cerr << unitbuf;

  while(true){
    cout << "$ ";
    string command;
    getline(std::cin, command);

    if(command == "exit") {
      break;
    }
    else if(command.substr(0,5) == "echo "){
      cout << command.substr(5) << endl;
    }
    else if(command.substr(0,5) == "type "){
      string com = command.substr(5);
      if(com=="exit" || com=="type" || com=="echo") {
        cout << com << " is a shell builtin" << endl;
      } 
      else {
        bool found = false;
        char* path_env = std::getenv("PATH");
        if (path_env) {
          std::stringstream ss_path(path_env);
          std::string path;
          while (std::getline(ss_path, path, ':')) {
            std::string full_path = path + '/' + com;
            if (access(full_path.c_str(), X_OK) == 0) {
              std::cout << com << " is " << full_path << std::endl;
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

      stringstream ss(command);
      string exe_name;
      ss >> exe_name;

      bool found = false;
      string executable_path;
      char* path_env = std::getenv("PATH");

      if (path_env) {
        stringstream ss_path(path_env);
        string path_dir;
        while (getline(ss_path, path_dir, ':')) {
          string full_path = path_dir + '/' + exe_name;
          if (access(full_path.c_str(), X_OK) == 0) {
            executable_path = full_path;
            found = true;
            break;
          }
        }
      }

      if (found) {
        // Collect arguments for execvp
        vector<string> args_vec;
        args_vec.push_back(exe_name);
        string arg;
        while (ss >> arg) {
          args_vec.push_back(arg);
        }

        vector<char*> c_args;
        for (auto& s : args_vec) {
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
        cout << command << ": command not found" << endl;
      }
    }
  }
  return 0;
}