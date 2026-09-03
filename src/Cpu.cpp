#include "Cpu.hpp"
#include "Bus.hpp"
#include "types.hpp"
#include <iomanip>
#include <iostream>
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

void Cpu::or_a(u8 value) {
  m_registers.a = static_cast<u8>(m_registers.a | value);

  // OR clears N, H and C flags
  // and sets Zero only when result is zero
  m_registers.f = 0x00;

  if (m_registers.a == 0) {
    m_registers.f = static_cast<u8>(Flag::Z);
  }
}

void Cpu::and_a(u8 value) {
  m_registers.a = static_cast<u8>(m_registers.a & value);

  // AND clearns N and C, sets H
  m_registers.f = static_cast<u8>(Flag::H);

  // Z is set when the result is zero
  if (m_registers.a == 0) {
    m_registers.f |= static_cast<u8>(Flag::Z);
  }
}

void Cpu::cp_a(u8 value) {
  const u8 a = m_registers.a;
  const u8 result = static_cast<u8>(a - value);

  // CP behaves like SUB but does not store the result in A.
  m_registers.f = static_cast<u8>(Flag::N);

  if (result == 0) {
    m_registers.f |= static_cast<u8>(Flag::Z);
  }

  if ((a & 0x0F) < (value & 0x0F)) {
    m_registers.f |= static_cast<u8>(Flag::H);
  }

  if (a < value) {
    m_registers.f |= static_cast<u8>(Flag::C);
  }
}

void Cpu::sub_a(u8 value) {
  const u8 a = m_registers.a;
  const u8 res = static_cast<u8>(a - value);

  m_registers.a = res;
  m_registers.f = static_cast<u8>(Flag::N);

  if (res == 0) {
    m_registers.f |= static_cast<u8>(Flag::Z);
  }

  if ((a & 0x0F) < (value & 0x0F)) {
    m_registers.f |= static_cast<u8>(Flag::H);
  }

  if (a < value) {
    m_registers.f |= static_cast<u8>(Flag::C);
  }
}

void Cpu::add_a(u8 value) {
  const u8 a = m_registers.a;
  const u16 result = static_cast<u16>(a) + value;

  m_registers.a = static_cast<u8>(result);

  m_registers.f = 0x00;

  if (m_registers.a == 0) {
    m_registers.f |= static_cast<u8>(Flag::Z);
  }

  if (((a & 0x0F) + (value & 0x0F)) > 0x0F) {
    m_registers.f |= static_cast<u8>(Flag::H);
  }

  if (result > 0xFF) {
    m_registers.f |= static_cast<u8>(Flag::C);
  }
}

void Cpu::adc_a(u8 value) {
  const u8 a = m_registers.a;
  const u8 carry = (m_registers.f & static_cast<u8>(Flag::C)) ? 1 : 0;

  const u16 res = static_cast<u16>(a) + value + carry;

  m_registers.a = static_cast<u8>(res);
  m_registers.f = 0x00;

  if (m_registers.a == 0) {
    m_registers.f |= static_cast<u8>(Flag::Z);
  }

  if (((a & 0x0F) + (value & 0x0F) + carry) > 0x0F) {
    m_registers.f |= static_cast<u8>(Flag::H);
  }

  if (res > 0xFF) {
    m_registers.f |= static_cast<u8>(Flag::C);
  }
}

