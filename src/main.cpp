#include <iostream>
#include <string>
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
      if(com=="exit" || com=="type" || com=="echo") cout << com << " is a shell builtin" << endl;
      else cout << com << ": not found" << endl;
    }
    else {
      cout << command  << ": command not found" << endl;
    }
  }
}
