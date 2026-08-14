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
  // cartridge ROM is read-only for ROM-only cartridges
  if (addr <= 0x7FFF) {
    return;
  }

  if (addr <= 0x9FFF) {
    m_vram.at(addr - 0x8000) = value;
    return;
  }

  // external cartridge RAM is unsupported for now
  if (addr <= 0xBFFF) {
    return;
  }

  if (addr <= 0xDFFF) {
    m_wram.at(addr - 0xC000) = value;
    return;
  }

  // Echo RAM writes modify the corresponding WRAM byte
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

#include <cassert>

void run_bus_tests(const std::vector<u8> &rom) {
  assert(rom.size() >= 0x8000);

  Bus bus{rom};

  // cartridge ROM can be read
  assert(bus.read(0x0000) == rom.at(0x0000));
  assert(bus.read(0x0134) == rom.at(0x0134));
  assert(bus.read(0x7FFF) == rom.at(0x7FFF));

  // cartridge ROM cannot be modified
  const u8 original = bus.read(0x0134);

  bus.write(0x0134, static_cast<u8>(original ^ 0xFF));

  assert(bus.read(0x0134) == original);

  // VRAM works
  bus.write(0x8000, 0x11);
  bus.write(0x9FFF, 0x12);

  assert(bus.read(0x8000) == 0x11);
  assert(bus.read(0x9FFF) == 0x12);

  // external cartridge RAM is unsupported for now
  bus.write(0xA000, 0x21);
  bus.write(0xBFFF, 0x22);

  assert(bus.read(0xA000) == 0xFF);
  assert(bus.read(0xBFFF) == 0xFF);

  // WRAM works
  bus.write(0xC123, 0x42);
  bus.write(0xDFFF, 0x43);

  assert(bus.read(0xC123) == 0x42);
  assert(bus.read(0xDFFF) == 0x43);

  // Echo RAM reflects WRAM
  assert(bus.read(0xE123) == 0x42);

  // Echo RAM writes reflect back into WRAM
  bus.write(0xE456, 0x73);

  assert(bus.read(0xC456) == 0x73);

  // OAM works
  bus.write(0xFE00, 0x31);
  bus.write(0xFE9F, 0x32);

  assert(bus.read(0xFE00) == 0x31);
  assert(bus.read(0xFE9F) == 0x32);

  // Prohibited memory ignores writes and reads as 0xFF
  bus.write(0xFEA0, 0x41);
  bus.write(0xFEFF, 0x42);

  assert(bus.read(0xFEA0) == 0xFF);
  assert(bus.read(0xFEFF) == 0xFF);

  // I/O storage works for now
  bus.write(0xFF01, 0x51);
  bus.write(0xFF7F, 0x52);

  assert(bus.read(0xFF01) == 0x51);
  assert(bus.read(0xFF7F) == 0x52);

  // HRAM works and does not overlap VRAM
  bus.write(0xFF80, 0x61);
  bus.write(0xFFFE, 0x62);

  assert(bus.read(0xFF80) == 0x61);
  assert(bus.read(0xFFFE) == 0x62);
  assert(bus.read(0x8000) == 0x11);

  // Interrupt Enable register works
  bus.write(0xFFFF, 0x1F);

  assert(bus.read(0xFFFF) == 0x1F);
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: gbemu <path-to-rom.gb>\n";
      return 1;
    }

    const std::vector<u8> rom = load_rom(argv[1]);

    if (rom.size() >= 2 && rom[0] == 0x50 && rom[1] == 0x4B) {
      throw std::runtime_error{
          "The provided file is a ZIP archive. Extract the .gb file first."};
    }

    std::cout << "Loaded " << rom.size() << " bytes\n";
    print_title(rom);

    std::cout << "Header checksum: "
              << (valid_header_checksum(rom) ? "OK" : "FAILED") << '\n';

    run_bus_tests(rom);
    std::cout << "Bus tests passed\n";

  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}