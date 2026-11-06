#include "types.hpp"
#include <array>

class Apu {
public:
  u8 read_register(u16 addr) const;
  void write_register(u16 addr, u8 value);

  void tick(u32 cycles);

private:
  std::array<u8, 0x30> m_regs{}; // FF10-FF3F backing store
  bool m_powered{false};
  void power_off();
};