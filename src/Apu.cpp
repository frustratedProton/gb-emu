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

  if (addr == 0xFF26) {
    u8 value = 0x70;

    if (m_powered)
      value |= 0x80;
    if (m_ch[0].enabled)
      value |= 0x01;
    if (m_ch[1].enabled)
      value |= 0x02;
    if (m_ch[2].enabled)
      value |= 0x04;
    if (m_ch[3].enabled)
      value |= 0x08;

    return value;
  }

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

  switch (addr) {
  // NRx1: length load
  case 0xFF11: {
    m_ch[0].length = static_cast<u16>(64 - (value & 0x3F));
    break;
  }
  case 0xFF16: {
    m_ch[1].length = static_cast<u16>(64 - (value & 0x3F));
    break;
  }
  case 0xFF1B: {
    m_ch[2].length = static_cast<u16>(256 - value);
    break;
  }
  case 0xFF20: {

    m_ch[3].length = static_cast<u16>(64 - (value & 0x3F));
    break;
  }

  // DAC controls: DAC off kills the channel
  case 0xFF12: {
    if ((value & 0xF8) == 0)
      m_ch[0].enabled = false;
    break;
  }
  case 0xFF17: {
    if ((value & 0xF8) == 0)
      m_ch[1].enabled = false;
    break;
  }
  case 0xFF1A: {
    if ((value & 0x80) == 0)
      m_ch[2].enabled = false;
    break;
  }
  case 0xFF21: {
    if ((value & 0xF8) == 0)
      m_ch[3].enabled = false;
    break;
  }

  // NRx4: length enable + trigger
  case 0xFF14: {
    write_nrx4(0, value);
    break;
  }
  case 0xFF19: {
    write_nrx4(1, value);
    break;
  }
  case 0xFF1E: {
    write_nrx4(2, value);
    break;
  }
  case 0xFF23: {
    write_nrx4(3, value);
    break;
  }

  default:
    break;
  }

  m_regs[addr - 0xFF10] = value;
}

void Apu::write_nrx4(std::size_t ch, u8 value) {
  const bool trigger = (value & 0x80) != 0;
  const bool enable_length = (value & 0x40) != 0;
  const bool was_enabled = m_ch[ch].length_enabled;

  const bool next_clocks_length = (m_fs_step & 1) == 0;

  m_ch[ch].length_enabled = enable_length;

  // enbaling len (0 -> 1) when next sequencer step doesnt
  // clock length causes an immediate decrement (quirk)
  if (!was_enabled && enable_length && !next_clocks_length &&
      m_ch[ch].length > 0) {
    --m_ch[ch].length;

    if (m_ch[ch].length == 0 && !trigger) {
      m_ch[ch].enabled = false;
    }
  }

  if (trigger) {
    if (m_ch[ch].length == 0) {
      m_ch[ch].length = ch == 2 ? 256 : 64;

      // reload during a trigger is max-1 if length is enabled
      // and next step doesnt clock length (quirk)
      if (enable_length && !next_clocks_length) {
        --m_ch[ch].length;
      }
    }

    m_ch[ch].enabled = dac_on(ch);
  }
}

void Apu::power_off() {
  // Zero NR10-NR51 (FF10-FF25)
  // Wave RAM (FF30-FF3F) is preserved
  for (std::size_t i = 0; i <= 0x15; ++i) {
    m_regs[i] = 0;
  }

  m_fs_cycles = 0;
  m_fs_step = 0;

  for (auto &ch : m_ch) {
    ch.enabled = false;
    ch.length_enabled = false;
    // length will survie power off
  }
}

void Apu::tick(u32 cycles) {
  if (!m_powered) {
    return;
  }

  m_fs_cycles += cycles;

  while (m_fs_cycles >= 8192) {
    m_fs_cycles -= 8192;

    if ((m_fs_step & 1) == 0) {
      clock_lengths();
    }

    // step 2, 6: sweep
    // step 7: envelope
    m_fs_step = static_cast<u8>((m_fs_step + 1) & 7);
  }
}

void Apu::clock_lengths() {
  for (auto &ch : m_ch) {
    if (ch.length_enabled && ch.length > 0) {
      --ch.length;

      if (ch.length == 0) {
        ch.enabled = false;
      }
    }
  }
}

bool Apu::dac_on(std::size_t ch) const {
  switch (ch) {
  case 0:
    return (m_regs[0x02] & 0xF8) != 0; // NR12 (FF12)
  case 1:
    return (m_regs[0x07] & 0xF8) != 0; // NR22 (FF17)
  case 2:
    return (m_regs[0x0A] & 0x80) != 0; // NR30 (FF1A)
  default:
    return (m_regs[0x11] & 0xF8) != 0; // NR42 (FF21)
  }
}
