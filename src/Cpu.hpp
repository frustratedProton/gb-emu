#pragma once

#include "Bus.hpp"
#include "types.hpp"

enum class Flag : u8 {
  Z = 0x80,
  N = 0x40,
  H = 0x20,
  C = 0x10,
};

struct Registers {
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

  [[nodiscard]] const Registers &registers() const { return m_registers; }

  void set_pc(u16 value) { m_registers.pc = value; }
  void set_sp(u16 value) { m_registers.sp = value; }

private:
  [[nodiscard]] u32 execute_cb();

  [[nodiscard]] u8 rlc(u8 value);
  [[nodiscard]] u8 rrc(u8 value);
  [[nodiscard]] u8 rl(u8 value);
  [[nodiscard]] u8 rr(u8 value);
  [[nodiscard]] u8 sla(u8 value);
  [[nodiscard]] u8 sra(u8 value);
  [[nodiscard]] u8 swap(u8 value);
  [[nodiscard]] u8 srl(u8 value);

  void xor_a(u8 value);
  void or_a(u8 value);
  void and_a(u8 value);
  void cp_a(u8 value);
  void sub_a(u8 value);
  void add_a(u8 value);
  void adc_a(u8 value);
  void sbc_a(u8 value);

  [[nodiscard]] u8 fetch8();
  [[nodiscard]] u16 fetch16();

  void push16(u16 value);
  [[nodiscard]] u16 pop16();
  u16 read_r16_push(u8 reg) const;
  void write_r16_push(u8 reg, u16 value);

  [[nodiscard]] u8 read_r8(u8 code) const;
  [[nodiscard]] u16 read_r16(u16 code) const;
  void write_r8(u8 code, u8 value);
  void write_r16(u8 code, u16 value);

  void execute_alu(u8 operation, u8 value);

  u8 inc8(u8 value);

  [[nodiscard]] u8 dec8(u8 value);

  Bus &m_bus;
  Registers m_registers{};

  bool m_ime{};
  bool m_halted{};
};
