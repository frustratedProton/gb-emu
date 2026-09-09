#pragma once

#include "Bus.hpp"
#include "raylib.h"
#include "types.hpp"

#include <array>

static constexpr int GB_WIDTH = 160;
static constexpr int GB_HEIGHT = 144;

static constexpr Color GB_COLORS[4] = {
    {0xE0, 0xF8, 0xD0, 0xFF},
    {0x88, 0xC0, 0x70, 0xFF},
    {0x34, 0x68, 0x56, 0xFF},
    {0x08, 0x18, 0x20, 0xFF},
};

class Bus;

class Ppu {
public:
  explicit Ppu(Bus &bus) : m_bus(bus) {};

  bool tick(u32 cycles);

  [[nodiscard]] const std::array<Color, GB_WIDTH * GB_HEIGHT> &
  framebuffer() const {
    return m_framebuffer;
  }

private:
  void render_scanline();
  void render_background_scanline(u8 ly);

  [[nodiscard]] u8 get_tile_pixel(u8 tile_id, u8 tile_x, u8 tile_y,
                                  bool use_signed_addressing) const;
  [[nodiscard]] Color get_color(u8 color_id, u8 palette) const;

  Bus &m_bus;

  std::array<Color, GB_WIDTH * GB_HEIGHT> m_framebuffer{};

  u32 m_cycles{}; // cycles within current scanline
  u8 m_ly{};      // current scanline (0-153)

  enum class Mode : u8 {
    HBlank = 0,
    VBlank = 1,
    OAMScan = 2,
    Drawing = 3,
  };

  Mode m_mode{Mode::OAMScan};
};