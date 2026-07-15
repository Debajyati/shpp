#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

inline void Flush() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
}

bool fileExists(const std::string &path) {
  return std::filesystem::exists(path);
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

      const char *path_val = std::getenv("PATH");
      if (std::cin >> argument) {
        if (argument == "echo" || argument == "type" || argument == "exit") {
          std::cout << argument << " is a shell builtin" << std::endl;
          Flush();
        } else if (path_val != nullptr) {
          std::string path_str(path_val);
          std::stringstream ps(path_str);

          std::vector<std::string> pathsArray;
          std::string token;

          while (std::getline(ps, token, ':')) {
            pathsArray.push_back(token);
          }

          bool notfound = true;
          for (int i = 0; i < pathsArray.size(); i++) {
            const std::string filepath = pathsArray[i] + "/" + argument;
            if (fileExists(filepath)) {
              std::filesystem::perms active_permissions =
                  std::filesystem::status(filepath).permissions();

              if ((active_permissions & std::filesystem::perms::owner_exec) !=
                      std::filesystem::perms::none ||
                  (active_permissions & std::filesystem::perms::group_exec) !=
                      std::filesystem::perms::none ||
                  (active_permissions & std::filesystem::perms::others_exec) !=
                      std::filesystem::perms::none) {

                std::cout << argument << " is " << filepath << std::endl;
                notfound = false;
                Flush();
                break;
              } else
                continue;
            }
          }
          if (notfound)
            std::cerr << argument << ": not found" << std::endl;
        } else {
          std::cerr << argument << ": not found" << std::endl;
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
