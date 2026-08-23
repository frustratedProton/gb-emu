#include "Cpu.hpp"
#include "Bus.hpp"
#include "types.hpp"
#include <iomanip>
#include <sstream>
#include <stdexcept>

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

// 0 → B
// 1 → C
// 2 → D
// 3 → E
// 4 → H
// 5 → L
// 6 → memory at HL
// 7 → A
u8 Cpu::read_r8(u8 code) const {
  switch (code) {
  case 0:
    return m_registers.b;

  case 1:
    return m_registers.c;

  case 2:
    return m_registers.d;

  case 3:
    return m_registers.e;

  case 4:
    return m_registers.h;

  case 5:
    return m_registers.l;

  case 6:
    return m_bus.read(hl());

  case 7:
    return m_registers.a;

  default:
    throw std::logic_error{"Invalid 8-bit register code"};
  }
}

u16 Cpu::read_r16(u16 code) const {
  switch (code) {
  case 0:
    return bc();

  case 1:
    return de();

  case 2:
    return hl();

  case 3:
    return m_registers.sp;

  default:
    throw std::logic_error{"Invalid 16-bit register code"};
  }
}

void Cpu::write_r8(u8 code, u8 value) {
  switch (code) {
  case 0:
    m_registers.b = value;
    return;

  case 1:
    m_registers.c = value;
    return;

  case 2:
    m_registers.d = value;
    return;

  case 3:
    m_registers.e = value;
    return;

  case 4:
    m_registers.h = value;
    return;

  case 5:
    m_registers.l = value;
    return;

  case 6:
    m_bus.write(hl(), value);
    return;

  case 7:
    m_registers.a = value;
    return;

  default:
    throw std::logic_error{"Invalid 8-bit register code"};
  }
}

void Cpu::write_r16(u8 code, u16 value) {
  // 0 → BC
  // 1 → DE
  // 2 → HL
  // 3 → SP
  switch (code) {
  case 0:
    set_bc(value);
    return;

  case 1:
    set_de(value);
    return;

  case 2:
    set_hl(value);
    return;

  case 3:
    m_registers.sp = value;
    return;

  default:
    throw std::logic_error{"Invalid 16-bit register code"};
  }
}

u8 Cpu::dec8(u8 value) {
  const u8 res = static_cast<u8>(value - 1);

  set_flag(Flag::Z, res == 0);
  set_flag(Flag::N, true);
  set_flag(Flag::H, (value & 0x0F) == 0);

  return res;
}

u8 Cpu::inc8(u8 value) {
  const u8 res = static_cast<u8>(value + 1);
  set_flag(Flag::Z, res == 0);
  set_flag(Flag::N, false);
  set_flag(Flag::H, (value & 0x0F) == 0x0F);
  return res;
}

u32 Cpu::step() {
  if (m_halted) {
    return 4; // temp behaviour
  }

  const u16 instruction_address = m_registers.pc;
  const u8 opcode = fetch8();

  // LD r8, d8
  if ((opcode & 0xC7) == 0x06) {
    const u8 dest = static_cast<u8>((opcode >> 3) & 0x07);
    const u8 value = fetch8();

    write_r8(dest, value);

    // LD (HL),d8 requires a memory access and takes longer
    return dest == 6 ? 12 : 8;
  }

  // DEC r8
  if ((opcode & 0xC7) == 0x05) {
    const u8 dest = static_cast<u8>((opcode >> 3) & 0x07);

    const u8 value = read_r8(dest);
    const u8 result = dec8(value);

    write_r8(dest, result);

    // DEC (HL) requires a memory read and write
    return dest == 6 ? 12 : 4;
  }

  // LD r16, d16
  if ((opcode & 0xCF) == 0x01) {
    const u8 dest = static_cast<u8>((opcode >> 4) & 0x03);

    const u16 value = fetch16();

    write_r16(dest, value);
    return 12;
  }

  // INC r8
  if ((opcode & 0xC7) == 0x04) {
    const u8 dest = static_cast<u8>((opcode >> 3) & 0x07);

    const u8 value = read_r8(dest);
    const u8 result = inc8(value);

    write_r8(dest, result);

    return dest == 6 ? 12 : 4;
  }

  // LD r8, r8
  // 0x76 opcode is for halt
  if (opcode >= 0x40 && opcode <= 0x7F && opcode != 0x76) {
    const u8 dest = static_cast<u8>((opcode >> 3) & 0x07);
    const u8 src = static_cast<u8>(opcode & 0x07);

    const u8 value = read_r8(src);
    write_r8(dest, value);

    if (src == 6 || dest == 6)
      return 8;

    return 4;
  }

  // INC r16
  if ((opcode & 0xCF) == 0x03) {
    const u8 dest = static_cast<u8>((opcode >> 4) & 0x03);

    const u16 value = read_r16(dest);
    const u16 res = static_cast<u16>(value + 1);

    write_r16(dest, res);

    return 8;
  }

  // DEC r16
  if ((opcode & 0xCF) == 0x0B) {
    const u8 dest = static_cast<u8>((opcode >> 4) & 0x03);
    const u16 value = read_r16(dest);
    const u16 res = static_cast<u16>(value - 1);

    write_r16(dest, res);

    return 8;
  }

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

  // LD (HL-), A
  case 0x32: {
    const u16 addr = hl();
    m_bus.write(addr, m_registers.a);
    set_hl(addr - 1);
    return 8;
  }

  // JR NZ, i8
  case 0x20: {
    const i8 offset = static_cast<i8>(fetch8());

    if (!get_flag(Flag::Z)) {
      m_registers.pc += offset;
      return 12;
    }

    return 8;
  }

  // DI
  case 0xF3: {
    m_ime = false;
    return 4;
  }

  // LD (FF00+u8),A
  case 0xE0: {
    const u8 offset = fetch8();
    const u16 addr = 0xFF00 + offset;

    m_bus.write(addr, m_registers.a);

    return 12;
  }

  // LD A,(FF00+u8)
  case 0xF0: {
    const u8 offset = fetch8();
    m_registers.a = m_bus.read(0xFF00 + offset);

    return 12;
  }

  // CP A,u8
  case 0xFE: {
    const u8 value = fetch8();
    const u8 a = m_registers.a;
    const u8 res = a - value;

    set_flag(Flag::Z, res == 0);
    set_flag(Flag::N, true);
    set_flag(Flag::H, (a & 0x0F) < (value & 0x0F));
    set_flag(Flag::C, (a < value));

    return 8;
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
