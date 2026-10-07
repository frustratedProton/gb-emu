#include "Ppu.hpp"
#include "Bus.hpp"
#include "types.hpp"

Ppu::Ppu(Bus &bus) : m_bus(bus) {
  m_bus.write(0xFF44, 0);
  set_mode(Mode::HBlank);
}

void Ppu::set_mode(Mode mode) {
  m_mode = mode;

  u8 stat = m_bus.read(0xFF41);
  stat = static_cast<u8>((stat & 0xFC) | static_cast<u8>(mode));

  m_bus.write(0xFF41, stat);
}

bool Ppu::tick(u32 cycles) {
  // check if LCD is enabled
  // by checking LCDC bit 7
  const u8 lcdc = m_bus.read(0xFF40);
  const bool lcd_enabled = (lcdc & 0x80) != 0;

  if (!lcd_enabled) {
    if (m_lcd_enabled) {
      // LCD was just disabled. Reset PPU timing.
      m_lcd_enabled = false;
      m_cycles = 0;
      m_ly = 0;

      m_bus.write(0xFF44, 0);
      set_mode(Mode::HBlank);
    }

    return false;
  }

  if (!m_lcd_enabled) {
    // LCD was just enabled. Start at LY=0, mode 2.
    m_lcd_enabled = true;
    m_cycles = 0;
    m_ly = 0;

    m_bus.write(0xFF44, 0);
    set_mode(Mode::OAMScan);
  }

  //   if ((lcdc & 0x80) == 0) {
  //     return false; // LCD false, return
  //   }

  m_cycles += cycles;
  bool frame_ready = false;

  switch (m_mode) {
  case Mode::OAMScan: {
    // 80 cycles then move to drawing
    if (m_cycles >= 80) {
      m_cycles -= 80;
      set_mode(Mode::Drawing);
    }
    break;
  }

  case Mode::Drawing: {
    // 172 cycles then render and move to HBlank
    if (m_cycles >= 172) {
      m_cycles -= 172;

      render_scanline();
      set_mode(Mode::HBlank);
    }
    break;
  }

  case Mode::HBlank: {
    // 204 cycles then move to scanline
    if (m_cycles >= 204) {
      m_cycles -= 204;
      ++m_ly;

      m_bus.write(0xFF44, m_ly);

      if (m_ly == 144) {
        set_mode(Mode::VBlank);
        m_bus.request_interrupt(0);
        frame_ready = true;
      } else {
        set_mode(Mode::OAMScan);
      }
    }
    break;
  }

  case Mode::VBlank: {
    // 10 lines of VBlank, each line is 456 cycles
    if (m_cycles >= 456) {
      m_cycles -= 456;
      ++m_ly;

      m_bus.write(0xFF44, m_ly);

      if (m_ly == 154) {
        m_ly = 0;
        m_bus.write(0xFF44, 0);
        set_mode(Mode::OAMScan);
      }
    }
    break;
  }
  }

  return frame_ready;
}

void Ppu::render_scanline() {
  const u8 lcdc = m_bus.read(0xFF40);
  const u8 palette = m_bus.read(0xFF47);

  const Color background_color = get_color(0, palette);

  const auto begin = m_framebuffer.begin() + m_ly * GB_WIDTH;
  const auto end = begin + GB_WIDTH;

  std::fill(begin, end, background_color);

  if (lcdc & 0x01) {
    render_background_scanline(m_ly);
  }

  if (lcdc & 0x02) {
    render_sprites_scanline(m_ly);
  }
}

void Ppu::render_background_scanline(u8 ly) {

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

void Ppu::render_sprites_scanline(u8 ly) {
  const u8 lcdc = m_bus.read(0xFF40);
  const int sprite_height = (lcdc & 0x04) ? 16 : 8;

  // DMG allows at most 10 sprites on one scanline.
  int sprites_on_line = 0;

  // Lower OAM index has priority over higher OAM index.
  std::array<bool, GB_WIDTH> pixel_used{};

  for (int sprite_index = 0; sprite_index < 40 && sprites_on_line < 10;
       ++sprite_index) {

    const u16 oam_address = static_cast<u16>(0xFE00 + sprite_index * 4);

    const int sprite_y = static_cast<int>(m_bus.read(oam_address)) - 16;

    const int sprite_x = static_cast<int>(m_bus.read(oam_address + 1)) - 8;

    u8 tile_id = m_bus.read(oam_address + 2);
    const u8 attributes = m_bus.read(oam_address + 3);

    const int row_in_sprite = static_cast<int>(ly) - sprite_y;

    if (row_in_sprite < 0 || row_in_sprite >= sprite_height) {
      continue;
    }

    ++sprites_on_line;

    int row = row_in_sprite;

    if (attributes & 0x40) {
      row = sprite_height - 1 - row;
    }

    // Sprite tile data always uses unsigned 0x8000 addressing
    if (sprite_height == 16) {
      tile_id &= 0xFE;
      tile_id = static_cast<u8>(tile_id + row / 8);
      row %= 8;
    }

    const u16 tile_address = static_cast<u16>(0x8000 + tile_id * 16 + row * 2);

    const u8 low = m_bus.read(tile_address);
    const u8 high = m_bus.read(tile_address + 1);

    const u8 palette = (attributes & 0x10) ? m_bus.read(0xFF49)  // OBP1
                                           : m_bus.read(0xFF48); // OBP0

    for (int pixel = 0; pixel < 8; ++pixel) {
      const int screen_x = sprite_x + pixel;

      if (screen_x < 0 || screen_x >= GB_WIDTH) {
        continue;
      }

      if (pixel_used[screen_x]) {
        continue;
      }

      const int tile_x = (attributes & 0x20) ? 7 - pixel : pixel;

      const u8 bit = static_cast<u8>(7 - tile_x);

      const u8 lo_bit = static_cast<u8>((low >> bit) & 1);
      const u8 hi_bit = static_cast<u8>((high >> bit) & 1);

      const u8 color_id = static_cast<u8>((hi_bit << 1) | lo_bit);

      // Sprite color 0 is transparent.
      if (color_id == 0) {
        continue;
      }

      m_framebuffer[ly * GB_WIDTH + screen_x] = get_color(color_id, palette);

      pixel_used[screen_x] = true;
    }
  }
}

Color Ppu::get_color(u8 color_id, u8 palette) const {
  // each color ID uses 2 bits of the palette register
  const u8 shade = (palette >> (color_id * 2)) & 0x03;
  return GB_COLORS[shade];
}
