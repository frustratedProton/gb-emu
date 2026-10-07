#pragma once

#include "types.hpp"

#include <array>
#include <vector>

class Bus {
public:
  explicit Bus(const std::vector<u8> &rom) : m_rom(rom) {
    m_io[0x00] = 0xCF; // JOYP: no buttons pressed
    m_io[0x40] = 0x91; // LCDC
    m_io[0x42] = 0x00; // SCY
    m_io[0x43] = 0x00; // SCX
    m_io[0x45] = 0x00; // LYC
    m_io[0x47] = 0xFC; // BGP
    m_io[0x48] = 0xFF; // OBP0
    m_io[0x49] = 0xFF; // OBP1
    m_io[0x4A] = 0x00; // WY
    m_io[0x4B] = 0x00; // WX
  }

  [[nodiscard]] u8 read(u16 addr) const;
  void write(u16 addr, u8 value);

  [[nodiscard]] u8 get_if() const { return m_io[0x0F]; }
  void set_if(u8 value) { m_io[0x0F] = value; }

  void tick(u32 cycles);

  void request_interrupt(u8 bit);

  void set_joypad_state(u8 directions, u8 buttons) {
    // Active-low
    // 1 -> released
    // 0 -> pressed
    m_joypad_directions = directions & 0x0F;
    m_joypad_buttons = buttons & 0x0F;
  }

private:
  const std::vector<u8> &m_rom;
  std::array<u8, 0x2000> m_vram{}; // video ram
  std::array<u8, 0x2000> m_wram{}; // work ram
  std::array<u8, 0x00A0> m_oam{};  // object attribute memory
  std::array<u8, 0x0080> m_io{};
  std::array<u8, 0x007F> m_hram{}; // high ram

  u8 m_joypad_directions{0x0F};
  u8 m_joypad_buttons{0x0F};

  u8 m_ie{};
  //   u32 m_ppu_cycles{};
  u32 m_div_cycles{};
  u32 m_timer_cycles{};
};
