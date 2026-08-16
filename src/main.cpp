#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;

enum class Flag : u8 {
  Z = 0x80,
  N = 0x40,
  H = 0x20,
  C = 0x10,
};

struct Register {
  u8 a{};
  u8 f{};
  u8 b{};
  u8 c{};
  u8 d{};
  u8 e{};
  u8 h{};
  u8 l{};

  u16 sp{};
  u16 pc{};
};

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

class Cpu {
public:
  explicit Cpu(Bus &bus) : m_bus(bus) { reset_post_boot_dmg(); }

  void reset_post_boot_dmg();

  [[nodiscard]] u32 step();

  // Registers pairs
  [[nodiscard]] u16 af() const;
  [[nodiscard]] u16 bc() const;
  [[nodiscard]] u16 de() const;
  [[nodiscard]] u16 hl() const;

  void set_af(u16 value);
  void set_bc(u16 value);
  void set_de(u16 value);
  void set_hl(u16 value);

  // flags
  [[nodiscard]] bool get_flag(Flag flag) const;
  void set_flag(Flag flag, bool value);

  [[nodiscard]] const Register &registers() const { return m_registers; }

  void set_pc(u16 value) { m_registers.pc = value; }

private:
  void xor_a(u8 value);
  [[nodiscard]] u8 fetch8();
  [[nodiscard]] u16 fetch16();

  Bus &m_bus;
  Register m_registers{};

  bool m_ime{};
  bool m_halted{};
};

void Cpu::reset_post_boot_dmg() {
  set_af(0x01B0);
  set_bc(0x0013);
  set_de(0x00D8);
  set_hl(0x014D);

  m_registers.sp = 0xFFFE;
  m_registers.pc = 0x0100;

  m_ime = false;
  m_halted = false;
}

u16 Cpu::af() const {
  return static_cast<u16>((static_cast<u16>(m_registers.a) << 8) |
                          m_registers.f);
}

u16 Cpu::bc() const {
  return static_cast<u16>((static_cast<u16>(m_registers.b) << 8) |
                          m_registers.c);
}

u16 Cpu::de() const {
  return static_cast<u16>((static_cast<u16>(m_registers.d) << 8) |
                          m_registers.e);
}

u16 Cpu::hl() const {
  return static_cast<u16>((static_cast<u16>(m_registers.h) << 8) |
                          m_registers.l);
}

void Cpu::set_af(u16 value) {
  m_registers.a = static_cast<u8>(value >> 8);
  m_registers.f =
      static_cast<u8>(value & 0x00F0); // lower 4 bits must always be zero
}

void Cpu::set_bc(u16 value) {
  m_registers.b = static_cast<u8>(value >> 8);
  m_registers.c = static_cast<u8>(value & 0x00FF);
}

void Cpu::set_de(u16 value) {
  m_registers.d = static_cast<u8>(value >> 8);
  m_registers.e = static_cast<u8>(value & 0x00FF);
}

void Cpu::set_hl(u16 value) {
  m_registers.h = static_cast<u8>(value >> 8);
  m_registers.l = static_cast<u8>(value & 0x00FF);
}

bool Cpu::get_flag(Flag flag) const {
  const u8 mask = static_cast<u8>(flag);
  return (m_registers.f & mask) != 0;
}

void Cpu::set_flag(Flag flag, bool value) {
  const u8 mask = static_cast<u8>(flag);

  if (value) {
    m_registers.f = static_cast<u8>(m_registers.f | mask);
  } else {
    m_registers.f = static_cast<u8>(m_registers.f & static_cast<u8>(~mask));
  }

  m_registers.f = static_cast<u8>(m_registers.f & 0xF0);
}

void Cpu::xor_a(u8 value) {
  m_registers.a = static_cast<u8>(m_registers.a ^ value);

  // xor clears N, H and C flags
  // and sets Z only when result is zero
  m_registers.f = 0x00;

  if (m_registers.a == 0) {
    m_registers.f = static_cast<u8>(Flag::Z);
  }
}

u8 Cpu::fetch8() {
  const u8 value = m_bus.read(m_registers.pc);
  m_registers.pc = static_cast<u16>(m_registers.pc + 1);
  return value;
}

u16 Cpu::fetch16() {
  const u8 low = fetch8();
  const u8 high = fetch8();

  return static_cast<u16>((static_cast<u16>(high) << 8) | low);
}

u32 Cpu::step() {
  if (m_halted) {
    return 4; // temp behaviour
  }

  const u16 instruction_address = m_registers.pc;
  const u8 opcode = fetch8();

  switch (opcode) {
  // NOP
  case 0x00:
    return 4;

  // JP a16
  // am i supposed to switch case ALL these opcodes????
  case 0xC3: {
    const u16 destination = fetch16();
    m_registers.pc = destination;
    return 16;
  }

  // XOR A,A
  case 0xAF: {
    xor_a(m_registers.a);
    return 4;
  }

  default: {
    std::ostringstream message;

    message << std::uppercase << std::hex << std::setfill('0')
            << "Unimplemented opcode 0x" << std::setw(2)
            << static_cast<unsigned>(opcode) << " at PC=0x" << std::setw(4)
            << static_cast<unsigned>(instruction_address) << " AF=0x"
            << std::setw(4) << static_cast<unsigned>(af()) << " BC=0x"
            << std::setw(4) << static_cast<unsigned>(bc()) << " DE=0x"
            << std::setw(4) << static_cast<unsigned>(de()) << " HL=0x"
            << std::setw(4) << static_cast<unsigned>(hl()) << " SP=0x"
            << std::setw(4) << static_cast<unsigned>(m_registers.sp);

    throw std::runtime_error{message.str()};
  }
  }
}

