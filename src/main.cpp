#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

inline void Flush() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
}

/* checks if a file exists in the given absolute path */
bool fileExists(const std::string &path) {
  return std::filesystem::exists(path);
}

/* Checks if a filepath is executable */
bool isFileExecutable(const std::string &filepath) {
  std::filesystem::perms active_permissions =
      std::filesystem::status(filepath).permissions();

  return ((active_permissions & std::filesystem::perms::owner_exec) !=
              std::filesystem::perms::none ||
          (active_permissions & std::filesystem::perms::group_exec) !=
              std::filesystem::perms::none ||
          (active_permissions & std::filesystem::perms::others_exec) !=
              std::filesystem::perms::none);
}

/* Checks if the given argument is an external command */
bool isExternalCommand(std::string &argument) {
  const char *path_val = std::getenv("PATH");

  if (path_val != nullptr) {
    std::string path_str(path_val);
    std::stringstream ps(path_str);

    std::vector<std::string> pathsArray;
    std::string token;

    while (std::getline(ps, token, ':')) {
      pathsArray.push_back(token);
    }

    bool found = false;
    for (int i = 0; i < pathsArray.size(); i++) {
      const std::string filepath = pathsArray[i] + "/" + argument;
      if (fileExists(filepath) and isFileExecutable(filepath)) {
        found = true;
        break;
      } else
        continue;
    }
    return found;
  } else {
    return false;
  }
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

    // pwd shell builtin
    if (command == "pwd") {
      try {
        std::filesystem::path cwd = std::filesystem::current_path();
        std::cout << cwd.string() << std::endl;

      } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error " << e.code() << ": " << e.what() << std::endl;
      }
      Flush();
      continue;
    }

    // type command logic
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
            if (fileExists(filepath) and isFileExecutable(filepath)) {
              std::cout << argument << " is " << filepath << std::endl;
              notfound = false;
              Flush();
              break;
            } else
              continue;
          }
          if (notfound)
            std::cerr << argument << ": not found" << std::endl;
        } else {
          std::cerr << argument << ": not found" << std::endl;
        }
      }
      continue;
    }

    // echo command logic
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

    // executing external commands
    if (isExternalCommand(command)) {
      std::string line;
      std::getline(std::cin, line);
      std::vector<std::string> tokens;
      std::istringstream iss(line);
      std::string nextword;

      std::vector<char *> fullCommand;
      tokens.push_back(command);

      while (iss >> nextword) {
        tokens.push_back(nextword);
      }

      for (auto &token : tokens) {
        // &token[0] gives a mutable char* pointer to the string's internal
        // buffer
        fullCommand.push_back(&token[0]);
      }
      fullCommand.push_back(nullptr);

      pid_t pid = fork();

      if (pid < 0) {
        std::cerr << "fork failed... couldn't execute command!";
        continue;
      }

      if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
      }

      if (pid == 0) {
        execvp(fullCommand[0], fullCommand.data());
        perror("Exec failed"); // Only runs if execvp fails
        exit(1);
      }
    } else {
      std::string line;
      std::getline(std::cin, line);
      std::cerr << command << ": command not found" << std::endl;
      // Flush after every std::cout / std:cerr
      Flush();
    }
  } while (true);
  return 0;
}
