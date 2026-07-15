#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Flush after every std::cout / std:cerr
inline void Flush() {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
}

// Get current user's home directory
std::string getHomeDirectory() {
#if defined(_WIN32)
  const char *homeDrive = std::getenv("HOMEDRIVE");
  const char *homePath = std::getenv("HOMEPATH");
  const char *userProfile = std::getenv("USERPROFILE");

  if (userProfile)
    return userProfile;
  if (homeDrive && homePath)
    return std::string(homeDrive) + homePath;

  throw std::runtime_error("Unable to determine home directory on Windows");
#else
  const char *home = std::getenv("HOME");
  if (home)
    return home;

  struct passwd *pwd = getpwuid(getuid());
  if (pwd && pwd->pw_dir)
    return pwd->pw_dir;

  throw std::runtime_error("Unable to determine home directory on Unix");
#endif
}

// Expand leading '~' in a path
std::filesystem::path expandUserPath(const std::string &path) {
  if (path.empty() || path[0] != '~') {
    return path; // No tilde, return as-is
  }

  // Handle "~" or "~/..."
  if (path.size() == 1 || path[1] == '/') {
    std::string home = getHomeDirectory();
    if (path.length() <= 2)
      return std::filesystem::path(home);
    return std::filesystem::path(home) / path.substr(2);
  }

  // Handle "~username" (Unix only)
#if !defined(_WIN32)
  size_t slashPos = path.find('/');
  std::string user = (slashPos == std::string::npos)
                         ? path.substr(1)
                         : path.substr(1, slashPos - 1);

  struct passwd *pwd = getpwnam(user.c_str());
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

std::vector<std::string> parseArguments(const std::string &command,
                                        const std::string &remainder) {
  std::vector<std::string> args;
  args.push_back(command); // The first element must always be the command name

  std::string current_arg = "";
  bool in_quotes = false;
  char ch;

  std::istringstream stream(remainder);
  stream >> std::noskipws; // Read spaces as literal characters

  while (stream >> ch) {
    if (ch == '\'') {
      in_quotes =
          !in_quotes; // Toggle quote state, skip pushing the quote itself
    } else if (ch == ' ' && !in_quotes) {
      // End of an unquoted token
      if (!current_arg.empty()) {
        args.push_back(current_arg);
        current_arg = "";
      }
    } else {
      current_arg += ch;
    }
  }
  // Don't forget the last token
  if (!current_arg.empty()) {
    args.push_back(current_arg);
  }

  return args;
}

int main(int argc, char *argv[]) {
  Flush();

  do {
    std::cout << "$ ";

    // Read the ENTIRE line immediately at the top of the loop
    std::string line;
    if (!std::getline(std::cin, line)) {
      break; // Handle EOF (Ctrl+D) safely
    }

    if (line.empty()) {
      continue;
    }

    // Parse out the first word as the command
    std::istringstream iss(line);
    std::string command;
    iss >> command;

    // the exit builtin. when shell recieves the exit command, it should
    // terminate immediately.
    if (command == "exit") {
      break;
    }

    // the cd shell builtin. changes the working directory
    if (command == "cd") {
      std::string argument;
      if (!(iss >> argument)) {
        char *home = std::getenv("HOME");
        argument = home ? home : "/";
      }

      std::filesystem::path directory(expandUserPath(argument));
      // Use error_code to avoid exceptions
      std::error_code ec;

      if (!std::filesystem::exists(directory, ec) ||
          !std::filesystem::is_directory(directory, ec)) {
        std::cerr << "cd: " << argument << ": No such file or directory\n";
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

      if (iss >> argument) {
        if (argument == "echo" || argument == "type" || argument == "exit" ||
            argument == "pwd" || argument == "cd") {
          std::cout << argument << " is a shell builtin" << std::endl;
        } else {
          const char *path_val = std::getenv("PATH");
          if (path_val != nullptr) {
            std::stringstream ps(path_val);
            std::string token;
            bool found = false;
            while (std::getline(ps, token, ':')) {
              std::string filepath = token + "/" + argument;
              if (fileExists(filepath) && isFileExecutable(filepath)) {
                std::cout << argument << " is " << filepath << std::endl;
                found = true;
                break;
              }
            }
            if (!found)
              std::cerr << argument << ": not found" << std::endl;
          } else {
            std::cerr << argument << ": not found" << std::endl;
          }
        }
      }
      Flush();
      continue;
    }

    std::string remainder = (iss.tellg() != -1) ? line.substr(iss.tellg()) : "";
    // the echo shell builtin command logic
    if (command == "echo") {
      // Parse args (we skip index 0 because args[0] is "echo")
      std::vector<std::string> args = parseArguments(command, remainder);

      for (size_t i = 1; i < args.size(); ++i) {
        std::cout << args[i];
        if (i + 1 < args.size()) {
          std::cout << " ";
        }
      }
      std::cout << std::endl;
      continue;
    }

    // executing external commands
    if (isExternalCommand(command)) {
      std::vector<std::string> tokens = parseArguments(command, remainder);

      // Build the mutable char* array required by execvp
      std::vector<char *> fullCommand;
      for (auto &token : tokens) {
        fullCommand.push_back(&token[0]);
      }
      fullCommand.push_back(nullptr);

      pid_t pid = fork();
      if (pid < 0) {
        std::cerr << "fork failed... couldn't execute command!\n";
        continue;
      }

      if (pid == 0) {
        // Child process
        execvp(fullCommand[0], fullCommand.data());
        perror("Exec failed");
        std::exit(1); // Make sure the child exits if exec fails
      } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
      }
      continue;
    }
    // command not found
    else {
      std::cerr << command << ": not found" << std::endl;
      // Flush after every std::cout / std:cerr
      Flush();
    }
  } while (true);
  return 0;
}
