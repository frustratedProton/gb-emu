#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iosfwd>
#include <iostream>
#include <stdexcept>
#include <vector>

using u8t = std::uint8_t;

std::vector<u8t> load_rom(const std::string &path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};

  if (!file)
    throw std::runtime_error{"failed to open ROM: " + path};

  const std::streampos end = file.tellg();

  if (end < 0)
    throw std::runtime_error{"Failed to determine ROM size"};

  std::vector<u8t> rom(static_cast<std::size_t>(end));

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

bool valid_header_checksum(const std::vector<u8t> &rom) {
  if (rom.size() <= 0x14D)
    return false;

  u8t checksum{};
  // This byte contains an 8-bit checksum
  // computed from the cartridge header
  // bytes $0134–014C.
  for (std::size_t addr = 0x0134; addr <= 0x14C; ++addr) {
    checksum = static_cast<u8t>(checksum - rom[addr] - 1);
  }

  return checksum == rom[0x14D];
}

void print_title(const std::vector<u8t> &rom) {
  if (rom.size() <= 0x143)
    throw std::runtime_error{"File too small"};

  std::cout << "Title: ";

  // 0134-0143 — Title
  for (std::size_t addr = 0x0134; addr <= 0x143; ++addr) {
    const u8t byte = rom[addr];

    if (byte == 0)
      break;

    if (byte >= 32 && byte <= 126)
      std::cout << static_cast<char>(byte);
  }
  std::cout << '\n';
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: gbemu <path-to-rom.gb>\n";
      return 1;
    }

    const std::vector<u8t> rom = load_rom(argv[1]);

    if (rom.size() >= 2 && rom[0] == 0x50 && rom[1] == 0x4B) {
      throw std::runtime_error{
          "The provided file is a ZIP archive. Extract the .gb file first."};
    }

    std::cout << "Loaded " << rom.size() << " bytes\n";
    print_title(rom);

    std::cout << "Header checksum: "
              << (valid_header_checksum(rom) ? "OK" : "FAILED") << '\n';
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}