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

  cpu.set_bc(0x0100);
  cpu.set_af(0x0000);
  cpu.set_pc(0xC000);

  const u32 inc_b_cycles = cpu.step();

  assert(inc_b_cycles == 4);
  assert(cpu.registers().b == 0x02);
  assert(cpu.registers().c == 0x00);
  assert(cpu.registers().f == 0x00);
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
  assert(cpu.registers().pc == 0xC001);
  assert(cpu.registers().a == 0x02);
  assert(cpu.registers().f == 0x00);

  // LD B,C
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x41);

  cpu.set_bc(0x1234);
  cpu.set_af(0x78F0);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().b == 0x34);
  assert(cpu.registers().c == 0x34);
  assert(cpu.registers().pc == 0xC001);
  assert(cpu.registers().a == 0x78);
  assert(cpu.registers().f == 0xF0);

  // LD L,A
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x6F);

  cpu.set_hl(0x1200);
  cpu.set_af(0x56F0);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().l == 0x56);
  assert(cpu.registers().a == 0x56);
  assert(cpu.registers().f == 0xF0);
  assert(cpu.registers().pc == 0xC001);

  // LD A,(HL)
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x7E);

  cpu.set_hl(0xC100);
  bus.write(0xC100, 0x42);

  cpu.set_af(0x00F0);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.registers().a == 0x42);
  assert(cpu.registers().f == 0xF0);
  assert(cpu.registers().pc == 0xC001);

  // LD (HL),A
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x77);

  cpu.set_hl(0xC100);
  cpu.set_af(0x42F0);
  cpu.set_pc(0xC000);

  bus.write(0xC100, 0x00);

  assert(cpu.step() == 8);
  assert(bus.read(0xC100) == 0x42);
  assert(cpu.registers().f == 0xF0);
  assert(cpu.registers().pc == 0xC001);

  // 0x76 = HALT
  // FOR LATER, havent implemented halted yet
  //   cpu.reset_post_boot_dmg();

  //   bus.write(0xC000, 0x76);

  //   cpu.set_hl(0xC100);
  //   bus.write(0xC100, 0x42);
  //   cpu.set_pc(0xC000);

  //   const u32 halt_cycles = cpu.step();

  //   assert(halt_cycles == 4);
  //   assert(cpu.registers().pc == 0xC001);

  //   assert(bus.read(0xC100) == 0x42);

  // LD B, B
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x40);

  cpu.set_bc(0x1200);
  cpu.set_af(0x00F0);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().b == 0x12);
  assert(cpu.registers().f == 0xF0);
  assert(cpu.registers().pc == 0xC001);

  // LD A,A
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x7F);

  cpu.set_af(0x42F0);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x42);
  assert(cpu.registers().f == 0xF0);
  assert(cpu.registers().pc == 0xC001);

  // INC BC
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x03);

  cpu.set_bc(0xFFFF);
  cpu.set_af(0x5AF0);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.bc() == 0x0000);
  assert(cpu.af() == 0x5AF0);
  assert(cpu.registers().pc == 0xC001);

  // DEC SP (wrap-around)
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0x3B);

  cpu.set_sp(0x0000);
  cpu.set_af(0x5AF0);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.registers().sp == 0xFFFF);
  assert(cpu.af() == 0x5AF0);
  assert(cpu.registers().pc == 0xC001);

  // XOR A,B
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0xA8);

  cpu.set_af(0xF0F0);
  cpu.set_bc(0x0F00);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0xFF);
  assert(cpu.registers().b == 0x0F);
  assert(cpu.registers().f == 0x00);
  assert(cpu.registers().pc == 0xC001);

  // XOR A,(HL)
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0xAE);
  bus.write(0xC100, 0x55);

  cpu.set_hl(0xC100);
  cpu.set_af(0x55F0);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.registers().a == 0x00);
  assert(cpu.registers().f == 0x80);
  assert(cpu.hl() == 0xC100);
  assert(cpu.registers().pc == 0xC001);

  // ADD A,0x01: FF + 01 = 00
  cpu.reset_post_boot_dmg();

  bus.write(0xC000, 0xC6);
  bus.write(0xC001, 0x01);

  cpu.set_af(0xFF00);
  cpu.set_pc(0xC000);

  const u32 cycles = cpu.step();

  assert(cycles == 8);
  assert(cpu.registers().pc == 0xC002);
  assert(cpu.registers().a == 0x00);

  // Z + H + C; N is clear.
  assert(cpu.registers().f == 0xB0);

  // FF02 bus test
  bus.write(0xFF01, static_cast<u8>('A'));
  bus.write(0xFF02, 0x81);

  assert(bus.read(0xFF01) == static_cast<u8>('A'));
  assert((bus.read(0xFF02) & 0x80) == 0);

  // LD A,(HL+)
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x2A);
  bus.write(0xC100, 0x5A);
  bus.write(0xC101, 0xA5);
  cpu.set_hl(0xC100);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.registers().a == 0x5A);
  assert(cpu.hl() == 0xC101);
  assert(bus.read(0xC100) == 0x5A);

  // LD (HL+),A — store at [HL], then HL++
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x22);
  bus.write(0xC100, 0x00);
  bus.write(0xC101, 0x00);
  cpu.set_af(0x5AB0);
  cpu.set_hl(0xC100);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(bus.read(0xC100) == 0x5A); // stored at the pre-increment address
  assert(bus.read(0xC101) == 0x00); // not the next one
  assert(cpu.hl() == 0xC101);
  assert(cpu.registers().a == 0x5A);
  assert(cpu.registers().f == 0xB0);

  // LD (HL-),A
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x32);
  bus.write(0xC0FF, 0x00);
  cpu.set_af(0x5AB0);
  cpu.set_hl(0xC100);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(bus.read(0xC100) == 0x5A); // stored at the pre-decrement address
  assert(bus.read(0xC0FF) == 0x00);
  assert(cpu.hl() == 0xC0FF);
  assert(cpu.registers().a == 0x5A);

  // LD A,(HL-)
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x3A);
  bus.write(0xC100, 0x5A);
  cpu.set_hl(0xC100);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.registers().a == 0x5A);
  assert(cpu.hl() == 0xC0FF);

  // LD A,(BC) / LD (BC),A — pins bit 3 as the direction bit
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x0A);
  bus.write(0xC100, 0x5A);
  cpu.set_bc(0xC100);
  cpu.set_hl(0x9999);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.registers().a == 0x5A);
  assert(cpu.hl() == 0x9999);

  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x02);
  bus.write(0xC100, 0x00);
  cpu.set_af(0x5AB0);
  cpu.set_bc(0xC100);
  cpu.set_hl(0x9999);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(bus.read(0xC100) == 0x5A);
  assert(cpu.hl() == 0x9999);

  // LD (a16),A
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xEA);
  bus.write(0xC001, 0x00); // addr low  = 0xC500
  bus.write(0xC002, 0xC5); // addr high
  bus.write(0xC500, 0x00);
  cpu.set_af(0x5AB0);
  cpu.set_hl(0x9999);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0xC003); // 3 bytes consumed
  assert(bus.read(0xC500) == 0x5A);
  assert(cpu.hl() == 0x9999); // HL untouched
  assert(cpu.registers().f == 0xB0);

  // LD A,(a16)
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xFA);
  bus.write(0xC001, 0x00);
  bus.write(0xC002, 0xC5);
  bus.write(0xC500, 0xA5);
  cpu.set_hl(0x9999);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().a == 0xA5);
  assert(cpu.hl() == 0x9999);

  // CALL a16
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xCD);
  bus.write(0xC001, 0x34); // target = 0xC534
  bus.write(0xC002, 0xC5);
  cpu.set_sp(0xDFFF);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 24);
  assert(cpu.registers().pc == 0xC534);
  assert(cpu.registers().sp == 0xDFFD); // two bytes pushed
  assert(bus.read(0xDFFE) == 0xC0); // return addr high, at the higher address
  assert(bus.read(0xDFFD) == 0x03); // return addr low
  assert(cpu.af() == 0x01B0);       // CALL touches no flags

  // JR r8 forward
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x18);
  bus.write(0xC001, 0x10); // +16
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.registers().pc == 0xC012); // 0xC002 + 0x10

  // JR r8 backward
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x18);
  bus.write(0xC001, 0xFE); // -2
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.registers().pc == 0xC000); // 0xC002 - 2, a tight loop

  // JR r8 must branch even with Z set
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x18);
  bus.write(0xC001, 0x05);
  cpu.set_af(0xFF80); // Z set
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.registers().pc == 0xC007);

  // JR Z,r8 not taken
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x28);
  bus.write(0xC001, 0x05);
  cpu.set_af(0x0000); // Z clear
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.registers().pc == 0xC002);

  // RET
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xC9);

  bus.write(0xDFFD, 0x12); // low byte
  bus.write(0xDFFE, 0xC0); // high byte

  cpu.set_sp(0xDFFD);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0xC012);
  assert(cpu.registers().sp == 0xDFFF);

  // CALL then RET round-trips
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xCD);
  bus.write(0xC001, 0x00);
  bus.write(0xC002, 0xC1); // CALL 0xC100
  bus.write(0xC100, 0xC9); // RET
  cpu.set_sp(0xDFFF);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 24);
  assert(cpu.registers().pc == 0xC100);
  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0xC003); // back to the instruction after CALL
  assert(cpu.registers().sp == 0xDFFF); // stack balanced

  // RET NZ not taken
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xC0);
  bus.write(0xDFFE, 0x12);
  bus.write(0xDFFD, 0xC0);
  cpu.set_sp(0xDFFD);
  cpu.set_af(0x0080);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.registers().pc == 0xC001); // falls through
  assert(cpu.registers().sp == 0xDFFD); // nothing popped

  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xCD);
  bus.write(0xC001, 0x00);
  bus.write(0xC002, 0xC1); // CALL 0xC100 from 0xC000
  cpu.set_sp(0xDFFF);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 24);
  assert(bus.read(0xDFFD) == 0x03); // low byte at the lower address
  assert(bus.read(0xDFFE) == 0xC0);

  // PUSH HL
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0x1234);
  cpu.set_sp(0xDFFF);
  bus.write(0xC000, 0xE5); // PUSH HL
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().sp == 0xDFFD); // sp decremented by 2
  assert(bus.read(0xDFFE) == 0x12);     // H (high byte) at SP+1
  assert(bus.read(0xDFFD) == 0x34);     // L (low byte)  at SP

  // PUSH then POP round trip
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0xBEEF);
  cpu.set_bc(0x0000);
  cpu.set_sp(0xDFFF);

  bus.write(0xC000, 0xE5); // PUSH HL
  bus.write(0xC001, 0xC1); // POP BC

  cpu.set_pc(0xC000);
  static_cast<void>(cpu.step()); // PUSH HL
  static_cast<void>(cpu.step()); // POP BC

  assert(cpu.bc() == 0xBEEF);           // BC should now equal what HL was
  assert(cpu.registers().sp == 0xDFFF); // stack balanced

  // PUSH HL writes correct bytes to stack
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0x1234);
  cpu.set_sp(0xDFFF);
  bus.write(0xC000, 0xE5);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().sp == 0xDFFD);
  assert(bus.read(0xDFFE) == 0x12); // H at SP+1
  assert(bus.read(0xDFFD) == 0x34); // L at SP

  // PUSH BC
  cpu.reset_post_boot_dmg();
  cpu.set_bc(0xABCD);
  cpu.set_sp(0xDFFF);
  bus.write(0xC000, 0xC5);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(bus.read(0xDFFE) == 0xAB);
  assert(bus.read(0xDFFD) == 0xCD);
  assert(cpu.registers().sp == 0xDFFD);

  // PUSH DE
  cpu.reset_post_boot_dmg();
  cpu.set_de(0x1234);
  cpu.set_sp(0xDFFF);
  bus.write(0xC000, 0xD5);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(bus.read(0xDFFE) == 0x12);
  assert(bus.read(0xDFFD) == 0x34);
  assert(cpu.registers().sp == 0xDFFD);

  // PUSH AF - F low nibble must stay zero
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x12F0);
  cpu.set_sp(0xDFFF);
  bus.write(0xC000, 0xF5);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(bus.read(0xDFFE) == 0x12);
  assert(bus.read(0xDFFD) == 0xF0);
  assert(cpu.registers().sp == 0xDFFD);

  // POP BC
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xDFFD);
  bus.write(0xDFFD, 0xCD); // lo
  bus.write(0xDFFE, 0xAB); // hi
  bus.write(0xC000, 0xC1);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.bc() == 0xABCD);
  assert(cpu.registers().sp == 0xDFFF);

  // POP AF masks lower nibble
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xDFFD);
  bus.write(0xDFFD, 0xFF); // lower nibble should be masked
  bus.write(0xDFFE, 0x12); // hi
  bus.write(0xC000, 0xF1);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.registers().a == 0x12);
  assert(cpu.registers().f == 0xF0); // 0xFF masked to 0xF0
  assert(cpu.registers().sp == 0xDFFF);

  // PUSH HL then POP BC round trip
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0xBEEF);
  cpu.set_bc(0x0000);
  cpu.set_sp(0xDFFF);
  bus.write(0xC000, 0xE5); // PUSH HL
  bus.write(0xC001, 0xC1); // POP BC
  cpu.set_pc(0xC000);

  static_cast<void>(cpu.step()); // PUSH HL, discard cycles
  static_cast<void>(cpu.step()); // POP BC,  discard cycles

  assert(cpu.bc() == 0xBEEF);
  assert(cpu.registers().sp == 0xDFFF);

  // RLCA - bit 7 goes to carry and wraps to bit 0
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x8500); // A = 0x85 = 1000 0101
  bus.write(0xC000, 0x07);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x0B);
  assert(cpu.get_flag(Flag::C));  // old bit7 was 1
  assert(!cpu.get_flag(Flag::Z)); // always cleared
  assert(!cpu.get_flag(Flag::N));
  assert(!cpu.get_flag(Flag::H));

  // RRCA - bit 0 goes to carry and wraps to bit 7
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x8500); // A = 0x85 = 1000 0101
  bus.write(0xC000, 0x0F);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0xC2);
  assert(cpu.get_flag(Flag::C)); // old bit0 was 1
  assert(!cpu.get_flag(Flag::Z));
  assert(!cpu.get_flag(Flag::N));
  assert(!cpu.get_flag(Flag::H));

  // RLA - rotate left through carry
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x8500); // A = 0x85, C = 0
  bus.write(0xC000, 0x17);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x0A);
  assert(cpu.get_flag(Flag::C));
  assert(!cpu.get_flag(Flag::Z));
  assert(!cpu.get_flag(Flag::N));
  assert(!cpu.get_flag(Flag::H));

  // RLA with carry set going in
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x8510); // A = 0x85, C = 1
  bus.write(0xC000, 0x17);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x0B);
  assert(cpu.get_flag(Flag::C));

  // RRA - rotate right through carry
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x8500); // A = 0x85, C = 0
  bus.write(0xC000, 0x1F);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x42);
  assert(cpu.get_flag(Flag::C));
  assert(!cpu.get_flag(Flag::Z));
  assert(!cpu.get_flag(Flag::N));
  assert(!cpu.get_flag(Flag::H));

  // RRA with carry set going in
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x8510);
  bus.write(0xC000, 0x1F);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0xC2);
  assert(cpu.get_flag(Flag::C));

  // RLCA does not set Z even when result is zero
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x0000);
  bus.write(0xC000, 0x07);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x00);
  assert(!cpu.get_flag(Flag::Z));
  assert(!cpu.get_flag(Flag::C));

  // JP HL
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0xC100);
  bus.write(0xC000, 0xE9);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().pc == 0xC100); // jumped to HL
  assert(cpu.hl() == 0xC100);           // HL unchanged

  // JP cc
  // JP NZ taken
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xC2);
  bus.write(0xC001, 0x00);
  bus.write(0xC002, 0xC1); // target = 0xC100
  cpu.set_af(0x0000);      // Z clear -> NZ taken
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0xC100);

  // JP NZ not taken
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xC2);
  bus.write(0xC001, 0x00);
  bus.write(0xC002, 0xC1);
  cpu.set_af(0x0080); // Z set -> NZ not taken
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.registers().pc == 0xC003); // fell through

  // JP Z taken
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xCA);
  bus.write(0xC001, 0x00);
  bus.write(0xC002, 0xC1);
  cpu.set_af(0x0080); // Z set -> Z taken
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0xC100);

  // JP NC taken
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xD2);
  bus.write(0xC001, 0x00);
  bus.write(0xC002, 0xC1);
  cpu.set_af(0x0000); // C clear -> NC taken
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0xC100);

  // JP C taken
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xDA);
  bus.write(0xC001, 0x00);
  bus.write(0xC002, 0xC1);
  cpu.set_af(0x0010); // C set -> C taken
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0xC100);

  // JP C not taken
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0xDA);
  bus.write(0xC001, 0x00);
  bus.write(0xC002, 0xC1);
  cpu.set_af(0x0000); // C clear -> C not taken
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.registers().pc == 0xC003);

  // ADD HL, rr
  // ADD HL, BC basic
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0x1000);
  cpu.set_bc(0x0234);
  bus.write(0xC000, 0x09);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.hl() == 0x1234);
  assert(!cpu.get_flag(Flag::N));
  assert(!cpu.get_flag(Flag::H));
  assert(!cpu.get_flag(Flag::C));

  // ADD HL, HL
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0x1000);
  bus.write(0xC000, 0x29);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.hl() == 0x2000);
  assert(!cpu.get_flag(Flag::N));
  assert(!cpu.get_flag(Flag::C));

  // ADD HL, rr carry
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0xFFFF);
  cpu.set_bc(0x0001);
  bus.write(0xC000, 0x09);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.hl() == 0x0000);
  assert(cpu.get_flag(Flag::C));
  assert(!cpu.get_flag(Flag::N));

  // ADD HL, rr half carry (bit 11 overflow)
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0x0FFF);
  cpu.set_bc(0x0001);
  bus.write(0xC000, 0x09);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.hl() == 0x1000);
  assert(cpu.get_flag(Flag::H));
  assert(!cpu.get_flag(Flag::C));

  // ADD HL, rr does not modify Z flag
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0x0000);
  cpu.set_bc(0x0000);
  cpu.set_af(0x0080); // Z set before
  bus.write(0xC000, 0x09);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.hl() == 0x0000);
  assert(cpu.get_flag(Flag::Z)); // Z unchanged

  // ADD HL, SP
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0x1000);
  cpu.set_sp(0x0234);
  bus.write(0xC000, 0x39);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.hl() == 0x1234);

  // DAA after ADD - no adjustment needed
  // 0x05 + 0x03 = 0x08, valid BCD already
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x27);
  cpu.set_af(0x0800); // A = 0x08, no flags set
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x08);
  assert(!cpu.get_flag(Flag::C));
  assert(!cpu.get_flag(Flag::H));

  // DAA after ADD - lower nibble fix
  // 0x08 + 0x04 = 0x0C, lower nibble > 9, add 0x06
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x27);
  cpu.set_af(0x0C00); // A = 0x0C, N clear (was add)
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x12); // 0x0C + 0x06 = 0x12, which is BCD 12

  // DAA after ADD - carry needed
  // 0x99 + 0x01 in BCD
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x27);
  cpu.set_af(0x9A00); // A = 0x9A, N clear
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x00); // 0x9A + 0x66 = 0x100, wraps to 0x00
  assert(cpu.get_flag(Flag::C));
  assert(cpu.get_flag(Flag::Z));

  // DAA after SUB - H flag set
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x27);
  cpu.set_af(0x0040 | 0x20); // A = 0x00, N set, H set
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0xFA); // 0x00 - 0x06 = 0xFA
  assert(!cpu.get_flag(Flag::H));

  // DAA sets Z when result is zero
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x27);
  cpu.set_af(0x0000); // A = 0x00, no flags
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x00);
  assert(cpu.get_flag(Flag::Z));

  // DAA clears H always
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x27);
  cpu.set_af(0x0020); // H set going in
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(!cpu.get_flag(Flag::H)); // always cleared after DAA

  // LD HL, SP+i8 positive offset
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xFFF0);
  bus.write(0xC000, 0xF8);
  bus.write(0xC001, 0x04); // offset = +4
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.hl() == 0xFFF4);
  assert(!cpu.get_flag(Flag::Z));
  assert(!cpu.get_flag(Flag::N));

  // LD HL, SP+i8 negative offset
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xFF00);
  bus.write(0xC000, 0xF8);
  bus.write(0xC001, 0xFC); // offset = -4 (0xFC as signed = -4)
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.hl() == 0xFEFC);
  assert(!cpu.get_flag(Flag::Z));
  assert(!cpu.get_flag(Flag::N));

  // LD HL, SP+i8 half carry
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xFF0F);
  bus.write(0xC000, 0xF8);
  bus.write(0xC001, 0x01); // offset = +1, low nibble 0xF + 0x1 overflows
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.hl() == 0xFF10);
  assert(cpu.get_flag(Flag::H));
  assert(!cpu.get_flag(Flag::C));

  // LD HL, SP+i8 carry
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xFFFF);
  bus.write(0xC000, 0xF8);
  bus.write(0xC001, 0x01); // offset = +1, low byte 0xFF + 0x01 overflows
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.hl() == 0x0000);
  assert(cpu.get_flag(Flag::C));
  assert(cpu.get_flag(Flag::H));

  // SP unchanged after LD HL, SP+i8
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xFF00);
  bus.write(0xC000, 0xF8);
  bus.write(0xC001, 0x10);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 12);
  assert(cpu.registers().sp == 0xFF00); // SP not modified

  // LD SP, HL
  cpu.reset_post_boot_dmg();
  cpu.set_hl(0x1234);
  bus.write(0xC000, 0xF9);
  cpu.set_pc(0xC000);

  const u16 af_before_2 = cpu.af();

  assert(cpu.step() == 8);
  assert(cpu.registers().sp == 0x1234); // SP = HL
  assert(cpu.hl() == 0x1234);           // HL unchanged
  assert(cpu.af() == af_before_2);      // flags unchanged

  // STOP
  cpu.reset_post_boot_dmg();
  bus.write(0xC000, 0x10);
  bus.write(0xC001, 0x00); // second byte always 0x00
  cpu.set_pc(0xC000);

  const u16 af_before_3 = cpu.af();
  const u16 bc_before_3 = cpu.bc();

  assert(cpu.step() == 4);
  assert(cpu.registers().pc == 0xC002); // consumed both bytes
  assert(cpu.af() == af_before_3);      // flags unchanged
  assert(cpu.bc() == bc_before_3);      // registers unchanged

  // LD (u16), SP
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0x1234);
  bus.write(0xC000, 0x08);
  bus.write(0xC001, 0x00); // addr low
  bus.write(0xC002, 0xD0); // addr high -> writes to 0xD000
  cpu.set_pc(0xC000);

  const u16 sp_store_flags = cpu.af();

  assert(cpu.step() == 20);
  assert(bus.read(0xD000) == 0x34);     // SP low byte
  assert(bus.read(0xD001) == 0x12);     // SP high byte
  assert(cpu.registers().sp == 0x1234); // SP unchanged
  assert(cpu.af() == sp_store_flags);   // flags unchanged
  assert(cpu.registers().pc == 0xC003); // advanced past 3 byte instruction

  // RST 00h
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xDFFF);
  bus.write(0xC000, 0xC7);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0x0000);
  assert(cpu.registers().sp == 0xDFFD);
  assert(bus.read(0xDFFE) == 0xC0); // high byte of return address
  assert(bus.read(0xDFFD) == 0x01); // low byte of return address

  // RST 38h
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xDFFF);
  bus.write(0xC000, 0xFF);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0x0038);

  // RST 28h
  cpu.reset_post_boot_dmg();
  cpu.set_sp(0xDFFF);
  bus.write(0xC000, 0xEF);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 16);
  assert(cpu.registers().pc == 0x0028);

  // LD A, (FF00+C)
  cpu.reset_post_boot_dmg();
  bus.write(0xFF42, 0xAB); // put value at 0xFF00 + 0x42
  cpu.set_bc(0x0042);      // C = 0x42
  bus.write(0xC000, 0xF2);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(cpu.registers().a == 0xAB);
  assert(cpu.registers().c == 0x42); // C unchanged

  // LD (FF00+C), A
  cpu.reset_post_boot_dmg();
  cpu.set_af(0xCD00); // A = 0xCD
  cpu.set_bc(0x0042); // C = 0x42
  bus.write(0xC000, 0xE2);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 8);
  assert(bus.read(0xFF42) == 0xCD);
  assert(cpu.registers().a == 0xCD); // A unchanged
  assert(cpu.registers().c == 0x42); // C unchanged

  // CPL
  cpu.reset_post_boot_dmg();
  cpu.set_af(0xF0B0); // A = 0xF0
  bus.write(0xC000, 0x2F);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0x0F); // 0xF0 flipped = 0x0F
  assert(cpu.get_flag(Flag::N));     // always set
  assert(cpu.get_flag(Flag::H));     // always set
  assert(cpu.get_flag(Flag::Z));     // Z unmodified, was set before
  assert(cpu.get_flag(Flag::C));     // C unmodified, was set before

  // CPL all zeros
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x0000); // A = 0x00
  bus.write(0xC000, 0x2F);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.registers().a == 0xFF); // 0x00 flipped = 0xFF
  assert(cpu.get_flag(Flag::N));
  assert(cpu.get_flag(Flag::H));

  // SCF
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x0080); // Z set, C clear
  bus.write(0xC000, 0x37);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.get_flag(Flag::C));  // carry set
  assert(!cpu.get_flag(Flag::N)); // cleared
  assert(!cpu.get_flag(Flag::H)); // cleared
  assert(cpu.get_flag(Flag::Z));  // Z unmodified

  // CCF with carry clear
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x0000); // C clear
  bus.write(0xC000, 0x3F);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(cpu.get_flag(Flag::C)); // was clear, now set
  assert(!cpu.get_flag(Flag::N));
  assert(!cpu.get_flag(Flag::H));

  // CCF with carry set
  cpu.reset_post_boot_dmg();
  cpu.set_af(0x0010); // C set
  bus.write(0xC000, 0x3F);
  cpu.set_pc(0xC000);

  assert(cpu.step() == 4);
  assert(!cpu.get_flag(Flag::C)); // was set, now clear
}

std::vector<u8> create_test_rom() {
  std::vector<u8> rom(0x8000, 0x00);

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