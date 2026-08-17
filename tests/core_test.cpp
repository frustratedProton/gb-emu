#include "../src/Bus.hpp"
#include "../src/Cpu.hpp"
#include "../src/rom.hpp"
#include "../src/types.hpp"

#include <cassert>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

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

void run_cpu_tests(const std::vector<u8> &rom) {
  Bus bus{rom};
  Cpu cpu{bus};

  //   run_cpu_smoke_test(rom, 10);

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
}

std::vector<u8> create_test_rom() {
  std::vector<u8> rom(0x8000, 0x00);

  // Distinct values make ROM read tests more meaningful.
  rom.at(0x0000) = 0x31;
  rom.at(0x0134) = 'T';
  rom.at(0x7FFF) = 0x42;

  return rom;
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: gbemu_tests <path-to-rom.gb>\n";
      return 1;
    }

    const std::vector<u8> rom = load_rom(argv[1]);

    if (rom.size() >= 2 && rom.at(0) == 0x50 && rom.at(1) == 0x4B) {
      throw std::runtime_error{"The provided file is a ZIP archive. "
                               "Extract the .gb file first."};
    }

    if (rom.size() < 0x8000) {
      throw std::runtime_error{"Test ROM must contain at least 32 KiB"};
    }

    std::cout << "Testing ROM: " << argv[1] << '\n';

    run_bus_tests(rom);
    std::cout << "Bus tests passed\n";

    run_cpu_tests(rom);
    std::cout << "CPU tests passed\n";

    std::cout << "All core tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << "Test error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}