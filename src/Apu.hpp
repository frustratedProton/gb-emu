#include "types.hpp"
#include <array>
#include <cstddef>

class Apu {
public:
  u8 read_register(u16 addr) const;
  void write_register(u16 addr, u8 value);

  void tick(u32 cycles);

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
};