#include <cstdlib>
#include <stdexcept>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

inline void Flush() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
}

// Get current user's home directory
std::string getHomeDirectory() {
#if defined(_WIN32)
    const char* homeDrive = std::getenv("HOMEDRIVE");
    const char* homePath  = std::getenv("HOMEPATH");
    const char* userProfile = std::getenv("USERPROFILE");

    if (userProfile) return userProfile;
    if (homeDrive && homePath) return std::string(homeDrive) + homePath;

    throw std::runtime_error("Unable to determine home directory on Windows");
#else
    const char* home = std::getenv("HOME");
    if (home) return home;

    struct passwd* pwd = getpwuid(getuid());
    if (pwd && pwd->pw_dir) return pwd->pw_dir;

    throw std::runtime_error("Unable to determine home directory on Unix");
#endif
}

// Expand leading '~' in a path
std::filesystem::path expandUserPath(const std::string& path) {
    if (path.empty() || path[0] != '~') {
        return path; // No tilde, return as-is
    }

    // Handle "~" or "~/..."
    if (path.size() == 1 || path[1] == '/') {
        return std::filesystem::path(getHomeDirectory()) / path.substr(2);
    }

    // Handle "~username" (Unix only)
#if !defined(_WIN32)
    size_t slashPos = path.find('/');
    std::string user = (slashPos == std::string::npos) ? path.substr(1) : path.substr(1, slashPos - 1);

    struct passwd* pwd = getpwnam(user.c_str());
    if (!pwd || !pwd->pw_dir) {
        throw std::runtime_error("User '" + user + "' not found");
    }

    if (slashPos == std::string::npos) {
        return pwd->pw_dir;
    } else {
        return std::filesystem::path(pwd->pw_dir) / path.substr(slashPos + 1);
    }
#else
    throw std::runtime_error("~username expansion not supported on Windows");
#endif
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

    // the cd shell builtin. changes the working directory
    if (command == "cd") {
      std::string line, argument;
      std::getline(std::cin, line);

      std::istringstream iss(line);

      if (!(iss >> argument)) {
        // If no argument was provided (just typed "cd"), default to HOME
        // directory
        char *home = std::getenv("HOME");
        argument = home ? home : "/";
      }

      std::filesystem::path directory(expandUserPath(argument));
      // Use error_code to avoid exceptions
      std::error_code ec;

      // Check if path exists first
      if (!std::filesystem::exists(directory, ec)) {
        if (ec) {
          std::cerr << "Error " << ec.value() << ": " << ec.message() << "\n";
        } else {
          std::cerr << "cd: " << argument << ": No such file or directory\n";
        }
        Flush();
        continue;
      }

      bool is_dir = std::filesystem::is_directory(directory, ec);

      if (ec) {
        std::cerr << "Error " << ec.value() << ": " << ec.message() << "\n";
        Flush();
        continue;
      }

      if (!is_dir) {
        std::cerr << "cd: " << argument << ": No such file or directory"
                  << std::endl;
        Flush();
        continue;
      } else {
        std::filesystem::current_path(directory);
        continue;
      }
    }

    // the pwd shell builtin
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

    // the type shell builtin
    if (command == "type") {
      std::string argument;

      const char *path_val = std::getenv("PATH");
      if (std::cin >> argument) {
        if (argument == "echo" || argument == "type" || argument == "exit" ||
            argument == "pwd" || argument == "cd") {
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

    // the echo shell builtin command logic
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
