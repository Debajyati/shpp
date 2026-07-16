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

struct RedirectionInfo {
  int redirect_idx = -1;
  bool is_stderr = false;
  bool is_append = false;
  std::string filename = "";
  size_t cmd_end_idx = 0; // Where the actual command arguments end
};

// Scans tokens starting from a given index to find redirection operators
RedirectionInfo parseRedirection(const std::vector<std::string> &tokens,
                                 size_t start_idx = 1) {
  RedirectionInfo info;
  info.cmd_end_idx = tokens.size();

  for (size_t i = start_idx; i < tokens.size(); i++) {
    if (tokens[i] == ">" || tokens[i] == "1>" || tokens[i] == "2>" ||
        tokens[i] == ">>" || tokens[i] == "1>>" || tokens[i] == "2>>") {

      info.redirect_idx = i;
      info.is_stderr = (tokens[i] == "2>" || tokens[i] == "2>>");
      info.is_append =
          (tokens[i] == ">>" || tokens[i] == "1>>" || tokens[i] == "2>>");

      if (i + 1 < tokens.size()) {
        info.filename = tokens[i + 1];
      }
      info.cmd_end_idx = i;
      break;
    }
  }
  return info;
}

// RAII class to temporarily redirect std::cout or std::cerr for builtins
class BuiltinRedirectionGuard {
  std::streambuf *old_buf = nullptr;
  std::ostream *target_stream = nullptr;
  std::ofstream file_stream;

public:
  BuiltinRedirectionGuard(const RedirectionInfo &info,
                          bool command_failed = false) {
    if (info.redirect_idx == -1 || info.filename.empty())
      return;

    // Determine if the output generation should go to the redirected file.
    // For type/pwd/echo, we match the stream type or status:
    // e.g. if command failed (stderr) and user redirected stderr, OR if command
    // succeeded and user redirected stdout.
    bool match_stdout = !info.is_stderr && !command_failed;
    bool match_stderr = info.is_stderr && command_failed;

    // Special case: echo always prints to stdout stream conceptually, pwd
    // always prints to stdout. If a command prints to STDOUT but user
    // redirected STDERR, the file is just created/cleared.
    std::ios_base::openmode mode =
        info.is_append ? std::ios_base::app : std::ios_base::trunc;

    file_stream.open(info.filename, mode);
    if (!file_stream.is_open())
      return;

    if (match_stdout || (info.is_stderr && !command_failed)) {
      // If it's a stdout command, check if we hook up cout or just leave it
      // open
      if (!info.is_stderr) {
        target_stream = &std::cout;
        old_buf = std::cout.rdbuf(file_stream.rdbuf());
      }
    } else if (match_stderr || (!info.is_stderr && command_failed)) {
      // If it's a stderr error output (like type command not found)
      if (info.is_stderr) {
        target_stream = &std::cerr;
        old_buf = std::cerr.rdbuf(file_stream.rdbuf());
      }
    }
  }

  ~BuiltinRedirectionGuard() {
    if (old_buf && target_stream) {
      target_stream->rdbuf(
          old_buf); // Restore original stream buffer automatically
    }
    if (file_stream.is_open()) {
      file_stream.close();
    }
  }
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
        RedirectionInfo redir = parseRedirection(tokens, 1);
        {
          BuiltinRedirectionGuard guard(redir, false);
          std::cout << std::filesystem::current_path().string() << std::endl;
        }
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
        RedirectionInfo redir = parseRedirection(tokens, 2);

        std::string type_output = "";
        bool found = false;

        if (argument == "echo" || argument == "type" || argument == "exit" ||
            argument == "pwd" || argument == "cd") {
          type_output = argument + " is a shell builtin";
          found = true;
        } else {
          const char *path_val = std::getenv("PATH");
          if (path_val != nullptr) {
            std::stringstream ps(path_val);
            std::string token;
            while (std::getline(ps, token, ':')) {
              std::string filepath = token + "/" + argument;
              if (fileExists(filepath) && isFileExecutable(filepath)) {
                type_output = argument + " is " + filepath;
                found = true;
                break;
              }
            }
          }
          if (!found)
            type_output = argument + ": not found";
        }

        // The Guard handles standard text streams vs files safely!
        {
          BuiltinRedirectionGuard guard(redir, !found);
          if (found) {
            std::cout << type_output << std::endl;
          } else {
            std::cerr << type_output << std::endl;
          }
        }
      }
      Flush();
      continue;
    }

    // THE ECHO BUILTIN
    if (command == "echo") {
      RedirectionInfo redir = parseRedirection(tokens, 1);
      {
        BuiltinRedirectionGuard guard(redir, false);
        // Print everything up to the redirection symbol location
        printTokens(tokens, std::cout, redir.cmd_end_idx);
      }
      continue;
    }

    // EXTERNAL COMMAND LOGIC
    if (isExternalCommand(command)) {
      RedirectionInfo redir = parseRedirection(tokens, 1);

      std::vector<char *> fullCommand;
      for (size_t i = 0; i < redir.cmd_end_idx; i++) {
        fullCommand.push_back(&tokens[i][0]);
      }
      fullCommand.push_back(nullptr);

      pid_t pid = fork();
      if (pid < 0) {
        std::cerr << "fork failed... couldn't execute command!\n";
        continue;
      }

      if (pid == 0) {
        // Child process execution window
        // O_WRONLY: Write-only mode
        // O_CREAT: Create file if it doesn't exist
        // O_TRUNC: Clear existing content (use O_APPEND to append instead)
        // 0644: Read/Write permissions for owner, Read-only for others
        if (redir.redirect_idx != -1 && !redir.filename.empty()) {
          int file_mode = redir.is_append ? O_APPEND : O_TRUNC;
          int fd = open(redir.filename.c_str(), O_WRONLY | O_CREAT | file_mode,
                        0644);

          if (fd < 0)
            std::exit(1);

          int target_fd = redir.is_stderr ? STDERR_FILENO : STDOUT_FILENO;
          if (dup2(fd, target_fd) < 0)
            std::exit(1);
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
