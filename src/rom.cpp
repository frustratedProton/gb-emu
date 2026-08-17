#include "types.hpp"

#include <fstream>
#include <iostream>
#include <vector>

std::vector<u8> load_rom(const std::string &path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};

  if (!file)
    throw std::runtime_error{"failed to open ROM: " + path};

  const std::streampos end = file.tellg();

  if (end < 0)
    throw std::runtime_error{"Failed to determine ROM size"};

  std::vector<u8> rom(static_cast<std::size_t>(end));

  file.seekg(0, std::ios::beg);

  if (!rom.empty()) {
    file.read(reinterpret_cast<char *>(rom.data()),
              static_cast<std::streamsize>(rom.size()));

    if (!file) {
      throw std::runtime_error{"Failed to read complete ROM"};
    }
  }

  return rom;
}

bool valid_header_checksum(const std::vector<u8> &rom) {
  if (rom.size() <= 0x14D)
    return false;

  u8 checksum{};
  // This byte contains an 8-bit checksum
  // computed from the cartridge header
  // bytes $0134–014C.
  for (std::size_t addr = 0x0134; addr <= 0x14C; ++addr) {
    checksum = static_cast<u8>(checksum - rom[addr] - 1);
  }

  return checksum == rom[0x14D];
}

void print_title(const std::vector<u8> &rom) {
  if (rom.size() <= 0x143)
    throw std::runtime_error{"File too small"};

  std::cout << "Title: ";

  // 0134-0143 — Title
  for (std::size_t addr = 0x0134; addr <= 0x143; ++addr) {
    const u8 byte = rom[addr];

    if (byte == 0)
      break;

    if (byte >= 32 && byte <= 126)
      std::cout << static_cast<char>(byte);
  }
  std::cout << '\n';
}
