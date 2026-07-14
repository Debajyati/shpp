#include <iostream>
#include <sstream>
#include <string>

inline void Flush() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
}

int main(int argc, char *argv[]) {
  Flush();

  do {
    std::cout << "$ ";

    std::string command;
    std::cin >> command;

    // the exit builtin. when shell recieves the exit command, it should
    // terminate immediately.
    if (command == "exit") {
      break;
    }

    if (command == "type") {
      std::string argument;

      if (std::cin >> argument) {
        if (argument == "echo" || argument == "type" || argument == "exit") {
          std::cout << argument << " is a shell builtin" << std::endl;
          Flush();
        } else {
          std::cout << argument << ": not found" << std::endl;
        }
      }
      continue;
    }

    if (command == "echo") {
      std::string line;
      std::getline(std::cin, line);

      std::istringstream iss(line);

      std::string nextword;

      while (iss >> nextword) {
        std::cout << nextword << " ";
      }
      std::cout << std::endl;

      Flush();
      continue;
    }

    std::cerr << command << ": command not found" << std::endl;
    // Flush after every std::cout / std:cerr
    Flush();
  } while (true);
  return 0;
}
