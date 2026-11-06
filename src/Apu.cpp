#include "Apu.hpp"
#include "types.hpp"
#include <cstddef>

namespace {
constexpr std::array<u8, 0x20> read_masks = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF, // FF10-FF14  NR10-NR14
    0xFF, 0x3F, 0x00, 0xFF, 0xBF, // FF15-FF19  ----,NR21-NR24
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF, // FF1A-FF1E  NR30-NR34
    0xFF, 0xFF, 0x00, 0x00, 0xBF, // FF1F-FF23  ----,NR41-NR44
    0x00, 0x00, 0x70,             // FF24-FF26  NR50,NR51,NR52
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // FF27-FF2B  unmapped
    0xFF, 0xFF, 0xFF, 0xFF,       // FF2C-FF2F  unmapped
};
} // namespace

u8 Apu::read_register(u16 addr) const {
  if (addr >= 0xFF30 && addr <= 0xFF3F) {
    return m_regs[addr - 0xFF10];
  }
  const std::size_t index = addr - 0xFF10;
  return static_cast<u8>(m_regs[index] | read_masks[index]);
}

void Apu::write_register(u16 addr, u8 value) {
  // WAV ram is always writable
  if (addr >= 0xFF30 && addr <= 0xFF3F) {
    m_regs[addr - 0xFF10] = value;
    return;
  }

  // NR52: only bit 7 is writable
  if (addr == 0xFF26) {
    const bool new_power = (value & 0x80) != 0;
    if (m_powered && !new_power) {
      power_off();
    }

    m_powered = new_power;

    // Bits 0-3 are read only
    m_regs[0x16] = static_cast<u8>(value & 0x80);
    return;
  }

  // while powered off, all other register
  // writes are ignored
  if (!m_powered) {
    return;
  }

  m_regs[addr - 0xFF10] = value;
}

void Apu::power_off() {
  // Zero NR10-NR51 (FF10-FF25).
  // Wave RAM (FF30-FF3F) is preserved.
  for (std::size_t i = 0; i <= 0x15; ++i) {
    m_regs[i] = 0;
  }
}

void Apu::tick(u32 cycles) {
  // Step 2: frame sequencer goes here.
  static_cast<void>(cycles);
}
