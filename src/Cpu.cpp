#include "Cpu.hpp"
#include <iomanip>
#include <sstream>

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
