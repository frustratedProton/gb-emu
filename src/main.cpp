#include "Bus.hpp"
#include "Cpu.hpp"
#include "rom.hpp"
#include "types.hpp"

#include "raylib.h"

#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

static constexpr int GB_WIDTH = 160;
static constexpr int GB_HEIGHT = 144;
static constexpr int SCALE = 3;

static constexpr Color GB_COLORS[4] = {
    {0xE0, 0xF8, 0xD0, 0xFF}, // 0 = lightest (white-ish)
    {0x88, 0xC0, 0x70, 0xFF}, // 1 = light green
    {0x34, 0x68, 0x56, 0xFF}, // 2 = dark green
    {0x08, 0x18, 0x20, 0xFF}, // 3 = darkest (black-ish)
};

void run_emulator(const std::vector<u8> &rom) {
  Bus bus{rom};
  Cpu cpu{bus};

  std::array<Color, GB_WIDTH * GB_HEIGHT> framebuffer{};

  for (auto &pixel : framebuffer) {
    pixel = GB_COLORS[0];
  }

  InitWindow(GB_WIDTH, GB_HEIGHT, "GameBoy Emulator");
  SetTargetFPS(60);
  Image img = GenImageColor(GB_WIDTH, GB_HEIGHT, BLACK);
  Texture2D texture = LoadTextureFromImage(img);
  UnloadImage(img);

  u64 instruction = 0;
  const u64 limit = 100000000;

  while (!WindowShouldClose()) {
    // run one frame worth of cycles
    // 4194304 / 60 = ~69905 cycles per frame
    u32 frame_cycles = 0;
    while (frame_cycles < 69905 && instruction < limit) {
      const u16 old_pc = cpu.registers().pc;

      const u32 cycles = cpu.step();
      bus.tick(cycles);
      frame_cycles += cycles;
      instruction++;

      if (cpu.registers().pc == old_pc)
        break;
    }

    // upload framebuffer to texture and draw
    UpdateTexture(texture, framebuffer.data());

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTextureEx(texture, {0, 0}, 0.0f, static_cast<float>(SCALE), WHITE);
    EndDrawing();
  }

  UnloadTexture(texture);
  CloseWindow();
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: gbemu <path-to-rom.gb>\n";
      return 1;
    }

    const std::vector<u8> rom = load_rom(argv[1]);

    if (rom.size() >= 2 && rom.at(0) == 0x50 && rom.at(1) == 0x4B)
      throw std::runtime_error{"ZIP archive detected"};

    if (rom.size() < 0x150)
      throw std::runtime_error{"ROM too small"};

    run_emulator(rom);

  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
