#include <iostream>
#include <string>
#include <sstream>
#include <unistd.h>

using namespace std;

int main() {
  // Flush after every std::cout / std:cerr
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
        // Path search logic starts here
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
      cout << command  << ": command not found" << endl;
    }
  }
}