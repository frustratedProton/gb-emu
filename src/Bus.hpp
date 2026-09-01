#pragma once

#include "types.hpp"

#include <array>
#include <vector>

class Bus {
public:
  explicit Bus(const std::vector<u8> &rom) : m_rom(rom) {}

  [[nodiscard]] u8 read(u16 addr) const;
  void write(u16 addr, u8 value);

  [[nodiscard]] u8 get_if() const { return m_io[0x0F]; }
  void set_if(u8 value) { m_io[0x0F] = value; }

  void request_interrupt(u8 bit) { m_io[0x0F] |= (1 << bit); }

private:
  const std::vector<u8> &m_rom;
  std::array<u8, 0x2000> m_vram{}; // video ram
  std::array<u8, 0x2000> m_wram{}; // work ram
  std::array<u8, 0x00A0> m_oam{};  // object attribute memory
  std::array<u8, 0x0080> m_io{};
  std::array<u8, 0x007F> m_hram{}; // high ram

  u8 m_ie{};
};
