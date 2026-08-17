#include "types.hpp"
#include "Bus.hpp"

u8 Bus::read(u16 addr) const {
  // Cartridge ROM
  if (addr <= 0x7FFF) {
    if (static_cast<std::size_t>(addr) < m_rom.size())
      return m_rom.at(addr);

    return 0xFF;
  }

  // Video RAM
  if (addr <= 0x9FFF) {
    return m_vram.at(addr - 0x8000);
  }

  // External cartridge RAM; not supported yet
  if (addr <= 0xBFFF) {
    return 0xFF;
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
    return m_io.at(addr - 0xFF00);
  }

  // High RAM
  if (addr <= 0xFFFE) {
    return m_hram.at(addr - 0xFF80);
  }

  // The only remaining u16 address is 0xFFFF
  return m_ie;
}

void Bus::write(u16 addr, u8 value) {
  // cartridge ROM is read-only for ROM-only cartridges
  if (addr <= 0x7FFF) {
    return;
  }

  if (addr <= 0x9FFF) {
    m_vram.at(addr - 0x8000) = value;
    return;
  }

  // external cartridge RAM is unsupported for now
  if (addr <= 0xBFFF) {
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

  if (addr <= 0xFEFF) {
    return;
  }

  if (addr <= 0xFF7F) {
    m_io.at(addr - 0xFF00) = value;
    return;
  }

  if (addr <= 0xFFFE) {
    m_hram.at(addr - 0xFF80) = value;
    return;
  }

  m_ie = value;
}
