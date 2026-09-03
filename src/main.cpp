#include "Bus.hpp"
#include "Cpu.hpp"
#include "rom.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

void run_emulator(const std::vector<u8> &rom) {
  Bus bus{rom};
  Cpu cpu{bus};

  std::array<bool, 256> seen_opcodes{};
  std::uint64_t instruction = 0;
  const std::uint64_t limit = 100000000;

  while (instruction < limit) {
    const u16 old_pc = cpu.registers().pc;
    const u8 opcode = bus.read(old_pc);

    if (!seen_opcodes[opcode]) {
      seen_opcodes[opcode] = true;
      std::cerr << "new opcode: 0x" << std::hex << std::setw(2)
                << std::setfill('0') << (int)opcode << " at PC=0x"
                << std::setw(4) << (int)old_pc << '\n';
    }

    const u32 cycles = cpu.step();
    bus.tick(cycles);
    instruction++;

    if (cpu.registers().pc == old_pc)
      break;
  }
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: gbemu <path-to-rom.gb>\n";
      return 1;
    }

    const std::vector<u8> rom = load_rom(argv[1]);

    if (rom.size() >= 2 && rom.at(0) == 0x50 && rom.at(1) == 0x4B)
      throw std::runtime_error{"ZIP archive detected, extract .gb first"};

    if (rom.size() < 0x150)
      throw std::runtime_error{"ROM too small to contain header"};

    run_emulator(rom);

  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}