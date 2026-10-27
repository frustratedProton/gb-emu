#include "Bus.hpp"
#include "types.hpp"
#include <cstddef>
#include <fstream>
#include <iostream>

u8 Bus::cpu_read(u16 addr) const {
  if (addr >= 0x8000 && addr <= 0x9FFF && !m_cpu_vram_accessible) {
    return 0xFF;
  }

  if (addr >= 0xFE00 && addr <= 0xFE9F && !m_cpu_oam_accessible) {
    return 0xFF;
  }

  return read(addr);
}

void Bus::load_battery_ram() {
  if (!m_has_battery || m_save_path.empty() || m_external_ram.empty()) {
    return;
  }

  std::ifstream file(m_save_path, std::ios::binary);

  if (!file) {
    return;
  }

  file.read(reinterpret_cast<char *>(m_external_ram.data()),
            static_cast<std::streamsize>(m_external_ram.size()));
}

void Bus::save_battery_ram() {
  if (!m_has_battery || !m_ram_dirty || m_save_path.empty() ||
      m_external_ram.empty()) {
    return;
  }

  std::ofstream file(m_save_path, std::ios::binary | std::ios::trunc);

  if (!file) {
    return;
  }

  file.write(reinterpret_cast<const char *>(m_external_ram.data()),
             static_cast<std::streamsize>(m_external_ram.size()));

  if (file) {
    m_ram_dirty = false;
  }
}

void Bus::init_cartrigde() {
  if (m_rom.size() <= 0x0147) {
    return;
  }

  const u8 cartridge_type = m_rom[0x0147];

  m_is_mbc1 = cartridge_type == 0x01 || cartridge_type == 0x02 ||
              cartridge_type == 0x03;
  m_mbc1_multicart = detect_mbc1_multicart();

  m_has_battery = cartridge_type == 0x03;

  if (!m_is_mbc1 || m_rom.size() <= 0x0149) {
    return;
  }

  const u8 ram_size = m_rom[0x0149];

  std::size_t ram_bytes = 0;

  switch (ram_size) {
  case 0x00:
    ram_bytes = 0;
    break;
  case 0x01:
    ram_bytes = 0x0800;
    break;
  case 0x02:
    ram_bytes = 0x2000;
    break;
  case 0x03:
    ram_bytes = 0x8000;
    break;
  default:
    ram_bytes = 0;
    break;
  }

  m_external_ram.resize(ram_bytes);
  load_battery_ram();
}

u8 Bus::read_cartridge_rom(u16 addr) const {
  if (m_rom.empty())
    return 0xFF;

  if (!m_is_mbc1)
    return static_cast<std::size_t>(addr) < m_rom.size() ? m_rom.at(addr)
                                                         : 0xFF;

  const std::size_t rom_bank_count = (m_rom.size() + 0x3FFF) / 0x4000;

  if (rom_bank_count == 0)
    return 0xFF;

  std::size_t bank = 0;

  if (m_mbc1_multicart) {
    if (addr < 0x4000) {
      bank = m_banking_mode ? static_cast<std::size_t>(m_bank_high2) << 4 : 0;
    } else {
      bank = (static_cast<std::size_t>(m_bank_high2) << 4) |
             (m_rom_bank_low5 & 0x0F);
    }
  } else {
    if (addr < 0x4000) {
      bank = m_banking_mode ? static_cast<std::size_t>(m_bank_high2) << 5 : 0;
    } else {
      bank = (static_cast<std::size_t>(m_bank_high2) << 5) | m_rom_bank_low5;
    }
  }

  bank %= rom_bank_count;

  const std::size_t offset = static_cast<std::size_t>(addr & 0x3FFF);

  const std::size_t index = bank * 0x4000 + offset;

  return index < m_rom.size() ? m_rom.at(index) : 0xFF;
}

