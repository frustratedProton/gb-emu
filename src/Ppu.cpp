#include "Ppu.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>

Ppu::Ppu(Bus &bus) : m_bus(bus) {
  m_bus.write(0xFF44, 0);

  // LCD is initially off, so report mode 0
  const u8 stat = m_bus.read(0xFF41);
  m_bus.ppu_set_stat(static_cast<u8>(stat & 0xFC));
}

void Ppu::set_mode(Mode mode) {
  m_mode = mode;
  update_stat();
  update_cpu_access();
}

void Ppu::update_stat() {
  // STAT/LYC state is latched while LCD is disabled
  if (!m_lcd_enabled) {
    return;
  }

  u8 stat = m_bus.read(0xFF41);

  const u8 lyc = m_bus.read(0xFF45);
  const bool coincidence = m_ly == lyc;

  // STAT bit 2: LYC == LY
  if (coincidence) {
    stat |= 0x04;
  } else {
    stat &= static_cast<u8>(~0x04);
  }

  // STAT bits 0-1: current PPU mode
  stat = static_cast<u8>((stat & 0xFC) | static_cast<u8>(m_mode));

  // PPU updates the read-only STAT bits
  m_bus.ppu_set_stat(stat);

  // STAT interrupt
  const bool lyc_interrupt = (stat & 0x40) != 0 && coincidence;
  const bool mode2_interrupt = (stat & 0x20) != 0 && m_mode == Mode::OAMScan;
  const bool mode1_interrupt = (stat & 0x10) != 0 && m_mode == Mode::VBlank;
  const bool mode0_interrupt = (stat & 0x08) != 0 && m_mode == Mode::HBlank;
  const bool stat_line =
      lyc_interrupt || mode2_interrupt || mode1_interrupt || mode0_interrupt;

  // STAT interrupts happen on a rising edge
  if (stat_line && !m_stat_irq_line) {
    m_bus.request_interrupt(1);
  }

  m_stat_irq_line = stat_line;
}

bool Ppu::tick(u32 cycles) {
  const u8 lcdc = m_bus.read(0xFF40);
  const bool lcd_enabled = (lcdc & 0x80) != 0;

  // LCD disabled
  if (!lcd_enabled) {
    if (m_lcd_enabled) {
      m_lcd_enabled = false;

      m_cycles = 0;
      m_ly = 0;
      m_window_line = 0;

      m_lcd_on_line0 = false;
      m_line0_mode0_phase = false;

      // STAT/LYC state remains latched while LCD is off
      m_mode = Mode::HBlank;

      m_bus.write(0xFF44, 0);

      const u8 stat = m_bus.read(0xFF41);
      m_bus.ppu_set_stat(static_cast<u8>(stat & 0xFC));
    }

    return false;
  }

  // LCD has just been enabled
  if (!m_lcd_enabled) {
    m_lcd_enabled = true;

    m_cycles = 0;
    m_ly = 0;
    m_window_line = 0;

    m_lcd_on_line0 = true;
    m_line0_mode0_phase = true;

    m_bus.write(0xFF44, 0);

    set_mode(Mode::HBlank);
  }

  m_cycles += cycles;

  bool frame_ready = false;

  for (;;) {
    switch (m_mode) {
    case Mode::OAMScan:
      if (m_cycles < 80) {
        update_stat();
        update_cpu_access();
        return frame_ready;
      }

      m_cycles -= 80;
      set_mode(Mode::Drawing);
      break;

    case Mode::Drawing: {
      const u32 drawing_cycles = m_lcd_on_line0 ? 174 : 172;

      if (m_cycles < drawing_cycles) {
        update_stat();
        update_cpu_access();
        return frame_ready;
      }

      m_cycles -= drawing_cycles;

      render_scanline();
      set_mode(Mode::HBlank);
      break;
    }

    case Mode::HBlank: {
      u32 hblank_cycles = 204;

      if (m_line0_mode0_phase) {
        // The initial LCD-on Mode 0 occupies the normal OAM-scan
        // duration, but no OAM scan occurs.
        hblank_cycles = 80;
      } else if (m_lcd_on_line0) {
        // Line 0 has a 2-cycle longer Mode 3 and a 2-cycle shorter
        // HBlank, keeping the line at 456 cycles.
        hblank_cycles = 202;
      }

      if (m_cycles < hblank_cycles) {
        update_stat();
        update_cpu_access();
        return frame_ready;
      }

      m_cycles -= hblank_cycles;

      if (m_line0_mode0_phase) {
        m_line0_mode0_phase = false;
        set_mode(Mode::Drawing);
        break;
      }

      ++m_ly;
      m_bus.write(0xFF44, m_ly);

      m_lcd_on_line0 = false;

      if (m_ly == 144) {
        const u8 stat = m_bus.read(0xFF41);
        const bool mode2_stat_enabled = (stat & 0x20) != 0;

        set_mode(Mode::VBlank);

        if (mode2_stat_enabled) {
          m_bus.request_interrupt(1);
        }

        m_bus.request_interrupt(0);
        frame_ready = true;
      } else {
        set_mode(Mode::OAMScan);
      }

      break;
    }

    case Mode::VBlank:
      if (m_cycles < 456) {
        update_stat();
        update_cpu_access();
        return frame_ready;
      }

      m_cycles -= 456;
      ++m_ly;

      if (m_ly == 154) {
        m_ly = 0;
        m_window_line = 0;

        m_bus.write(0xFF44, 0);
        set_mode(Mode::OAMScan);
      } else {
        m_bus.write(0xFF44, m_ly);
        update_stat();
      }

      break;
    }
  }

  update_stat();

  return frame_ready;
}

