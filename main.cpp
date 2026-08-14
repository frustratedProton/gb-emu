#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iosfwd>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <cassert>

using u8 = std::uint8_t;
using u16 = std::uint16_t;

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

class Bus {
public:
  explicit Bus(const std::vector<u8> &rom) : m_rom(rom) {}

  [[nodiscard]] u8 read(u16 addr) const;
  void write(u16 addr, u8 value);

private:
  const std::vector<u8> &m_rom;
  std::array<u8, 0x2000> m_vram{}; // video ram
  std::array<u8, 0x2000> m_wram{}; // work ram
  std::array<u8, 0x00A0> m_oam{};  // object attribute memory
  std::array<u8, 0x0080> m_io{};
  std::array<u8, 0x007F> m_hram{}; // high ram

  u8 m_ie{};
};

u8 Bus::read(u16 addr) const {
  // Cartridge ROM
  if (addr <= 0x7FFF) {
    if (static_cast<std::size_t>(addr) < m_rom.size())
      return m_rom.at(addr);

    return 0xFF;
  }

  // Video RAM
  if (addr <= 0x9FFF) {
    return m_vram.at(addr - 0x8000);
  }

  // External cartridge RAM; not supported yet
  if (addr <= 0xBFFF) {
    return 0xFF;
  }

  // Work RAM
  if (addr <= 0xDFFF) {
    return m_wram.at(addr - 0xC000);
  }

  // Echo RAM: mirror of 0xC000-0xDDFF
  if (addr <= 0xFDFF) {
    return m_wram.at(addr - 0xE000);
  }

  // OAM
  if (addr <= 0xFE9F) {
    return m_oam.at(addr - 0xFE00);
  }

  // Prohibited area
  if (addr <= 0xFEFF) {
    return 0xFF;
  }

  // I/O registers
  if (addr <= 0xFF7F) {
    return m_io.at(addr - 0xFF00);
  }

  // High RAM
  if (addr <= 0xFFFE) {
    return m_hram.at(addr - 0xFF80);
  }

  // The only remaining u16 address is 0xFFFF
  return m_ie;
}

void Bus::write(u16 addr, u8 value) {
  // Cartridge ROM is read-only for ROM-only cartridges.
  if (addr <= 0x7FFF) {
    return;
  }

  if (addr <= 0x9FFF) {
    m_vram.at(addr - 0x8000) = value;
    return;
  }

  // External cartridge RAM is unsupported for now.
  if (addr <= 0xBFFF) {
    return;
  }

  if (addr <= 0xDFFF) {
    m_wram.at(addr - 0xC000) = value;
    return;
  }

  // Echo RAM writes modify the corresponding WRAM byte.
  if (addr <= 0xFDFF) {
    m_wram.at(addr - 0xE000) = value;
    return;
  }

  if (addr <= 0xFE9F) {
    m_oam.at(addr - 0xFE00) = value;
    return;
  }

  if (addr <= 0xFEFF) {
    return;
  }

  if (addr <= 0xFF7F) {
    m_io.at(addr - 0xFF00) = value;
    return;
  }

  if (addr <= 0xFFFE) {
    m_hram.at(addr - 0xFF80) = value;
    return;
  }

  m_ie = value;
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: gbemu <path-to-rom.gb>\n";
      return 1;
    }

    const std::vector<u8> rom = load_rom(argv[1]);
    Bus bus{rom};

    assert(bus.read(0x134) == 'T');

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