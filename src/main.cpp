#include "Bus.hpp"
#include "Cpu.hpp"
#include "Ppu.hpp"
#include "rom.hpp"
#include "types.hpp"

#include "raylib.h"

#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

static constexpr int SCALE = 3;

void run_emulator(const std::vector<u8> &rom) {
  Bus bus{rom};
  Cpu cpu{bus};
  Ppu ppu{bus};

  InitWindow(GB_WIDTH * SCALE, GB_HEIGHT * SCALE, "GB Emulator");
  SetTargetFPS(60);

  Image img = GenImageColor(GB_WIDTH, GB_HEIGHT, BLACK);
  Texture2D texture = LoadTextureFromImage(img);
  UnloadImage(img);

  while (!WindowShouldClose()) {
    std::uint32_t frame_cycles = 0;
    while (frame_cycles < 70224) {
      const u32 cycles = cpu.step();
      bus.tick(cycles);
      ppu.tick(cycles);
      frame_cycles += cycles;
    }

    UpdateTexture(texture, ppu.framebuffer().data());

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
