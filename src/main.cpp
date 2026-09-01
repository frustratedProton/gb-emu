#include "Bus.hpp"
#include "Cpu.hpp"
#include "rom.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

// this is what i am using to implement opcodes for tetris (for now)
void run_cpu_trace(const std::vector<u8> &rom,
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
              << static_cast<unsigned>(opcode) << std::dec << '\n';

    std::cout << "    before: "
              << "AF=0x" << std::uppercase << std::hex << std::setw(4)
              << static_cast<unsigned>(cpu.af()) << " BC=0x" << std::setw(4)
              << static_cast<unsigned>(cpu.bc()) << " DE=0x" << std::setw(4)
              << static_cast<unsigned>(cpu.de()) << " HL=0x" << std::setw(4)
              << static_cast<unsigned>(cpu.hl()) << " SP=0x" << std::setw(4)
              << static_cast<unsigned>(cpu.registers().sp) << std::dec << '\n';

    const u32 cycles = cpu.step();
    total_cycles += cycles;

    std::cout << "    new PC=0x" << std::uppercase << std::hex
              << std::setfill('0') << std::setw(4)
              << static_cast<unsigned>(cpu.registers().pc) << std::dec
              << " cycles=" << cycles << " total=" << total_cycles << '\n';
  }

  std::cout << "Stopped after instruction limit\n";
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: gbemu <path-to-rom.gb>\n";
      return 1;
    }

    const std::vector<u8> rom = load_rom(argv[1]);

    // ZIP archives normally begin with the ASCII characters "PK".
    if (rom.size() >= 2 && rom.at(0) == 0x50 && rom.at(1) == 0x4B) {
      throw std::runtime_error{"The provided file is a ZIP archive. "
                               "Extract the .gb file first."};
    }

    if (rom.size() < 0x150) {
      throw std::runtime_error{
          "File is too small to contain a Game Boy cartridge header"};
    }

    std::cout << "Loaded " << rom.size() << " bytes\n";

    print_title(rom);

    std::cout << "Header checksum: "
              << (valid_header_checksum(rom) ? "OK" : "FAILED") << '\n';

    run_cpu_trace(rom, 10000000);
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}