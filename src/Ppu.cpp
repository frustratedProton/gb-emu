#include "Ppu.hpp"
#include "Bus.hpp"
#include "types.hpp"
#include <iostream>

bool Ppu::tick(u32 cycles) {
  // check if LCD is enabled
  // by checking LCDC bit 7
  const u8 lcdc = m_bus.read(0xFF40);

  if ((lcdc & 0x80) == 0) {
    return false; // LCD false, return
  }

  m_cycles += cycles;
  bool frame_ready = false;

  switch (m_mode) {
  case Mode::OAMScan: {
    // 80 cycles then move to drawing
    if (m_cycles >= 80) {
      m_cycles -= 80;
      m_mode = Mode::Drawing;
    }
    break;
  }

  case Mode::Drawing: {
    // 172 cycles then render and move to HBlank
    if (m_cycles >= 172) {
      m_cycles -= 172;

      render_scanline();

      m_mode = Mode::HBlank;

      // update STAT mode bits
      u8 stat = m_bus.read(0xFF41);
      stat = (stat & 0xFC) | static_cast<u8>(Mode::HBlank);
      m_bus.write(0xFF41, stat);
    }
    break;
  }

  case Mode::HBlank: {
    // 204 cycles then move to scanline
    if (m_cycles >= 204) {
      m_cycles -= 204;
      m_ly++;

      m_bus.write(0xFF44, m_ly);

      if (m_ly == 144) {
        m_mode = Mode::VBlank;
        m_bus.request_interrupt(0); // VBlank interrupt

        u8 stat = m_bus.read(0xFF41);
        stat = (stat & 0xFC) | static_cast<u8>(Mode::VBlank);
        m_bus.write(0xFF41, stat);

        frame_ready = true;
      } else {
        m_mode = Mode::OAMScan;

        u8 stat = m_bus.read(0xFF41);
        stat = (stat & 0xFC) | static_cast<u8>(Mode::OAMScan);
        m_bus.write(0xFF41, stat);
      }
    }
    break;
  }

  case Mode::VBlank: {
    // 10 lines of VBlank, each line is 456 cycles
    if (m_cycles >= 456) {
      m_cycles -= 456;
      m_ly++;

      m_bus.write(0xFF44, m_ly);

      if (m_ly == 154) {
        m_ly = 0;
        m_bus.write(0xFF44, 0);
        m_mode = Mode::OAMScan;

        u8 stat = m_bus.read(0xFF41);
        stat = (stat & 0xFC) | static_cast<u8>(Mode::OAMScan);
        m_bus.write(0xFF41, stat);
      }
    }
    break;
  }
  }

  return frame_ready;
}

void Ppu::render_scanline() {
  const u8 lcdc = m_bus.read(0xFF40);

  std::cerr << "render LY=" << std::dec << (int)m_ly << " LCDC=0x" << std::hex
            << (int)lcdc << '\n';

  if (lcdc & 0x01) {
    render_background_scanline(m_ly);
  }
}

void Ppu::render_background_scanline(u8 ly) {

  if (ly == 0) {
    const u8 lcdc = m_bus.read(0xFF40);
    const u16 map_base = (lcdc & 0x08) ? 0x9C00 : 0x9800;

    std::cerr << "tile map sample: ";
    for (int i = 0; i < 8; i++) {
      std::cerr << std::hex << (int)m_bus.read(map_base + i) << " ";
    }
    std::cerr << '\n';

    std::cerr << "VRAM 0x8000 sample: ";
    for (int i = 0; i < 8; i++) {
      std::cerr << std::hex << (int)m_bus.read(0x8000 + i) << " ";
    }
    std::cerr << '\n';
  }

  const u8 lcdc = m_bus.read(0xFF40);
  const u8 scx = m_bus.read(0xFF43);
  const u8 scy = m_bus.read(0xFF42);
  const u8 palette = m_bus.read(0xFF47);

  // which itle map to use
  const u16 map_base = (lcdc & 0x08) ? 0x9C00 : 0x9800;

  // which tile data to use
  // true  = 0x8000 method, tile IDs are unsigned (0-255)
  // false = 0x8800 method, tile IDs are signed (-128 to 127)
  const bool use_unsigned = (lcdc & 0x10) != 0;

  const u8 map_y = static_cast<u8>(ly + scy);

  const u8 tile_row = map_y / 8;

  const u8 tile_pixel_y = map_y % 8;

  for (int x = 0; x < GB_WIDTH; x++) {
    const u8 map_x = static_cast<u8>(x + scx);
    const u8 tile_col = map_x / 8;
    const u8 tile_pixel_x = map_x % 8;

    const u16 map_addr = map_base + tile_row * 32 + tile_col;
    const u8 tile_id = m_bus.read(map_addr);

    const u8 color_id =
        get_tile_pixel(tile_id, tile_pixel_x, tile_pixel_y, !use_unsigned);
    m_framebuffer[ly * GB_WIDTH + x] = get_color(color_id, palette);
  }
}

u8 Ppu::get_tile_pixel(u8 tile_id, u8 tile_x, u8 tile_y,
                       bool use_signed_addressing) const {
  u16 tile_addr{};

  if (use_signed_addressing) {
    // 0x8800 method: tile ID is signed, base is 0x9000
    const i8 signed_id = static_cast<i8>(tile_id);
    tile_addr = static_cast<u16>(0x9000 + signed_id * 16);
  } else {
    // 0x8000 method: tile ID is unsigned, base is 0x8000
    tile_addr = static_cast<u16>(0x8000 + tile_id * 16);
  }

  // each row is 2 bytes
  const u16 row_addr = tile_addr + tile_y * 2;

  const u8 low = m_bus.read(row_addr);
  const u8 high = m_bus.read(row_addr + 1);

  // bit 7 is the leftmost pixel
  const u8 bit = 7 - tile_x;

  const u8 lo_bit = (low >> bit) & 0x01;
  const u8 hi_bit = (high >> bit) & 0x01;

  return static_cast<u8>((hi_bit << 1) | lo_bit);
}

Color Ppu::get_color(u8 color_id, u8 palette) const {
  // each color ID uses 2 bits of the palette register
  const u8 shade = (palette >> (color_id * 2)) & 0x03;
  return GB_COLORS[shade];
}