void Ppu::render_scanline() {
  const u8 lcdc = m_bus.read(0xFF40);
  const u8 palette = m_bus.read(0xFF47);

  const Color background_color = get_color(0, palette);

  const auto framebuffer_begin = m_framebuffer.begin() + m_ly * GB_WIDTH;

  const auto framebuffer_end = framebuffer_begin + GB_WIDTH;

  const auto bg_ids_begin = m_bg_color_ids.begin() + m_ly * GB_WIDTH;

  const auto bg_ids_end = bg_ids_begin + GB_WIDTH;

  // Fill the scanline with color 0
  std::fill(framebuffer_begin, framebuffer_end, background_color);

  std::fill(bg_ids_begin, bg_ids_end, 0);

  // Background
  if (lcdc & 0x01) {
    render_background_scanline(m_ly);
  }

  // LCDC bit 0 controls the DMG background and window display
  const bool bg_window_enabled = (lcdc & 0x01) != 0;

  // Window
  const u8 wy = m_bus.read(0xFF4A);
  const u8 wx = m_bus.read(0xFF4B);

  const bool window_visible =
      bg_window_enabled && (lcdc & 0x20) && m_ly >= wy && wx <= 166;

  if (window_visible) {
    render_window_scanline(m_ly);
    ++m_window_line;
  }

  // Sprites
  if (lcdc & 0x02) {
    render_sprites_scanline(m_ly);
  }
}

void Ppu::render_background_scanline(u8 ly) {
  const u8 lcdc = m_bus.read(0xFF40);
  const u8 scx = m_bus.read(0xFF43);
  const u8 scy = m_bus.read(0xFF42);
  const u8 palette = m_bus.read(0xFF47);
  const u16 map_base = (lcdc & 0x08) ? 0x9C00 : 0x9800;
  const bool use_unsigned = (lcdc & 0x10) != 0;
  const u8 map_y = static_cast<u8>(ly + scy);
  const u8 tile_row = static_cast<u8>(map_y / 8);
  const u8 tile_pixel_y = static_cast<u8>(map_y % 8);

  for (int x = 0; x < GB_WIDTH; ++x) {
    const u8 map_x = static_cast<u8>(x + scx);

    const u8 tile_col = static_cast<u8>(map_x / 8);

    const u8 tile_pixel_x = static_cast<u8>(map_x % 8);

    const u16 map_address =
        static_cast<u16>(map_base + tile_row * 32 + tile_col);

    const u8 tile_id = m_bus.read(map_address);

    const u8 color_id =
        get_tile_pixel(tile_id, tile_pixel_x, tile_pixel_y, !use_unsigned);

    const std::size_t index = static_cast<std::size_t>(ly) * GB_WIDTH + x;

    m_bg_color_ids[index] = color_id;
    m_framebuffer[index] = get_color(color_id, palette);
  }
}

