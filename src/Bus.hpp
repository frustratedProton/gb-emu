#pragma once

#include "types.hpp"

#include <array>
#include <string>
#include <vector>

enum class TimaReloadPhase : u8 { None, Overflow, Reload };

class Bus {
public:
  explicit Bus(const std::vector<u8> &rom, const std::string &save_path = {})
      : m_rom(rom), m_save_path(save_path) {
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

    init_cartrigde();
  }

  [[nodiscard]] u8 read(u16 addr) const;
  void write(u16 addr, u8 value);

  [[nodiscard]] u8 cpu_read(u16 addr) const;
  void cpu_write(u16 addr, u8 value);

  [[nodiscard]] u8 get_if() const { return m_io[0x0F]; }
  void set_if(u8 value) { m_io[0x0F] = value; }

  void tick(u32 cycles);

  void request_interrupt(u8 bit);

  //   void set_joypad_state(u8 directions, u8 buttons) {
  //     // Active-low
  //     // 1 -> released
  //     // 0 -> pressed
  //     m_joypad_directions = directions & 0x0F;
  //     m_joypad_buttons = buttons & 0x0F;
  //   }

  void set_joypad_state(u8 directions, u8 buttons);

  void ppu_set_stat(u8 value) { m_io[0x41] = value; }

  u8 timer_bit() const {
    switch (m_io[0x07] & 0x03) {
    case 0:
      return 9;
    case 1:
      return 3;
    case 2:
      return 5;

    default:
      return 7;
    }
  }

  bool timer_input() const {
    const bool enabled = (m_io[0x07] & 0x04) != 0;

    if (!enabled) {
      return false;
    }

    return ((m_div_counter >> timer_bit()) & 1) != 0;
  }

  void increment_tima();
  void complete_tima_reload();

  void ppu_set_cpu_access(bool vram_accessible, bool oam_accessible) {
    m_cpu_vram_accessible = vram_accessible;
    m_cpu_oam_accessible = oam_accessible;
  }

  void save_battery_ram();

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

  u16 m_div_counter{};
  u8 m_tima_reload_delay{};
  TimaReloadPhase m_tima_reload_phase{TimaReloadPhase::None};

  bool m_cpu_vram_accessible{true};
  bool m_cpu_oam_accessible{true};

  [[nodiscard]] u8 joypad_input() const;
  void check_joypad_falling_edge(u8 old_input);

  bool m_is_mbc1{false};

  // MBC1 registers
  bool m_ram_enabled{false};
  u8 m_rom_bank_low5{1};
  u8 m_bank_high2{0};
  bool m_banking_mode{false};

  std::vector<u8> m_external_ram;

  void init_cartrigde();

  [[nodiscard]] u8 read_cartridge_rom(u16 addr) const;
  void write_cartridge_control(u16 addr, u8 value);

  [[nodiscard]] u8 read_cartridge_ram(u16 addr) const;
  void write_cartridge_ram(u16 addr, u8 value);

  void load_battery_ram();

  bool m_has_battery{false};
  bool m_ram_dirty{false};
  std::string m_save_path;

  // MCB1MB support
  bool m_mbc1_multicart{false};

  [[nodiscard]] bool has_valid_header_checksum(std::size_t base) const;

  [[nodiscard]] bool detect_mbc1_multicart() const;

  // MBC5
  bool m_is_mbc5{false};

  u16 m_mbc5_rom_bank{1};
  u8 m_mbc5_ram_bank{0};

  // MBC3
  bool m_is_mbc3{false};
  bool m_mbc3_has_rtc{false};

  u8 m_mbc3_rom_bank{1};
  u8 m_mbc3_ram_rtc_select{0};
};