void Bus::write_cartridge_control(u16 addr, u8 value) {
  if (!m_is_mbc1)
    return;

  if (addr <= 0x1FFF) {
    // RAM enable requires low nibble 0xA
    m_ram_enabled = (value & 0x0F) == 0x0A;
    return;
  }

  if (addr <= 0x3FFF) {
    m_rom_bank_low5 = static_cast<u8>(value & 0x1F);

    if (m_rom_bank_low5 == 0)
      m_rom_bank_low5 = 1;

    return;
  }

  if (addr <= 0x5FFF) {
    // upper two ROM bank bits,
    // or RAM bank number
    // when in RAM bank mode

    m_bank_high2 = static_cast<u8>(value & 0x03);
    return;
  }

  m_banking_mode = (value & 0x01) != 0;
}

u8 Bus::read_cartridge_ram(u16 addr) const {
  if (!m_is_mbc1 || !m_ram_enabled || m_external_ram.empty()) {
    return 0xFF;
  }

  const std::size_t ram_bank =
      m_banking_mode ? static_cast<std::size_t>(m_bank_high2) : 0;

  const std::size_t offset = static_cast<std::size_t>(addr - 0xA000);

  const std::size_t index =
      (ram_bank * 0x2000 + offset) % m_external_ram.size();

  return m_external_ram.at(index);
}

void Bus::write_cartridge_ram(u16 addr, u8 value) {
  if (!m_is_mbc1 || !m_ram_enabled || m_external_ram.empty()) {
    return;
  }

  const std::size_t ram_bank =
      m_banking_mode ? static_cast<std::size_t>(m_bank_high2) : 0;

  const std::size_t offset = static_cast<std::size_t>(addr - 0xA000);

  const std::size_t index =
      (ram_bank * 0x2000 + offset) % m_external_ram.size();

  m_external_ram.at(index) = value;
  m_ram_dirty = true;
}

bool Bus::has_valid_header_checksum(std::size_t base) const {
  if (base + 0x014D >= m_rom.size()) {
    return false;
  }

  u8 checksum = 0;

  for (std::size_t i = 0x0134; i <= 0x014C; ++i) {
    checksum = static_cast<u8>(checksum - m_rom[base + i] - 1);
  }

  return checksum == m_rom[base + 0x014D];
}

bool Bus::detect_mbc1_multicart() const {
  // MBC1M multicarts are 1 MiB MBC1 cartridges.
  if (!m_is_mbc1 || m_rom.size() != 0x100000) {
    return false;
  }

  int valid_slots = 0;

  // Check the 256 KiB game slots after the menu.
  for (std::size_t slot = 1; slot <= 3; ++slot) {
    const std::size_t base = slot * 0x40000;

    if (has_valid_header_checksum(base)) {
      ++valid_slots;
    }
  }

  return valid_slots >= 2;
}

u8 Bus::read(u16 addr) const {
  // Cartridge ROM
  if (addr <= 0x7FFF) {
    return read_cartridge_rom(addr);
  }

  // Video RAM
  if (addr <= 0x9FFF) {
    return m_vram.at(addr - 0x8000);
  }

  // External cartridge RAM; not supported yet
  if (addr <= 0xBFFF) {
    return read_cartridge_ram(addr);
  }

  // Work RAM
  if (addr <= 0xDFFF) {
    return m_wram.at(addr - 0xC000);
  }

  // Echo RAM: mirror of 0xC000-0xDDFF
  if (addr <= 0xFDFF) {
    return m_wram.at(addr - 0xE000);
  }

  // OAM
  if (addr <= 0xFE9F) {
    return m_oam.at(addr - 0xFE00);
  }

  // Prohibited area
  if (addr <= 0xFEFF) {
    return 0xFF;
  }

  // I/O registers
  if (addr <= 0xFF7F) {
    if (addr == 0xFF41) {
      return static_cast<u8>(m_io[0x41] | 0x80);
    }

    if (addr == 0xFF00) {
      const u8 select = m_io[0x00] & 0x30;
      const u8 input = joypad_input();

      return static_cast<u8>(0xC0 | select | input);
    }

    return m_io.at(addr - 0xFF00); // LY and STAT now come from IO array
                                   // PPU writes to them directly
  }

  // High RAM
  if (addr <= 0xFFFE) {
    return m_hram.at(addr - 0xFF80);
  }

  // The only remaining u16 address is 0xFFFF
  return m_ie;
}

