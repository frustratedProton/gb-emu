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
  explicit Ppu(Bus &bus) : m_bus(bus) {};

private:
  Bus &m_bus;

  std::array<Color, GB_WIDTH * GB_HEIGHT> m_framebuffer{};

  u32 m_cycles{}; // cycles within current scanline
  u8 m_ly{};      // current scanline (0-153)
};