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
    else {
      cout << command  << ": command not found" << endl;
    }
  }
}
