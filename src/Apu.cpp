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

namespace {
constexpr u8 duty_table[4] = {0b00000001, 0b10000001, 0b10000111, 0b01111110};
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
    switch (addr) {
    case 0xFF11:
      m_ch[0].length = static_cast<u16>(64 - (value & 0x3F));
      break;
    case 0xFF16:
      m_ch[1].length = static_cast<u16>(64 - (value & 0x3F));
      break;
    case 0xFF1B:
      m_ch[2].length = static_cast<u16>(256 - value);
      break;
    case 0xFF20:
      m_ch[3].length = static_cast<u16>(64 - (value & 0x3F));
      break;
    default:
      break;
    }

    return; // register store still skipped in all cases
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

  case 0xFF10: {
    if (m_sweep_negated && (value & 0x08) == 0) {
      m_ch[0].enabled = false;
    }
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

    if (ch == 0) {
      const u8 nr10 = m_regs[0x00];
      const u8 period = static_cast<u8>((nr10 >> 4) & 0x07);
      const u8 shift = nr10 & 0x07;

      m_sweep_shadow = static_cast<u16>(m_regs[0x03] | ((value & 0x07) << 8));
      m_sweep_timer = period != 0 ? period : 8;
      m_sweep_enabled = period != 0 || shift != 0;
      m_sweep_negated = false;

      // Quirk (test 04): shift != 0 runs an immediate
      // overflow check on trigger; shift == 0 does NOT.
      if (shift != 0) {
        static_cast<void>(sweep_calc());
      }
    }

    if (ch < 2) {
      const std::size_t base = ch == 0 ? 0x03 : 0x08;
      const std::size_t env_reg = ch == 0 ? 0x02 : 0x07;

      const u16 freq = static_cast<u16>(m_regs[base] | ((value & 0x07) << 8));

      m_sq[ch].timer = (2048u - freq) * 4u;
      m_sq[ch].volume = static_cast<u8>(m_regs[env_reg] >> 4);
      m_sq[ch].env_timer = m_regs[env_reg] & 0x07;
    }

    if (ch == 2) {
      const u16 freq = static_cast<u16>(m_regs[0x0D] | ((value & 0x07) << 8));
      m_wave.timer = (2048u - freq) * 2u;
      m_wave.pos = 0;
    }

    if (ch == 3) {
      static constexpr u32 divisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};
      const u8 nr43 = m_regs[0x12];

      m_noise.timer = divisors[nr43 & 0x07] << (nr43 >> 4);
      m_noise.lfsr = 0x7FFF;
      m_noise.volume = static_cast<u8>(m_regs[0x11] >> 4);
      m_noise.env_timer = m_regs[0x11] & 0x07;
    }
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

  m_sq = {};
  m_wave = {};
  m_noise = {};

  m_sweep_shadow = 0;
  m_sweep_timer = 0;
  m_sweep_enabled = false;
  m_sweep_negated = false;
}

