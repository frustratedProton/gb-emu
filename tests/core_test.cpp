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

  cpu.reset_post_boot_dmg();

  // LD HL
  bus.write(0xC000, 0x21);
  bus.write(0xC001, 0x34);
  bus.write(0xC002, 0x12);

  cpu.set_hl(0x0000);
  cpu.set_af(0x00F0);
  cpu.set_pc(0xC000);

  const u32 ld_hl_cycle = cpu.step();

  assert(ld_hl_cycle == 12);
  assert(cpu.registers().pc == 0xC003);
  assert(cpu.hl() == 0x1234);
  assert(cpu.registers().f == 0xF0);

  // LD C
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x0E);
  bus.write(0xC001, 0x7B);

  cpu.set_bc(0xAA00);
  cpu.set_af(0x00F0);
  cpu.set_pc(0xC000);

  const u32 ld_c_cycles = cpu.step();

  assert(ld_c_cycles == 8);
  assert(cpu.registers().pc == 0xC002);
  assert(cpu.registers().b == 0xAA);
  assert(cpu.registers().c == 0x7B);
  assert(cpu.registers().f == 0xF0);

  // LD B
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x06);
  bus.write(0xC001, 0x42);

  cpu.set_bc(0x00CC);
  cpu.set_af(0x00F0);
  cpu.set_pc(0xC000);

  const u32 ld_b_cycles = cpu.step();

  assert(ld_b_cycles == 8);
  assert(cpu.registers().pc == 0xC002);
  assert(cpu.registers().b == 0x42);
  assert(cpu.registers().c == 0xCC); // C must be unchanged.
  assert(cpu.registers().f == 0xF0);

  // LD A
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x3E);
  bus.write(0xC001, 0x42);

  cpu.set_af(0x00F0);
  cpu.set_pc(0xC000);

  const u32 ld_a_cycles = cpu.step();

  assert(ld_a_cycles == 8);
  assert(cpu.registers().pc == 0xC002);
  assert(cpu.registers().a == 0x42);
  assert(cpu.registers().f == 0xF0);

  // LD (HL-)
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x32);

  cpu.set_hl(0xC100);
  cpu.set_af(0x5AB0);
  cpu.set_pc(0xC000);

  const u32 ld_hld_a_cycles = cpu.step();

  assert(ld_hld_a_cycles == 8);
  assert(cpu.registers().pc == 0xC001);
  assert(bus.read(0xC100) == 0x5A);
  assert(cpu.hl() == 0xC0FF);
  assert(cpu.registers().a == 0x5A);
  assert(cpu.registers().f == 0xB0);

  // DEC B
  // Z = 1, N = 1, H = 0, C remains 1
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x05);

  cpu.set_bc(0x0177);
  cpu.set_af(0x0010);
  cpu.set_pc(0xC000);

  const u32 dec_b_zero_cycles = cpu.step();

  assert(dec_b_zero_cycles == 4);
  assert(cpu.registers().pc == 0xC001);
  assert(cpu.registers().b == 0x00);
  assert(cpu.registers().c == 0x77);
  assert(cpu.registers().f == 0xD0); // Z + N + C

  // DEC B
  // Z=0, N = 1, H = 1, C remains 0
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x05);

  cpu.set_bc(0x1077);
  cpu.set_af(0x0000);
  cpu.set_pc(0xC000);

  const u32 dec_b_half_borrow_cycles = cpu.step();

  assert(dec_b_half_borrow_cycles == 4);
  assert(cpu.registers().b == 0x0F);
  assert(cpu.registers().c == 0x77);
  assert(cpu.registers().f == 0x60); // N + H

  // DEC C
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x0D);

  cpu.set_bc(0xAA01);
  cpu.set_af(0x0010);
  cpu.set_pc(0xC000);

  const u32 dec_c_cycles = cpu.step();

  assert(dec_c_cycles == 4);
  assert(cpu.registers().pc == 0xC001);
  assert(cpu.registers().b == 0xAA);
  assert(cpu.registers().c == 0x00);
  assert(cpu.registers().f == 0xD0);

  // JR NZ, +2 branch taken
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x20);
  bus.write(0xC001, 0x02);

  cpu.set_af(0x0010);
  cpu.set_pc(0xC000);

  const u32 jr_taken_cycles = cpu.step();

  assert(jr_taken_cycles == 12);
  assert(cpu.registers().pc == 0xC004);
  assert(cpu.registers().f == 0x10);

  // JR NZ, -2 -ve signed offset
  cpu.reset_post_boot_dmg();

  bus.write(0xC010, 0x20);
  bus.write(0xC011, 0xFE); // -2 as signed i8

  cpu.set_af(0x0000); // Z=0
  cpu.set_pc(0xC010);

  const u32 jr_negative_cycles = cpu.step();

  assert(jr_negative_cycles == 12);
  assert(cpu.registers().pc == 0xC010);
  assert(cpu.registers().f == 0x00);

  // JR NZ branch not takne, Z = 1
  cpu.reset_post_boot_dmg();

  bus.write(0xC020, 0x20);
  bus.write(0xC021, 0x7F);

  cpu.set_af(0x0080); // Z=1
  cpu.set_pc(0xC020);

  const u32 jr_not_taken_cycles = cpu.step();

  assert(jr_not_taken_cycles == 8);
  assert(cpu.registers().pc == 0xC022);
  assert(cpu.registers().f == 0x80);

  // LDH A
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0xF0);
  bus.write(0xC001, 0x43);
  bus.write(0xFF43, 0x7B);

  cpu.set_af(0x00B0);
  cpu.set_pc(0xC000);

  const u32 ldh_read_cycles = cpu.step();

  assert(ldh_read_cycles == 12);
  assert(cpu.registers().pc == 0xC002);
  assert(cpu.registers().a == 0x7B);
  assert(cpu.registers().f == 0xB0);

  // CP A, equal
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0xFE);
  bus.write(0xC001, 0x42);

  cpu.set_af(0x4200);
  cpu.set_pc(0xC000);

  const u32 cp_equal_cycles = cpu.step();

  assert(cp_equal_cycles == 8);
  assert(cpu.registers().pc == 0xC002);
  assert(cpu.registers().a == 0x42);
  assert(cpu.registers().f == 0xC0); // Z + N

  // CP A half borrow
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0xFE);
  bus.write(0xC001, 0x01);

  cpu.set_af(0x10F0);
  cpu.set_pc(0xC000);

  const u32 cp_half_borrow_cycles = cpu.step();

  assert(cp_half_borrow_cycles == 8);
  assert(cpu.registers().a == 0x10);
  assert(cpu.registers().f == 0x60); // N + H

  // CP A full borrow
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0xFE);
  bus.write(0xC001, 0x01);

  cpu.set_af(0x00F0);
  cpu.set_pc(0xC000);

  const u32 cp_borrow_cycles = cpu.step();

  assert(cp_borrow_cycles == 8);
  assert(cpu.registers().a == 0x00);
  assert(cpu.registers().f == 0x70); // N + H + C

  // LD r16
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x01);
  bus.write(0xC001, 0x78);
  bus.write(0xC002, 0x56);

  cpu.set_bc(0x0000);
  cpu.set_pc(0xC000);
  cpu.set_af(0x10F0);

  const u32 ld_bc_cycles = cpu.step();

  assert(ld_bc_cycles == 12);
  assert(cpu.registers().b == 0x56);
  assert(cpu.registers().c == 0x78);
  assert(cpu.registers().sp == 0xFFFE);
  assert(cpu.registers().pc == 0xC003);
  assert(cpu.registers().f == 0xF0);

  // LD SP, 0xABCD
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x31);
  bus.write(0xC001, 0xCD);
  bus.write(0xC002, 0xAB);

  cpu.set_sp(0x0000);
  cpu.set_af(0x10F0);
  cpu.set_pc(0xC000);

  const u32 ld_sp_cycles = cpu.step();

  assert(ld_sp_cycles == 12);
  assert(cpu.registers().sp == 0xABCD);
  assert(cpu.registers().pc == 0xC003);
  assert(cpu.registers().a == 0x10);
  assert(cpu.registers().f == 0xF0);

  // INC B/C/D/E/H/L/A
  // opcodes: 04 0C 14 1C 24 2C 34 3C
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x04);

  cpu.set_bc(0x1000);
  cpu.set_af(0x0000);
  cpu.set_pc(0xC000);

  const u32 inc_b_cycles = cpu.step();

  assert(inc_b_cycles == 4);
  assert(cpu.registers().b == 0x02);
  assert(cpu.registers().pc == 0xC001);

  // INC C
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x0C);

  cpu.set_bc(0x0001);
  cpu.set_af(0x0000);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().c == 0x02);

  // INC D
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x14);

  cpu.set_de(0x0100);
  cpu.set_af(0x0000);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().d == 0x02);

  // INC E
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x1C);

  cpu.set_de(0x0001);
  cpu.set_af(0x0000);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().e == 0x02);

  // INC H
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x24);

  cpu.set_hl(0x0100);
  cpu.set_af(0x0000);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().h == 0x02);

  // INC L
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x2C);

  cpu.set_hl(0x0001);
  cpu.set_af(0x0000);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().l == 0x02);

  // INC A
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x3C);

  cpu.set_af(0x0100);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x02);
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