#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
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

std::vector<std::string> parseArguments(const std::string &line) {
  std::vector<std::string> args;
  std::string current_arg = "";
  bool in_single_quotes = false;
  bool in_double_quotes = false;

  std::istringstream stream(line);
  stream >> std::noskipws;
  char ch;

  while (stream >> ch) {
    // 1. Handle Single Quotes State
    if (in_single_quotes) {
      if (ch == '\'') {
        in_single_quotes = false;
      } else {
        current_arg += ch;
      }
      continue;
    }

    // 2. Handle Double Quotes State
    if (in_double_quotes) {
      if (ch == '"') {
        in_double_quotes = false;
      } else if (ch == '\\') {
        char next_ch;
        if (stream >> next_ch) {
          if (next_ch == '"' || next_ch == '\\' || next_ch == '$' ||
              next_ch == '\n') {
            current_arg += next_ch;
          } else {
            current_arg += ch;
            current_arg += next_ch;
          }
        } else {
          current_arg += ch;
        }
      } else {
        current_arg += ch;
      }
      continue;
    }

    // 3. Handle Unquoted State
    if (ch == '\'') {
      in_single_quotes = true;
    } else if (ch == '"') {
      in_double_quotes = true;
    } else if (ch == '\\') {
      char next_ch;
      if (stream >> next_ch) {
        current_arg += next_ch;
      } else {
        current_arg += ch;
      }
    } else if (ch == ' ') {
      // Space marks the end of a token (command or argument) only when unquoted
      if (!current_arg.empty()) {
        args.push_back(current_arg);
        current_arg = "";
      }
    } else {
      current_arg += ch;
    }
  }

  if (!current_arg.empty()) {
    args.push_back(current_arg);
  }

  return args;
}

// Simply loop through tokens. By default starting from index 1
void printTokens(std::vector<std::string> &tokens, std::ostream &out,
                 size_t end, size_t start = 1) {
  for (size_t i = start; i < end; ++i) {
    out << tokens[i];
    if (i + 1 < end) {
      out << " ";
    }
  }
  out << std::endl;
  Flush();
};

