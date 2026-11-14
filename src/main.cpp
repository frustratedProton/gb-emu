#include "Bus.hpp"
#include "Cpu.hpp"
#include "Ppu.hpp"
#include "rom.hpp"
#include "types.hpp"

#include "raylib.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

static std::mutex g_audio_mutex;
static std::vector<i16> g_audio_fifo;

static constexpr int SCALE = 3;

static void audio_callback(void *buffer, unsigned int frames) {
  auto *out = static_cast<i16 *>(buffer);
  const std::size_t need = static_cast<std::size_t>(frames) * 2;

  std::lock_guard<std::mutex> lock{g_audio_mutex};

  const std::size_t have = std::min(need, g_audio_fifo.size());

  std::copy_n(g_audio_fifo.begin(), have, out);
  g_audio_fifo.erase(g_audio_fifo.begin(),
                     g_audio_fifo.begin() + static_cast<std::ptrdiff_t>(have));

  std::fill(out + have, out + need, static_cast<i16>(0));
}

static void clear_audio_fifo() {
  std::lock_guard<std::mutex> lock{g_audio_mutex};
  g_audio_fifo.clear();
}

static void update_joypad(Bus &bus) {
  u8 directions = 0x0F;
  u8 buttons = 0x0F;

  // Directional buttons
  if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
    directions &= static_cast<u8>(~0x01u); // Right, bit 0
  }

  if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) {
    directions &= static_cast<u8>(~0x02u); // Left, bit 1
  }

  if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) {
    directions &= static_cast<u8>(~0x04u); // Up, bit 2
  }

  if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) {
    directions &= static_cast<u8>(~0x08u); // Down, bit 3
  }

  // Action buttons
  if (IsKeyDown(KEY_Z)) {
    buttons &= static_cast<u8>(~0x01u); // A
  }

  if (IsKeyDown(KEY_X)) {
    buttons &= static_cast<u8>(~0x02u); // B
  }

  if (IsKeyDown(KEY_BACKSPACE) || IsKeyDown(KEY_SPACE)) {
    buttons &= static_cast<u8>(~0x04u); // Select
  }

  if (IsKeyDown(KEY_ENTER)) {
    buttons &= static_cast<u8>(~0x08u); // Start
  }

  bus.set_joypad_state(directions, buttons);
}

static std::string save_path_for(const std::string &rom_path) {
  std::filesystem::path path{rom_path};
  path.replace_extension(".sav");
  return path.string();
}

static std::vector<u8> load_and_validate_rom(const std::string &rom_path) {
  std::vector<u8> rom = load_rom(rom_path);

  if (rom.size() >= 2 && rom.at(0) == 0x50 && rom.at(1) == 0x4B)
    throw std::runtime_error{"ZIP archive detected"};

  if (rom.size() < 0x150)
    throw std::runtime_error{"ROM too small"};

  return rom;
}

enum class AppState { Menu, Playing };

struct Emulator {
  std::vector<u8> rom_data;
  Bus bus;
  Ppu ppu;
  bool frame_ready = false;
  Cpu cpu;
  Texture2D texture{};

  explicit Emulator(const std::string &rom_path)
      : rom_data{load_and_validate_rom(rom_path)},
        bus{rom_data, save_path_for(rom_path)}, ppu{bus},
        cpu{bus, [this](u32 cycles) {
              bus.tick(cycles);
              if (ppu.tick(cycles)) {
                frame_ready = true;
              }
            }} {
    Image img = GenImageColor(GB_WIDTH, GB_HEIGHT, BLACK);
    texture = LoadTextureFromImage(img);
    UnloadImage(img);
  }

  Emulator(const Emulator &) = delete;
  Emulator &operator=(const Emulator &) = delete;

  ~Emulator() {
    bus.save_battery_ram();
    UnloadTexture(texture);
  }

  void run_frame() {
    update_joypad(bus);

    frame_ready = false;

    while (!frame_ready) {
      (void)cpu.step();
    }

    {
      std::lock_guard<std::mutex> lock{g_audio_mutex};
      auto &s = bus.apu_mut().samples();
      g_audio_fifo.insert(g_audio_fifo.end(), s.begin(), s.end());
      s.clear();
    }

    // Audio-driven pacing: block until the FIFO drains to ~100ms
    for (;;) {
      {
        std::lock_guard<std::mutex> lock{g_audio_mutex};
        if (g_audio_fifo.size() <= 8820) { // 100ms of stereo i16
          break;
        }
      }
      WaitTime(0.001);
    }
  }