void run_cpu_smoke_test(const std::vector<u8> &rom,
                        std::size_t maximum_instructions) {
  Bus bus{rom};
  Cpu cpu{bus};

  std::uint64_t total_cycles{};

  for (std::size_t instruction = 0; instruction < maximum_instructions;
       ++instruction) {
    const u16 old_pc = cpu.registers().pc;
    const u8 opcode = bus.read(old_pc);

    std::cout << '[' << std::dec << instruction << "] " << std::uppercase
              << std::hex << std::setfill('0') << "PC=0x" << std::setw(4)
              << static_cast<unsigned>(old_pc) << " OP=0x" << std::setw(2)
              << static_cast<unsigned>(opcode) << std::dec << std::endl;

    const u32 cycles = cpu.step();
    total_cycles += cycles;

    std::cout << "    new PC=0x" << std::uppercase << std::hex
              << std::setfill('0') << std::setw(4)
              << static_cast<unsigned>(cpu.registers().pc) << std::dec
              << " cycles=" << cycles << " total=" << total_cycles << '\n';
  }

  std::cout << "Stopped after instruction limit\n";
}

void run_cpu_tests(const std::vector<u8> &rom) {
  Bus bus{rom};
  Cpu cpu{bus};

  run_cpu_smoke_test(rom, 10);

  // Post-boot state
  assert(cpu.af() == 0x01B0);
  assert(cpu.bc() == 0x0013);
  assert(cpu.de() == 0x00D8);
  assert(cpu.hl() == 0x014D);
  assert(cpu.registers().sp == 0xFFFE);
  assert(cpu.registers().pc == 0x0100);

  // AF masking
  cpu.set_af(0x12FF);

  assert(cpu.registers().a == 0x12);
  assert(cpu.registers().f == 0xF0);
  assert(cpu.af() == 0x12F0);

  // Other register pairs
  cpu.set_bc(0x1234);
  assert(cpu.registers().b == 0x12);
  assert(cpu.registers().c == 0x34);
  assert(cpu.bc() == 0x1234);

  cpu.set_de(0x5678);
  assert(cpu.registers().d == 0x56);
  assert(cpu.registers().e == 0x78);
  assert(cpu.de() == 0x5678);

  cpu.set_hl(0x9ABC);
  assert(cpu.registers().h == 0x9A);
  assert(cpu.registers().l == 0xBC);
  assert(cpu.hl() == 0x9ABC);

  // Flag helpers
  cpu.set_af(0x0000);

  cpu.set_flag(Flag::Z, true);
  assert(cpu.get_flag(Flag::Z));
  assert(cpu.registers().f == 0x80);

  cpu.set_flag(Flag::C, true);
  assert(cpu.get_flag(Flag::C));
  assert(cpu.registers().f == 0x90);

  cpu.set_flag(Flag::Z, false);
  assert(!cpu.get_flag(Flag::Z));
  assert(cpu.get_flag(Flag::C));
  assert(cpu.registers().f == 0x10);

  // The lower nibble of F remains zero
  assert((cpu.registers().f & 0x0F) == 0);

  // Reset before instruction tests
  cpu.reset_post_boot_dmg();

  // NOP test
  bus.write(0xC000, 0x00);
  cpu.set_pc(0xC000);

  const u16 af_before = cpu.af();
  const u16 bc_before = cpu.bc();
  const u16 de_before = cpu.de();
  const u16 hl_before = cpu.hl();
  const u16 sp_before = cpu.registers().sp;

  const u32 nop_cycles = cpu.step();

  assert(nop_cycles == 4);
  assert(cpu.registers().pc == 0xC001);
  assert(cpu.af() == af_before);
  assert(cpu.bc() == bc_before);
  assert(cpu.de() == de_before);
  assert(cpu.hl() == hl_before);
  assert(cpu.registers().sp == sp_before);

  // JP a16 and little-endian fetch test
  bus.write(0xC000, 0xC3);
  bus.write(0xC001, 0x34);
  bus.write(0xC002, 0x12);

  cpu.set_pc(0xC000);

  const u32 jp_cycles = cpu.step();

  assert(jp_cycles == 16);
  assert(cpu.registers().pc == 0x1234);

  // XOR A,A
  bus.write(0xC000, 0xAF);

  cpu.set_af(0x5AF0);
  cpu.set_pc(0xC000);

  const u32 xor_cycles = cpu.step();

  assert(xor_cycles == 4);
  assert(cpu.registers().pc == 0xC001);
  assert(cpu.registers().a == 0x00);
  assert(cpu.registers().f == 0x80);

  assert(cpu.get_flag(Flag::Z));
  assert(!cpu.get_flag(Flag::N));
  assert(!cpu.get_flag(Flag::H));
  assert(!cpu.get_flag(Flag::C));

  std::cout << "CPU tests passed\n";
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
    run_cpu_tests(rom);

    std::cout << "Bus tests passed\n";

  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}