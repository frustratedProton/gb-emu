#pragma once

#include "types.hpp"
#include <array>
#include <cstddef>
#include <vector>

class Apu {
public:
  u8 read_register(u16 addr) const;
  void write_register(u16 addr, u8 value);

  void tick(u32 cycles);

  std::vector<i16> &samples() { return m_samples; }

  u8 channel_output(std::size_t ch) const; // debugging hook
private:
  std::array<u8, 0x30> m_regs{}; // FF10-FF3F backing store
  bool m_powered{false};
  void power_off();

  struct Channel {
    bool enabled{false};
    u16 length{0};
    bool length_enabled{false};
  };

  std::array<Channel, 4> m_ch{};

  u32 m_fs_cycles{0};
  u8 m_fs_step{0}; // step about to execute

  bool dac_on(std::size_t ch) const;
  void clock_lengths();
  void write_nrx4(std::size_t ch, u8 value);

  u16 m_sweep_shadow{0};
  u8 m_sweep_timer{0};
  bool m_sweep_enabled{false};
  bool m_sweep_negated{false};

  u16 sweep_calc();
  void clock_sweep();

  struct Square {
    u32 timer{0};
    u8 duty_pos{0};
    u8 volume{0};
    u8 env_timer{0};
  };

  std::array<Square, 2> m_sq{};

  void clock_envelopes();
  void step_squares(u32 cycles);
  u8 square_output(std::size_t idx) const;

  struct Wave {
    u32 timer{0};
    u8 pos{0};
  };

  struct Noise {
    u32 timer{0};
    u16 lfsr{0x7FFF};
    u8 volume{0};
    u8 env_timer{0};
  };

  Wave m_wave{};
  Noise m_noise{};

  void step_wave(u32 cycles);
  void step_noise(u32 cycles);

  u64 m_sample_clock{0};
  std::vector<i16> m_samples;

  void emit_sample();
};