  void draw() {
    UpdateTexture(texture, ppu.framebuffer().data());
    DrawTextureEx(texture, {0, 0}, 0.0f, static_cast<float>(SCALE), WHITE);
  }
};

#if 0  // OLD CODE
void run_emulator(const std::vector<u8> &rom, const std::string &rom_path) {
  Bus bus{rom, save_path_for(rom_path)};
  Ppu ppu{bus};

  bool frame_ready = false;

  Cpu cpu{bus, [&](u32 cycles) {
            bus.tick(cycles);

            if (ppu.tick(cycles)) {
              frame_ready = true;
            }
          }};

  InitWindow(GB_WIDTH * SCALE, GB_HEIGHT * SCALE, "GB Emulator");

  SetExitKey(KEY_NULL);
  SetTargetFPS(60);

  InitAudioDevice();
  //   SetAudioStreamBufferSizeDefault(1024);

  AudioStream stream = LoadAudioStream(44100, 16, 2);
  SetAudioStreamCallback(stream, audio_callback);
  PlayAudioStream(stream);

  //   SetTargetFPS(60);

  Image img = GenImageColor(GB_WIDTH, GB_HEIGHT, BLACK);
  Texture2D texture = LoadTextureFromImage(img);
  UnloadImage(img);

  while (!WindowShouldClose()) {
    update_joypad(bus);

    frame_ready = false;

    while (!frame_ready) {
      auto val = cpu.step();
    }

    {
      std::lock_guard<std::mutex> lock{g_audio_mutex};
      auto &s = bus.apu_mut().samples();
      g_audio_fifo.insert(g_audio_fifo.end(), s.begin(), s.end());
      s.clear();
    }

    for (;;) {
      {
        std::lock_guard<std::mutex> lock{g_audio_mutex};
        if (g_audio_fifo.size() <= 8820) { // 100ms of stereo i16
          break;
        }
      }
      WaitTime(0.001);
    }

    UpdateTexture(texture, ppu.framebuffer().data());

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTextureEx(texture, {0, 0}, 0.0f, static_cast<float>(SCALE), WHITE);
    EndDrawing();
  }

  bus.save_battery_ram();
  UnloadTexture(texture);
  CloseWindow();
}
#endif // OLD CODE

int main(int argc, char *argv[]) {
  InitWindow(GB_WIDTH * SCALE, GB_HEIGHT * SCALE, "GB Emulator");

  SetExitKey(KEY_NULL); // we use ESC for "back to menu", not quit
  SetTargetFPS(60);     

  InitAudioDevice();

  AudioStream stream = LoadAudioStream(44100, 16, 2);
  SetAudioStreamCallback(stream, audio_callback);
  PlayAudioStream(stream);

  AppState state = AppState::Menu;
  std::unique_ptr<Emulator> emu;
  std::string status = "DROP A .gb ROM HERE";

  auto start_game = [&](const std::string &path) {
    try {
      clear_audio_fifo(); // gotcha (a): no leftover audio from the last game
      emu = std::make_unique<Emulator>(path);
      state = AppState::Playing;
    } catch (const std::exception &error) {
      std::cerr << "Error: " << error.what() << '\n';
      status = std::string{"Error: "} + error.what();
      emu.reset();
      state = AppState::Menu;
    }
  };

  // also the fallback on WSLg, where drag-and-drop
  // from Windows Explorer may simply never arrive.
  if (argc == 2) {
    start_game(argv[1]);
  }

  while (!WindowShouldClose()) {
    switch (state) {

    case AppState::Menu: {
      if (IsFileDropped()) {
        FilePathList dropped = LoadDroppedFiles();
        std::string path = (dropped.count > 0) ? dropped.paths[0] : "";
        UnloadDroppedFiles(dropped); // ALWAYS unload

        if (!path.empty()) {
          start_game(path);
        }
      }

      BeginDrawing();
      ClearBackground(BLACK);
      DrawText(status.c_str(), 40, (GB_HEIGHT * SCALE) / 2 - 10, 20, RAYWHITE);
      EndDrawing();
    } break;

    case AppState::Playing: {
      if (IsKeyPressed(KEY_ESCAPE)) {
        emu.reset();        // dtor fires -> battery save
        clear_audio_fifo(); // don't let the old game's audio tail play
        status = "DROP A .gb ROM HERE";
        state = AppState::Menu;
        break;
      }

      emu->run_frame();

      BeginDrawing();
      ClearBackground(BLACK);
      emu->draw();
      EndDrawing();
    } break;
    }
  }

  emu.reset(); // battery save if the window was closed mid-game

  UnloadAudioStream(stream);
  CloseAudioDevice();
  CloseWindow();

  return 0;
}