void Bus::cpu_write(u16 addr, u8 value) {
  const bool lcd_enabled = (m_io[0x40] & 0x80) != 0;
  const u8 mode = m_io[0x41] & 0x03;

  if (lcd_enabled) {
    // VRAM writes are blocked during Mode 3.
    if (addr >= 0x8000 && addr <= 0x9FFF && mode == 3) {
      return;
    }

    // OAM writes are blocked during Modes 2 and 3.
    if (addr >= 0xFE00 && addr <= 0xFE9F && (mode == 2 || mode == 3)) {
      return;
    }
  }

  write(addr, value);
}

void Bus::write(u16 addr, u8 value) {
  // cartridge ROM is read-only for ROM-only cartridges
  if (addr <= 0x7FFF) {
    write_cartridge_control(addr, value);
    return;
  }

  if (addr <= 0x9FFF) {
    m_vram.at(addr - 0x8000) = value;
    return;
  }

  // external cartridge RAM is unsupported for now
  if (addr <= 0xBFFF) {
    write_cartridge_ram(addr, value);
    return;
  }

  if (addr <= 0xDFFF) {
    m_wram.at(addr - 0xC000) = value;
    return;
  }

  // Echo RAM writes modify the corresponding WRAM byte
  if (addr <= 0xFDFF) {
    m_wram.at(addr - 0xE000) = value;
    return;
  }

  if (addr <= 0xFE9F) {
    m_oam.at(addr - 0xFE00) = value;
    return;
  }

  if (addr <= 0xFEFF)
    return;

  if (addr == 0xFF46) {
    const u16 source = static_cast<u16>(value) << 8;

    for (u16 i = 0; i < 0xA0; ++i) {
      m_oam.at(i) = read(static_cast<u16>(source + i));
    }

    return;
  }

  // IO
  if (addr <= 0xFF7F) {
    if (addr == 0xFF05) {
      if (m_tima_reload_phase == TimaReloadPhase::Reload) {
        // TIMA writes are ignored during the reload M-cycle.
        return;
      }

      // A TIMA write during the reload window cancels the reload.
      if (m_tima_reload_phase == TimaReloadPhase::Overflow) {
        m_tima_reload_phase = TimaReloadPhase::None;
        m_tima_reload_delay = 0;
      }

      m_io[0x05] = value;
      return;
    }

    if (addr == 0xFF00) {
      const u8 old_input = joypad_input();

      // Only bits 4 and 5 are writable.
      m_io[0x00] = static_cast<u8>((m_io[0x00] & 0xCF) | (value & 0x30));

      check_joypad_falling_edge(old_input);
      return;
    }

    // writing any value to DIV resets it to 0
    if (addr == 0xFF04) {
      //   m_io.at(0x04) = 0;
      //   m_div_cycles = 0;
      //   return;

      const bool old_timer_signal = timer_input();

      m_div_counter = 0;
      m_io[0x04] = 0;

      const bool new_timer_signal = timer_input();

      if (old_timer_signal && !new_timer_signal) {
        increment_tima();
      }

      return;
    }

    // serial transfer - SC write of 0x81 means "start transfer now"
    // SB (0xFF01) holds the byte to send
    // SC (0xFF02) is the control register
    //
    // Bit 7: start transfer
    // Bit 0: use internal clock
    // if (addr == 0xFF02 && value == 0x81) {
    //   std::cout << static_cast<char>(m_io.at(0x01)) << std::flush;
    //   m_io.at(0x02) = static_cast<u8>(value & 0x7F);
    // }

    if (addr == 0xFF02) {
      m_io[0x02] = value;

      // Serial transfer: SC write of 0x81 starts transfer immediately.
      if (value == 0x81) {
        std::cout << static_cast<char>(m_io[0x01]) << std::flush;

        // Transfer completes immediately in this emulator.
        m_io[0x02] = static_cast<u8>(value & 0x7F);
      }

      return;
    }

    if (addr == 0xFF07) {
      const u8 old_tac = m_io[0x07];
      const bool old_timer_signal = timer_input();

      m_io[0x07] = value;

      const bool new_timer_signal = timer_input();
      const bool falling_edge = old_timer_signal && !new_timer_signal;

      if (falling_edge) {
        increment_tima();
      }

      return;
    }

    if (addr == 0xFF41) {
      // cpu can write bits 3-6 only
      // bit 0-2 are controlled by ppu
      m_io[0x41] = static_cast<u8>((m_io[0x41] & 0x87) | (value & 0x78));
      return;
    }

    if (addr == 0xFF06) {
      m_io[0x06] = value;
      if (m_tima_reload_phase == TimaReloadPhase::Reload) {
        m_io[0x05] = value;
      }

      return;
    }

    m_io.at(addr - 0xFF00) = value;
    return;
  }

  if (addr <= 0xFFFE) {
    m_hram.at(addr - 0xFF80) = value;
    return;
  }

  m_ie = value;
}

