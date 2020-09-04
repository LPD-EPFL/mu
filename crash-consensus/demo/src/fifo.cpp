
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Provide the file as arguments" << std::endl;
    return 1;
  }

  auto filename = argv[1];

  int fd = open(filename, O_WRONLY);
  if (fd == -1) {
    throw std::runtime_error("Could not open the fifo: " +
                             std::string(std::strerror(errno)));
  }

  char c;
  std::cout << "Type `p` to pause." << std::endl;

  while (std::cin >> c) {
    if (c != 'p') {
      continue;
    }

    if (write(fd, &c, 1) == -1) {
      throw std::runtime_error("Failed to write to the fifo: " +
                               std::string(std::strerror(errno)));
    }
  }

  return 0;
}
