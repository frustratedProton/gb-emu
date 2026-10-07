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

static void update_joypad(Bus &bus) {
  u8 directions = 0x0F;
  u8 buttons = 0x0F;

  // directional buttons
  if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
    directions &= static_cast<u8>(-0x01);
  }

  if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) {
    directions &= static_cast<u8>(-0x01);
  }

  if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) {
    directions &= static_cast<u8>(-0x01);
  }

  if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) {
    directions &= static_cast<u8>(-0x01);
  }

  // Action buttons
  if (IsKeyDown(KEY_Z)) {
    buttons &= static_cast<u8>(~0x01); // A
  }

  if (IsKeyDown(KEY_X)) {
    buttons &= static_cast<u8>(~0x02); // B
  }

  if (IsKeyDown(KEY_BACKSPACE) || IsKeyDown(KEY_SPACE)) {
    buttons &= static_cast<u8>(~0x04); // Select
  }

  if (IsKeyDown(KEY_ENTER)) {
    buttons &= static_cast<u8>(~0x08); // Start
  }

  bus.set_joypad_state(directions, buttons);
}

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
    update_joypad(bus);

    bool frame_ready = false;

    while (!frame_ready) {
      const u32 cycles = cpu.step();

      bus.tick(cycles);
      frame_ready = ppu.tick(cycles);
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