void Apu::tick(u32 cycles) {
  if (m_powered) {
    step_squares(cycles);
    step_wave(cycles);
    step_noise(cycles);
    m_fs_cycles += cycles;

    while (m_fs_cycles >= 8192) {
      m_fs_cycles -= 8192;

      if ((m_fs_step & 1) == 0) {
        clock_lengths();
      }

      // step 2, 6: sweep
      if (m_fs_step == 2 || m_fs_step == 6) {
        clock_sweep();
      }

      // step 7: envelope
      if (m_fs_step == 7) {
        clock_envelopes();
      }

      m_fs_step = static_cast<u8>((m_fs_step + 1) & 7);
    }
  }

  m_sample_clock += static_cast<u64>(cycles) * 44100u;

  while (m_sample_clock >= 4194304u) {
    m_sample_clock -= 4194304u;
    emit_sample();
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

u16 Apu::sweep_calc() {
  const u8 nr10 = m_regs[0x00];
  const u8 shift = nr10 & 0x07;

  u16 new_freq = m_sweep_shadow >> shift;

  if ((nr10 & 0x08) != 0) {
    m_sweep_negated = true;
    new_freq = static_cast<u16>(m_sweep_shadow - new_freq);
  } else {
    new_freq = static_cast<u16>(m_sweep_shadow + new_freq);
  }

  if (new_freq > 2047) {
    m_ch[0].enabled = false;
  }

  return new_freq;
}

void Apu::clock_sweep() {
  if (m_sweep_timer > 0) {
    --m_sweep_timer;
  }

  if (m_sweep_timer != 0) {
    return;
  }

  const u8 period = static_cast<u8>((m_regs[0x00] >> 4) & 0x07);
  m_sweep_timer = period != 0 ? period : 8;

  if (!m_sweep_enabled || period == 0) {
    return;
  }

  const u16 new_freq = sweep_calc();
  const u8 shift = m_regs[0x00] & 0x07;

  if (new_freq <= 2047 && shift != 0) {
    m_sweep_shadow = new_freq;
    m_regs[0x03] = static_cast<u8>(new_freq & 0xFF);
    m_regs[0x04] =
        static_cast<u8>((m_regs[0x04] & 0xF8) | ((new_freq >> 8) & 0x07));
    static_cast<void>(sweep_calc());
  }
}

void Apu::step_squares(u32 cycles) {
  for (std::size_t i = 0; i < 2; ++i) {
    const std::size_t base = i == 0 ? 0x03 : 0x08;
    const u16 freq =
        static_cast<u16>(m_regs[base] | ((m_regs[base + 1] & 0x07) << 8));
    const u32 period = (2048u - freq) * 4u;

    u32 remaining = cycles;

    while (remaining > 0) {
      if (m_sq[i].timer > remaining) {
        m_sq[i].timer -= remaining;
        remaining = 0;
      } else {
        remaining -= m_sq[i].timer;
        m_sq[i].timer = period;
        m_sq[i].duty_pos = static_cast<u8>((m_sq[i].duty_pos + 1) & 7);
      }
    }
  }
}

u8 Apu::square_output(std::size_t idx) const {
  const std::size_t ch = idx; // channel index 0/1

  if (!m_ch[ch].enabled || !dac_on(ch)) {
    return 0;
  }

  const std::size_t duty_reg = idx == 0 ? 0x01 : 0x06;
  const u8 duty = static_cast<u8>(m_regs[duty_reg] >> 6);
  const bool bit = (duty_table[duty] >> (7 - m_sq[idx].duty_pos)) & 1;

  return bit ? m_sq[idx].volume : 0;
}

u8 Apu::channel_output(std::size_t ch) const {
  if (ch < 2) {
    return square_output(ch);
  }

  if (!m_ch[ch].enabled || !dac_on(ch)) {
    return 0;
  }

  if (ch == 2) {
    const u8 byte = m_regs[0x20 + (m_wave.pos >> 1)];
    const u8 nibble = (m_wave.pos & 1) != 0 ? static_cast<u8>(byte & 0x0F)
                                            : static_cast<u8>(byte >> 4);

    switch ((m_regs[0x0C] >> 5) & 0x03) { // NR32
    case 0:
      return 0;
    case 1:
      return nibble;
    case 2:
      return static_cast<u8>(nibble >> 1);
    default:
      return static_cast<u8>(nibble >> 2);
    }
  }

  return (m_noise.lfsr & 1) == 0 ? m_noise.volume : 0;
}

void Apu::clock_envelopes() {
  // Square channels: NR12 at 0x02, NR22 at 0x07
  for (std::size_t i = 0; i < 2; ++i) {
    const std::size_t reg = i == 0 ? 0x02 : 0x07;
    const u8 period = m_regs[reg] & 0x07;

    if (period == 0) {
      continue;
    }

    if (m_sq[i].env_timer > 0) {
      --m_sq[i].env_timer;
    }

    if (m_sq[i].env_timer == 0) {
      m_sq[i].env_timer = period;

      const bool up = (m_regs[reg] & 0x08) != 0;

      if (up && m_sq[i].volume < 15) {
        ++m_sq[i].volume;
      } else if (!up && m_sq[i].volume > 0) {
        --m_sq[i].volume;
      }
    }
  }

  const u8 period = m_regs[0x11] & 0x07;

  if (period == 0) {
    return;
  }

  if (m_noise.env_timer > 0) {
    --m_noise.env_timer;
  }

  if (m_noise.env_timer == 0) {
    m_noise.env_timer = period;

    const bool up = (m_regs[0x11] & 0x08) != 0;

    if (up && m_noise.volume < 15) {
      ++m_noise.volume;
    } else if (!up && m_noise.volume > 0) {
      --m_noise.volume;
    }
  }
}

void Apu::step_wave(u32 cycles) {
  const u16 freq =
      static_cast<u16>(m_regs[0x0D] | ((m_regs[0x0E] & 0x07) << 8));
  const u32 period = (2048u - freq) * 2u;

  u32 remaining = cycles;

  while (remaining > 0) {
    if (m_wave.timer > remaining) {
      m_wave.timer -= remaining;
      remaining = 0;
    } else {
      remaining -= m_wave.timer;
      m_wave.timer = period;
      m_wave.pos = static_cast<u8>((m_wave.pos + 1) & 31);
    }
  }
}

void Apu::step_noise(u32 cycles) {
  static constexpr u32 divisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};

  const u8 nr43 = m_regs[0x12];
  const u8 shift = static_cast<u8>(nr43 >> 4);

  // Shifts 14-15 silence the channel on hardware
  // just make the period huge enough to not matter
  const u32 period = divisors[nr43 & 0x07] << shift;

  u32 remaining = cycles;

  while (remaining > 0) {
    if (m_noise.timer > remaining) {
      m_noise.timer -= remaining;
      remaining = 0;
    } else {
      remaining -= m_noise.timer;
      m_noise.timer = period;

      const u16 x = static_cast<u16>((m_noise.lfsr ^ (m_noise.lfsr >> 1)) & 1);

      m_noise.lfsr = static_cast<u16>((m_noise.lfsr >> 1) | (x << 14));

      if ((nr43 & 0x08) != 0) {
        m_noise.lfsr = static_cast<u16>((m_noise.lfsr & ~0x40u) | (x << 6));
      }
    }
  }
}

void Apu::emit_sample() {
  float left = 0.0f;
  float right = 0.0f;

  if (m_powered) {
    const u8 nr50 = m_regs[0x14];
    const u8 nr51 = m_regs[0x15];

    for (std::size_t ch = 0; ch < 4; ch++) {
      if (!dac_on(ch)) {
        continue; // silence
      }

      // DAC turns digital 0-15 into analog -1...+1
      const float analog = static_cast<float>(channel_output(ch)) / 7.5f - 1.0f;

      if ((nr51 & (1u << (ch + 4))) != 0)
        left += analog;
      if ((nr51 & (1u << ch)) != 0)
        right += analog;
    }

    left *= static_cast<float>(((nr50 >> 4) & 0x07) + 1) / 8.0f;
    right *= static_cast<float>((nr50 & 0x07) + 1) / 8.0f;
  }

  const auto to_i16 = [](float v) {
    v *= 0.25f * 32767.0f;
    if (v > 32767.0f)
      v = 32767.0f;
    if (v < -32768.0f)
      v = -32768.0f;
    return static_cast<i16>(v);
  };

  m_samples.push_back(to_i16(left));
  m_samples.push_back(to_i16(right));
}