// void Bus::set_joypad_state(u8 directions, u8 buttons) {
//   // Active-low
//   // 1 -> released
//   // 0 -> pressed
//   m_joypad_directions = directions & 0x0F;
//   m_joypad_buttons = buttons & 0x0F;
// }

u8 Bus::joypad_input() const {
  const u8 select = m_io[0x00] & 0x30;

  u8 input = 0x0F;

  // Bit 4 / P14 low selects directions.
  if ((select & 0x10) == 0) {
    input &= m_joypad_directions;
  }

  // Bit 5 / P15 low selects action buttons.
  if ((select & 0x20) == 0) {
    input &= m_joypad_buttons;
  }

  return input;
}

void Bus::check_joypad_falling_edge(u8 old_input) {
  const u8 new_input = joypad_input();

  const u8 falling_edges =
      static_cast<u8>(old_input & static_cast<u8>(~new_input));

  if ((falling_edges & 0x0F) != 0) {
    request_interrupt(4);
  }
}

void Bus::set_joypad_state(u8 directions, u8 buttons) {
  const u8 old_input = joypad_input();

  m_joypad_directions = directions & 0x0F;
  m_joypad_buttons = buttons & 0x0F;

  check_joypad_falling_edge(old_input);
}

void Bus::complete_tima_reload() {
  if (m_tima_reload_phase != TimaReloadPhase::Reload) {
    return;
  }

  m_io[0x05] = m_io[0x06];
  request_interrupt(2);

  m_tima_reload_phase = TimaReloadPhase::None;
  m_tima_reload_delay = 0;
}

void Bus::tick(u32 cycles) {
  // The CPU access for the previous Reload phase happened immediately
  // before this call. Close that phase now.
  if (m_tima_reload_phase == TimaReloadPhase::Reload) {
    m_tima_reload_phase = TimaReloadPhase::None;
    m_tima_reload_delay = 0;
  }

  for (u32 i = 0; i < cycles; ++i) {
    if (m_tima_reload_phase == TimaReloadPhase::Overflow) {
      if (m_tima_reload_delay > 0) {
        --m_tima_reload_delay;
      }

      if (m_tima_reload_delay == 0) {
        m_tima_reload_phase = TimaReloadPhase::Reload;

        m_io[0x05] = m_io[0x06];
        request_interrupt(2);
      }
    }

    const bool old_timer_signal = timer_input();

    ++m_div_counter;
    m_io[0x04] = static_cast<u8>(m_div_counter >> 8);

    const bool new_timer_signal = timer_input();

    if (old_timer_signal && !new_timer_signal) {
      increment_tima();
    }
  }
}

void Bus::increment_tima() {
  if (m_tima_reload_phase != TimaReloadPhase::None) {
    return;
  }

  if (m_io[0x05] == 0xFF) {
    m_io[0x05] = 0x00;

    // TIMA remains 00 during the overflow phase
    m_tima_reload_phase = TimaReloadPhase::Overflow;
    m_tima_reload_delay = 4;
  } else {
    ++m_io[0x05];
  }
}

void Bus::request_interrupt(u8 bit) {
  m_io.at(0x0F) |= static_cast<u8>(1 << bit);
}