void Ppu::render_window_scanline(u8 ly) {
  const u8 lcdc = m_bus.read(0xFF40);
  const u8 wx = m_bus.read(0xFF4B);
  const u8 palette = m_bus.read(0xFF47);
  const u16 map_base = (lcdc & 0x40) ? 0x9C00 : 0x9800;
  const bool use_unsigned = (lcdc & 0x10) != 0;
  const int window_left = static_cast<int>(wx) - 7;
  const u8 tile_row = static_cast<u8>(m_window_line / 8);
  const u8 tile_pixel_y = static_cast<u8>(m_window_line % 8);
  const int first_screen_x = std::max(0, window_left);

  for (int screen_x = first_screen_x; screen_x < GB_WIDTH; ++screen_x) {

    const int window_x = screen_x - window_left;

    const u8 tile_col = static_cast<u8>(window_x / 8);

    const u8 tile_pixel_x = static_cast<u8>(window_x % 8);

    const u16 map_address =
        static_cast<u16>(map_base + tile_row * 32 + tile_col);

    const u8 tile_id = m_bus.read(map_address);

    const u8 color_id =
        get_tile_pixel(tile_id, tile_pixel_x, tile_pixel_y, !use_unsigned);

    const std::size_t index =
        static_cast<std::size_t>(ly) * GB_WIDTH + screen_x;

    m_bg_color_ids[index] = color_id;
    m_framebuffer[index] = get_color(color_id, palette);
  }
}

u8 Ppu::get_tile_pixel(u8 tile_id, u8 tile_x, u8 tile_y,
                       bool use_signed_addressing) const {

  u16 tile_address{};

  if (use_signed_addressing) {
    const i8 signed_id = static_cast<i8>(tile_id);

    tile_address = static_cast<u16>(0x9000 + signed_id * 16);
  } else {
    tile_address = static_cast<u16>(0x8000 + tile_id * 16);
  }

  const u16 row_address = static_cast<u16>(tile_address + tile_y * 2);
  const u8 low = m_bus.read(row_address);
  const u8 high = m_bus.read(row_address + 1);
  const u8 bit = static_cast<u8>(7 - tile_x);
  const u8 low_bit = static_cast<u8>((low >> bit) & 1);
  const u8 high_bit = static_cast<u8>((high >> bit) & 1);

  return static_cast<u8>((high_bit << 1) | low_bit);
}

