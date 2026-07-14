#include <iostream>
#include <string>

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // TODO: Uncomment the code below to pass the first stage
  do {
    std::cout << "$ ";

    std::string command;
    std::cin >> command;

    std::cerr << command << ": command not found" << std::endl;
    // Flush after every std::cout / std:cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
  } while (true);
}