void Cpu::sbc_a(u8 value) {
  const u8 a = m_registers.a;
  const u8 carry = (m_registers.f & static_cast<u8>(Flag::C)) ? 1 : 0;

  const u16 subtrahend = static_cast<u16>(value) + carry;
  const u8 result = static_cast<u8>(a - subtrahend);

  m_registers.a = result;

  m_registers.f = static_cast<u8>(Flag::N);

  if (result == 0) {
    m_registers.f |= static_cast<u8>(Flag::Z);
  }

  if ((a & 0x0F) < ((value & 0x0F) + carry)) {
    m_registers.f |= static_cast<u8>(Flag::H);
  }

  if (static_cast<u16>(a) < subtrahend) {
    m_registers.f |= static_cast<u8>(Flag::C);
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

void Cpu::execute_alu(u8 operation, u8 value) {
  // 0 → ADD
  // 1 → ADC
  // 2 → SUB
  // 3 → SBC
  // 4 → AND
  // 5 → XOR
  // 6 → OR
  // 7 → CP
  switch (operation) {
  case 0:
    add_a(value);
    return;

  case 1:
    adc_a(value);
    return;

  case 2:
    sub_a(value);
    return;

  case 3:
    sbc_a(value);
    return;

  case 4:
    and_a(value);
    return;

  case 5:
    xor_a(value);
    return;

  case 6:
    or_a(value);
    return;

  case 7:
    cp_a(value);
    return;

  default:
    throw std::logic_error{"Invalid ALU operation"};
  }
}

void Cpu::push16(u16 value) {
  m_registers.sp -= 1;
  m_bus.write(m_registers.sp, static_cast<u8>(value >> 8));
  m_registers.sp -= 1;
  m_bus.write(m_registers.sp, static_cast<u8>(value & 0xFF));
}

u16 Cpu::pop16() {
  const u8 lo = m_bus.read(m_registers.sp);
  m_registers.sp += 1;
  const u8 hi = m_bus.read(m_registers.sp);
  m_registers.sp += 1;
  return static_cast<u16>((hi << 8) | lo);
}

u16 Cpu::read_r16_push(u8 reg) const {
  switch (reg) {
  case 0:
    return bc();
  case 1:
    return de();
  case 2:
    return hl();
  case 3:
    return af();
  }
  return 0;
}

void Cpu::write_r16_push(u8 reg, u16 value) {
  switch (reg) {
  case 0:
    set_bc(value);
    break;

  case 1:
    set_de(value);
    break;

  case 2:
    set_hl(value);
    break;

  case 3:
    set_af(value);
    break;
  }
}

u8 Cpu::rlc(u8 value) {
  const u8 bit7 = (value >> 7) & 0x01;
  const u8 result = static_cast<u8>((value << 1) | bit7);
  set_flag(Flag::Z, result == 0);
  set_flag(Flag::N, false);
  set_flag(Flag::H, false);
  set_flag(Flag::C, bit7 != 0);
  return result;
}

u8 Cpu::rrc(u8 value) {
  const u8 bit0 = value & 0x01;
  const u8 result = static_cast<u8>((value >> 1) | (bit0 << 7));
  set_flag(Flag::Z, result == 0);
  set_flag(Flag::N, false);
  set_flag(Flag::H, false);
  set_flag(Flag::C, bit0 != 0);
  return result;
}

u8 Cpu::rl(u8 value) {
  const u8 old_carry = get_flag(Flag::C) ? 1 : 0;
  const u8 bit7 = (value >> 7) & 0x01;
  const u8 result = static_cast<u8>((value << 1) | old_carry);
  set_flag(Flag::Z, result == 0);
  set_flag(Flag::N, false);
  set_flag(Flag::H, false);
  set_flag(Flag::C, bit7 != 0);
  return result;
}

u8 Cpu::rr(u8 value) {
  const u8 old_carry = get_flag(Flag::C) ? 1 : 0;
  const u8 bit0 = value & 0x01;
  const u8 result = static_cast<u8>((value >> 1) | (old_carry << 7));
  set_flag(Flag::Z, result == 0);
  set_flag(Flag::N, false);
  set_flag(Flag::H, false);
  set_flag(Flag::C, bit0 != 0);
  return result;
}

u8 Cpu::sla(u8 value) {
  const u8 bit7 = (value >> 7) & 0x01;
  const u8 result = static_cast<u8>(value << 1);
  set_flag(Flag::Z, result == 0);
  set_flag(Flag::N, false);
  set_flag(Flag::H, false);
  set_flag(Flag::C, bit7 != 0);
  return result;
}

u8 Cpu::swap(u8 value) {
  const u8 result = static_cast<u8>((value << 4) | (value >> 4));
  set_flag(Flag::Z, result == 0);
  set_flag(Flag::N, false);
  set_flag(Flag::H, false);
  set_flag(Flag::C, false);
  return result;
}

u8 Cpu::srl(u8 value) {
  const u8 bit0 = value & 0x01;
  const u8 result = static_cast<u8>(value >> 1);
  set_flag(Flag::Z, result == 0);
  set_flag(Flag::N, false);
  set_flag(Flag::H, false);
  set_flag(Flag::C, bit0 != 0);
  return result;
}

u8 Cpu::sra(u8 value) {
  const u8 bit0 = value & 0x01;
  const u8 bit7 = value & 0x80;
  const u8 result = static_cast<u8>((value >> 1) | bit7);
  set_flag(Flag::Z, result == 0);
  set_flag(Flag::N, false);
  set_flag(Flag::H, false);
  set_flag(Flag::C, bit0 != 0);
  return result;
}

u32 Cpu::execute_cb() {
  const u8 opcode = fetch8();

  const u8 op = (opcode >> 6) & 0x03;
  const u8 bit = (opcode >> 3) & 0x07;
  const u8 reg = opcode & 0x07;

  const bool is_hl = reg == 6;
  const u8 value = read_r8(reg);

  // BIT - just tests, does not write back
  if (op == 1) {
    const bool is_set = (value & (1 << bit)) != 0;
    set_flag(Flag::Z, !is_set);
    set_flag(Flag::N, false);
    set_flag(Flag::H, true);
    return is_hl ? 12 : 8;
  }

  // RES - clears a bit, writes back
  if (op == 2) {
    const u8 result = value & static_cast<u8>(~(1 << bit));
    write_r8(reg, result);
    return is_hl ? 16 : 8;
  }

  // SET - sets a bit, writes back
  if (op == 3) {
    const u8 result = value | static_cast<u8>(1 << bit);
    write_r8(reg, result);
    return is_hl ? 16 : 8;
  }

  // op == 0, shifts and rotates, bit field selects which one
  u8 result = 0;
  switch (bit) {
  case 0:
    result = rlc(value);
    break;
  case 1:
    result = rrc(value);
    break;
  case 2:
    result = rl(value);
    break;
  case 3:
    result = rr(value);
    break;
  case 4:
    result = sla(value);
    break;
  case 5:
    result = sra(value);
    break;
  case 6:
    result = swap(value);
    break;
  case 7:
    result = srl(value);
    break;
  }

  write_r8(reg, result);
  return is_hl ? 16 : 8;
}

u32 Cpu::handle_interrupts() {
  if (!m_ime && !m_halted)
    return 0;

  const u8 ie = m_bus.read(0xFFFF);  // what interrupts are enabled
  const u8 if_ = m_bus.read(0xFF0F); // what interrupts are pending
  const u8 pending = ie & if_;       // only care about enabled+pending

  if (pending == 0)
    return 0;

  m_halted = false; // wake up halt

  if (!m_ime)
    return 0;

  for (u8 bit = 0; bit < 5; ++bit) {
    if ((pending & (1 << bit)) == 0)
      continue;

    m_bus.write(0xFF0F, static_cast<u8>(if_ & ~(1 << bit)));

    m_ime = false;

    push16(m_registers.pc);

    static constexpr u16 vectors[5] = {
        0x0040, // VBlank
        0x0048, // LCD STAT
        0x0050, // Timer
        0x0058, // Serial
        0x0060, // Joypad
    };

    m_registers.pc = vectors[bit];
    return 20;
  }
  return 0;
}

u32 Cpu::execute_instruction() {
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

  // ALU A, r8
  if (opcode >= 0x80 && opcode <= 0xBF) {
    const u8 operation = static_cast<u8>((opcode >> 3) & 0x07);
    const u8 src = static_cast<u8>(opcode & 0x07);
    const u8 value = read_r8(src);

    execute_alu(operation, value);

    return src == 6 ? 8 : 4;
  }

  // ALU A,d8
  if ((opcode & 0xC7) == 0xC6) {
    const u8 operation = static_cast<u8>((opcode >> 3) & 0x07);
    const u8 value = fetch8();

    execute_alu(operation, value);

    return 8;
  }

  if ((opcode & 0xCF) == 0x02 || (opcode & 0xCF) == 0x0A) {
    const u8 pair = static_cast<u8>((opcode >> 4) & 0x03);
    const bool to_a = (opcode & 0x08) != 0;
    const bool hl_pair = pair == 2 || pair == 3;
    const bool increment = pair == 2;

    const u16 addr = hl_pair ? hl() : read_r16(pair);

    if (to_a) {
      m_registers.a = m_bus.read(addr);
    } else {
      m_bus.write(addr, m_registers.a);
    }

    if (hl_pair) {
      set_hl(increment ? static_cast<u16>(addr + 1)
                       : static_cast<u16>(addr - 1));
    }

    return 8;
  }

  // JR r8
  if (opcode == 0x18) {
    const i8 offset = static_cast<i8>(fetch8());
    m_registers.pc += offset;
    return 12;
  }

  // JR cc,r8
  if ((opcode & 0xE7) == 0x20) {
    const i8 offset = static_cast<i8>(fetch8());
    const u8 cc = (opcode >> 3) & 0x03;

    bool take;

    switch (cc) {
    case 0:
      take = !get_flag(Flag::Z);
      break; // NZ
    case 1:
      take = get_flag(Flag::Z);
      break; // Z
    case 2:
      take = !get_flag(Flag::C);
      break; // NC
    case 3:
      take = get_flag(Flag::C);
      break; // C
    }

    if (take) {
      m_registers.pc += offset;
      return 12;
    }

    return 8;
  }

  // RET
  if ((opcode & 0xE7) == 0xC0) {
    const u8 cc = static_cast<u8>((opcode >> 3) & 0x03);

    bool take;
    switch (cc) {
    case 0:
      take = !get_flag(Flag::Z);
      break; // NZ
    case 1:
      take = get_flag(Flag::Z);
      break; // Z
    case 2:
      take = !get_flag(Flag::C);
      break; // NC
    default:
      take = get_flag(Flag::C);
      break; // C
    }

    if (take) {
      m_registers.pc = pop16();
      return 20;
    }
    return 8;
  }

  // PUSH HL
  if ((opcode & 0xCF) == 0xC5) {
    const u8 pair = static_cast<u8>((opcode >> 4) & 0x03);
    push16(read_r16_push(pair));
    return 16;
  }

  // POP HL
  if ((opcode & 0xCF) == 0xC1) {
    const u8 pair = static_cast<u8>((opcode >> 4) & 0x03);
    write_r16_push(pair, pop16());
    return 12;
  }

  // CALL cc, u16
  if ((opcode & 0xE7) == 0xC4) {
    const u16 target = fetch16();
    const u8 cc = static_cast<u8>((opcode >> 3) & 0x03);

    bool take;
    switch (cc) {
    case 0:
      take = !get_flag(Flag::Z);
      break; // NZ
    case 1:
      take = get_flag(Flag::Z);
      break; // Z
    case 2:
      take = !get_flag(Flag::C);
      break; // NC
    default:
      take = get_flag(Flag::C);
      break; // C
    }

    if (take) {
      push16(m_registers.pc);
      m_registers.pc = target;
      return 24;
    }

    return 12;
  }

  // ADD HL, rr
  if ((opcode & 0xCF) == 0x09) {
    const u8 src = static_cast<u8>((opcode >> 4) & 0x03);
    const u16 rr = read_r16(src);
    const u16 h = hl();

    const u32 result = static_cast<u32>(h) + static_cast<u32>(rr);

    const bool half_carry = ((h & 0x0FFF) + (rr & 0x0FFF)) > 0x0FFF;
    const bool carry = result > 0xFFFF;

    set_hl(static_cast<u16>(result));

    set_flag(Flag::N, false);
    set_flag(Flag::H, half_carry);
    set_flag(Flag::C, carry);

    return 8;
  }

  // JP cc
  if ((opcode & 0xE7) == 0xC2) {
    const u16 target = fetch16();
    const u8 cc = static_cast<u8>((opcode >> 3) & 0x03);

    bool take{};
    switch (cc) {
    case 0:
      take = !get_flag(Flag::Z);
      break; // NZ
    case 1:
      take = get_flag(Flag::Z);
      break; // Z
    case 2:
      take = !get_flag(Flag::C);
      break; // NC
    default:
      take = get_flag(Flag::C);
      break; // C
    }

    if (take) {
      m_registers.pc = target;
      return 16;
    }
    return 12;
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

  // DAA
  case 0x27: {
    u8 reg_a = m_registers.a;
    u8 adjust = 0;

    if (!get_flag(Flag::N)) {
      // after addition
      if (get_flag(Flag::H) || (reg_a & 0x0F) > 9)
        adjust |= 0x06;

      if (get_flag(Flag::C) || reg_a > 0x99) {
        adjust |= 0x60;
        set_flag(Flag::C, true);
      }

      reg_a += adjust;
    } else {
      // after subtraction
      if (get_flag(Flag::H))
        adjust |= 0x06;

      if (get_flag(Flag::C)) {
        adjust |= 0x60;
      }

      reg_a -= adjust;
    }

    m_registers.a = reg_a;
    set_flag(Flag::Z, reg_a == 0);
    set_flag(Flag::H, false); // always cleared
    return 4;
  }

  // CALL a16
  case 0xCD: {
    const u16 target = fetch16();
    push16(m_registers.pc);
    m_registers.pc = target;
    return 24;
  }

  // RET 0xC9
  case 0xC9: {
    m_registers.pc = pop16();
    return 16;
  }

  // PREFIX CB
  case 0xCB: {
    return execute_cb();
  }

  // RETI
  case 0xD9: {
    m_registers.pc = pop16();
    m_ime = true;
    return 16;
  }

  // DI
  case 0xF3: {
    m_ime = false;
    return 4;
  }

  // EI
  case 0xFB: {
    m_ime_pending = true;
    return 4;
  }

  // LD (FF00+u8),A
  case 0xE0: {
    const u8 offset = fetch8();
    const u16 addr = 0xFF00 + offset;

    m_bus.write(addr, m_registers.a);

    return 12;
  }

  // LD (a16), A
  case 0xEA: {
    const u16 addr = fetch16();
    m_bus.write(addr, m_registers.a);
    return 16;
  }

  // JP HL
  case 0xE9: {
    m_registers.pc = hl();
    return 4;
  }

  // LD A,(FF00+u8)
  case 0xF0: {
    const u8 offset = fetch8();
    m_registers.a = m_bus.read(0xFF00 + offset);

    return 12;
  }

  // LD A,(a16)
  case 0xFA: {
    const u16 addr = fetch16();
    m_registers.a = m_bus.read(addr);
    return 16;
  }

  // i used switch for below 4 cases cause
  // i felt bitmask was kinda weird to read
  case 0x07: {
    const u8 bit7 = (m_registers.a >> 7) & 0x01;
    m_registers.a = static_cast<u8>((m_registers.a << 1) | bit7);
    set_flag(Flag::Z, false);
    set_flag(Flag::N, false);
    set_flag(Flag::H, false);
    set_flag(Flag::C, bit7 != 0);
    return 4;
  }

  case 0x0F: {
    const u8 bit0 = m_registers.a & 0x01;
    m_registers.a = static_cast<u8>((m_registers.a >> 1) | (bit0 << 7));
    set_flag(Flag::Z, false);
    set_flag(Flag::N, false);
    set_flag(Flag::H, false);
    set_flag(Flag::C, bit0 != 0);
    return 4;
  }

    // RLA
  case 0x17: {
    const u8 old_carry = get_flag(Flag::C) ? 1 : 0;
    const u8 bit7 = (m_registers.a >> 7) & 0x01;
    m_registers.a = static_cast<u8>((m_registers.a << 1) | old_carry);
    set_flag(Flag::Z, false);
    set_flag(Flag::N, false);
    set_flag(Flag::H, false);
    set_flag(Flag::C, bit7 != 0);
    return 4;
  }

  // RRA
  case 0x1F: {
    const u8 old_carry = get_flag(Flag::C) ? 1 : 0;
    const u8 bit0 = m_registers.a & 0x01;
    m_registers.a = static_cast<u8>((m_registers.a >> 1) | (old_carry << 7));
    set_flag(Flag::Z, false);
    set_flag(Flag::N, false);
    set_flag(Flag::H, false);
    set_flag(Flag::C, bit0 != 0);
    return 4;
  }

  // LD HL, SP+i8
  case 0xF8: {
    const i8 offset = static_cast<i8>(fetch8());
    const u16 sp = m_registers.sp;
    const u16 result = static_cast<u16>(sp + offset);

    const u8 sp_lo = static_cast<u8>(sp & 0xFF);
    const u8 off_u8 = static_cast<u8>(offset);

    set_flag(Flag::Z, false);
    set_flag(Flag::N, false);
    set_flag(Flag::H, ((sp_lo & 0x0F) + (off_u8 & 0x0F)) > 0x0F);
    set_flag(Flag::C,
             (static_cast<u16>(sp_lo) + static_cast<u16>(off_u8)) > 0xFF);

    set_hl(result);
    return 12;
  }

  // ADD SP, i8
  case 0xE8: {
    const i8 offset = static_cast<i8>(fetch8());
    const u16 sp = m_registers.sp;

    const u8 sp_lo = static_cast<u8>(sp & 0xFF);
    const u8 off_u8 = static_cast<u8>(offset);

    set_flag(Flag::Z, false);
    set_flag(Flag::N, false);
    set_flag(Flag::H, ((sp_lo & 0x0F) + (off_u8 & 0x0F)) > 0x0F);
    set_flag(Flag::C,
             (static_cast<u16>(sp_lo) + static_cast<u16>(off_u8)) > 0xFF);

    m_registers.sp = static_cast<u16>(sp + offset);
    return 16;
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

u32 Cpu::step() {
  const u32 interrupt_cycles = handle_interrupts();
  if (interrupt_cycles > 0) {
    return interrupt_cycles;
  }

  if (m_halted)
    return 4; // temp behaviour

  const u32 cycles = execute_instruction();

  if (m_ime_pending) {
    m_ime_pending = false;
    m_ime = true;
  }

  return cycles;
}