void Ppu::render_sprites_scanline(u8 ly) {
  const u8 lcdc = m_bus.read(0xFF40);
  const int sprite_height = (lcdc & 0x04) ? 16 : 8;

  struct VisibleSprite {
    int oam_index;
    int x;
    int y;
    u8 tile_id;
    u8 attributes;
  };

  // Hardware selects at most 10 sprites in OAM order.
  std::array<VisibleSprite, 10> sprites{};
  int sprite_count = 0;

  for (int sprite_index = 0; sprite_index < 40 && sprite_count < 10;
       ++sprite_index) {
    const u16 address = static_cast<u16>(0xFE00 + sprite_index * 4);

    const int sprite_y = static_cast<int>(m_bus.read(address)) - 16;
    const int sprite_x = static_cast<int>(m_bus.read(address + 1)) - 8;

    const int row_in_sprite = static_cast<int>(ly) - sprite_y;

    if (row_in_sprite < 0 || row_in_sprite >= sprite_height) {
      continue;
    }

    sprites[sprite_count++] = {
        sprite_index,
        sprite_x,
        sprite_y,
        m_bus.read(address + 2),
        m_bus.read(address + 3),
    };
  }

  // On DMG, lower X coordinate has priority.
  // If X is equal, lower OAM index has priority.
  std::stable_sort(sprites.begin(), sprites.begin() + sprite_count,
                   [](const VisibleSprite &a, const VisibleSprite &b) {
                     if (a.x != b.x) {
                       return a.x < b.x;
                     }

                     return a.oam_index < b.oam_index;
                   });

  // Tracks which screen pixels have already been claimed
  // by a higher-priority sprite.
  std::array<bool, GB_WIDTH> pixel_used{};

  for (int sprite_number = 0; sprite_number < sprite_count; ++sprite_number) {
    const VisibleSprite &sprite = sprites[sprite_number];

    int row = static_cast<int>(ly) - sprite.y;

    if (sprite.attributes & 0x40) {
      row = sprite_height - 1 - row;
    }

    u8 tile_id = sprite.tile_id;

    // 8x16 sprites use two consecutive tiles.
    // The low bit of the tile number is ignored.
    if (sprite_height == 16) {
      tile_id &= 0xFE;
      tile_id = static_cast<u8>(tile_id + row / 8);
      row %= 8;
    }

    const u16 tile_address = static_cast<u16>(0x8000 + tile_id * 16 + row * 2);

    const u8 low = m_bus.read(tile_address);
    const u8 high = m_bus.read(tile_address + 1);

    const u8 palette = (sprite.attributes & 0x10) ? m_bus.read(0xFF49)  // OBP1
                                                  : m_bus.read(0xFF48); // OBP0

    for (int pixel = 0; pixel < 8; ++pixel) {
      const int screen_x = sprite.x + pixel;

      if (screen_x < 0 || screen_x >= GB_WIDTH) {
        continue;
      }

      if (pixel_used[screen_x]) {
        continue;
      }

      const int tile_x = (sprite.attributes & 0x20) ? 7 - pixel : pixel;

      const u8 bit = static_cast<u8>(7 - tile_x);

      const u8 low_bit = static_cast<u8>((low >> bit) & 1);

      const u8 high_bit = static_cast<u8>((high >> bit) & 1);

      const u8 color_id = static_cast<u8>((high_bit << 1) | low_bit);

      // Color 0 is transparent and does not claim the pixel.
      if (color_id == 0) {
        continue;
      }

      // Claim the pixel before applying background priority.
      pixel_used[screen_x] = true;

      const std::size_t framebuffer_index =
          static_cast<std::size_t>(ly) * GB_WIDTH + screen_x;

      // Attribute bit 7: sprite is behind a non-zero
      // background/window pixel.
      if ((sprite.attributes & 0x80) &&
          m_bg_color_ids[framebuffer_index] != 0) {
        continue;
      }

      m_framebuffer[framebuffer_index] = get_color(color_id, palette);
    }
  }
}

Color Ppu::get_color(u8 color_id, u8 palette) const {
  const u8 shade = static_cast<u8>((palette >> (color_id * 2)) & 0x03);
  return GB_COLORS[shade];
}

void Ppu::update_cpu_access() {
  if (!m_lcd_enabled) {
    m_bus.ppu_set_cpu_access(true, true);
    return;
  }

  bool vram_accessible = true;
  bool oam_accessible = true;

  switch (m_mode) {
  case Mode::HBlank:
  case Mode::VBlank:
    // Both are accessible outside rendering/OAM scan.
    break;

  case Mode::OAMScan:
    // Allow access during the final OAM-scan machine cycle.
    oam_accessible = m_cycles >= 76;
    break;

  case Mode::Drawing:
    vram_accessible = false;
    oam_accessible = false;
    break;
  }

  m_bus.ppu_set_cpu_access(vram_accessible, oam_accessible);
}