int main(int argc, char *argv[]) {
  Flush();

  while (true) {
    std::cout << "$ ";
    Flush();

    std::string line;
    if (!std::getline(std::cin, line)) {
      break; // Handle EOF (Ctrl+D)
    }

    // 1. Parse the entire line into tokens instantly
    std::vector<std::string> tokens = parseArguments(line);

    if (tokens.empty()) {
      continue;
    }

    // 2. The first token is always the command name
    std::string command = tokens[0];

    // the exit builtin
    if (command == "exit") {
      break;
    }

    // the cd shell builtin
    if (command == "cd") {
      std::string argument;
      if (tokens.size() > 1) {
        argument = tokens[1];
      } else {
        char *home = std::getenv("HOME");
        argument = home ? home : "/";
      }

      std::filesystem::path directory(expandUserPath(argument));
      std::error_code ec;

      if (!std::filesystem::exists(directory, ec) ||
          !std::filesystem::is_directory(directory, ec)) {
        std::cerr << "cd: " << argument << ": No such file or directory\n";
        continue;
      }
      std::filesystem::current_path(directory);
      continue;
    }

    // the pwd shell builtin
    if (command == "pwd") {
      try {
        if ((tokens[1] == ">" or tokens[1] == "1>") and !tokens[2].empty()) {
          std::ofstream output_file(tokens[2], std::ios::trunc);
          output_file << std::filesystem::current_path().string() << std::endl;
        } else
          std::cout << std::filesystem::current_path().string() << std::endl;
      } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error " << e.code() << ": " << e.what() << std::endl;
      }
      Flush();
      continue;
    }

    // the type shell builtin
    if (command == "type") {
      if (tokens.size() > 1) {
        std::string argument = tokens[1];
        if (argument == "echo" || argument == "type" || argument == "exit" ||
            argument == "pwd" || argument == "cd") {
          if ((!tokens[2].empty() and
               (tokens[2] == ">" or tokens[2] == "1>")) &&
              !tokens[3].empty()) {
            std::ofstream output_file(tokens[3], std::ios::trunc);
            output_file << argument << " is a shell builtin" << std::endl;
          } else {
            std::cout << argument << " is a shell builtin" << std::endl;
          }
        } else {
          const char *path_val = std::getenv("PATH");
          if (path_val != nullptr) {
            std::stringstream ps(path_val);
            std::string token;
            bool found = false;
            while (std::getline(ps, token, ':')) {
              std::string filepath = token + "/" + argument;
              if (fileExists(filepath) && isFileExecutable(filepath)) {
                if ((!tokens[2].empty() and
                     (tokens[2] == ">" or tokens[2] == "1>")) &&
                    !tokens[3].empty()) {
                  std::ofstream output_file(tokens[3], std::ios::trunc);
                  output_file << argument << " is " << filepath << std::endl;
                } else {
                  std::cout << argument << " is " << filepath << std::endl;
                }
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
      continue;
    }

    // THE ECHO BUILTIN
    if (command == "echo") {
      if (tokens.size() > 2) {
        int output_redirect_idx = -1;
        bool is_stderr = false;

        // Scan for ANY redirection operator inside echo's arguments
        for (size_t i = 1; i < tokens.size(); i++) {
          if (tokens[i] == ">" || tokens[i] == "1>" || tokens[i] == "2>") {
            output_redirect_idx = i;
            if (tokens[i] == "2>") {
              is_stderr = true;
            }
            break;
          }
        }

        if (output_redirect_idx == -1) {
          printTokens(tokens, std::cout, tokens.size());
          continue;
        } else if (output_redirect_idx + 1 < tokens.size()) {
          // Choose the right output stream or file mode
          std::ofstream outFile;

          // If it is 2>, echo itself still writes to std::cout!
          // (Because echo outputs normal text to stdout, not stderr).
          // But we must completely drop the 2> and filename from printing.
          if (!is_stderr) {
            outFile.open(tokens[output_redirect_idx + 1], std::ios_base::trunc);
          }

          // Print tokens up to the redirection index
          std::ostream &targetStream = is_stderr ? std::cout : outFile;

          if (is_stderr || outFile.is_open()) {
            for (size_t i = 1; i < output_redirect_idx; i++) {
              targetStream << tokens[i];
              if (i != output_redirect_idx - 1) {
                targetStream << ' ';
              }
            }
            if (!is_stderr)
              outFile.close();
          }

          // Handle any tokens trailing AFTER the filename
          if (output_redirect_idx + 1 < tokens.size() - 1) {
            if (is_stderr) {
              for (size_t i = output_redirect_idx + 2; i < tokens.size(); i++) {
                std::cout << ' ' << tokens[i];
              }
            } else {
              std::ofstream outFileInAppendMode(tokens[output_redirect_idx + 1],
                                                std::ios::app);
              if (outFileInAppendMode.is_open()) {
                for (size_t i = output_redirect_idx + 2; i < tokens.size();
                     i++) {
                  outFileInAppendMode << ' ' << tokens[i];
                }
              }
              outFileInAppendMode.close();
            }
          }

          // Wrap up the line ending
          if (is_stderr) {
            std::cout << std::endl;
          } else {
            std::ofstream outFileInAppendMode(tokens[output_redirect_idx + 1],
                                              std::ios::app);
            outFileInAppendMode << std::endl;
            outFileInAppendMode.close();
          }
          continue;
        }
      } else {
        printTokens(tokens, std::cout, tokens.size());
        continue;
      }
    }

    // EXTERNAL COMMAND LOGIC
    if (isExternalCommand(command)) {
      int output_redirect_idx = -1;
      bool is_stderr = false; // Tracks if we found 2>

      // Look through tokens to find ANY redirection operator (>, 1>, or 2>)
      for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == ">" || tokens[i] == "1>" || tokens[i] == "2>") {
          output_redirect_idx = i;
          if (tokens[i] == "2>") {
            is_stderr = true;
          }
          break;
        }
      }

      // Build the mutable char* array up to the redirection operator index
      std::vector<char *> fullCommand;
      size_t end =
          (output_redirect_idx == -1) ? tokens.size() : output_redirect_idx;

      for (size_t i = 0; i < end; i++) {
        auto &token = tokens[i];
        fullCommand.push_back(&token[0]);
      }
      fullCommand.push_back(nullptr);

      pid_t pid = fork();
      if (pid < 0) {
        std::cerr << "fork failed... couldn't execute command!\n";
        continue;
      }

      if (pid == 0) {
        // Child process execution window
        // 3. Open the file for writing
        // O_WRONLY: Write-only mode
        // O_CREAT: Create file if it doesn't exist
        // O_TRUNC: Clear existing content (use O_APPEND to append instead)
        // 0644: Read/Write permissions for owner, Read-only for others
        if (output_redirect_idx != -1 &&
            output_redirect_idx + 1 < tokens.size()) {
          const char *outFile = tokens[output_redirect_idx + 1].c_str();

          int fd = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (fd < 0) {
            std::exit(1);
          }

          // DYNAMIC FILENO REDIRECTION:
          // If we flagged is_stderr as true, redirect STDERR_FILENO (2).
          // Otherwise, redirect STDOUT_FILENO (1).
          int target_fd = is_stderr ? STDERR_FILENO : STDOUT_FILENO;

          if (dup2(fd, target_fd) < 0) {
            std::exit(1);
          }
          close(fd);
        }

        execvp(fullCommand[0], fullCommand.data());
        perror("Exec failed");
        std::exit(1);
      } else {
        int status;
        waitpid(pid, &status, 0);
      }
      continue;
    }

    // COMMAND NOT FOUND
    else {
      std::cerr << command << ": not found" << std::endl;
      Flush();
    }
  }
  return 0;
}
