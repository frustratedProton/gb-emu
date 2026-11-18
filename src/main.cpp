#include "Bus.hpp"
#include "Cpu.hpp"
#include "Ppu.hpp"
#include "rom.hpp"
#include "state.hpp"
#include "types.hpp"

#include "raylib.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
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
  path.replace_extension(".state");
  return path.string();
}

static constexpr u16 STATE_VERSION = 1;

static std::string state_path_for(const std::string &rom_path) {
  std::filesystem::path path{rom_path};
  path.replace_extension(".state");
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

struct RomEntry {
  std::string path;  // full path
  std::string title; // header title
};

static std::string header_title_or_fallback(const std::filesystem::path &path) {
  std::string title;
  std::ifstream file{path, std::ios::binary};

  if (file) {
    std::array<char, 16> raw{};
    file.seekg(0x134); // title is located at 0x134-0x142
    file.read(raw.data(), raw.size());

    if (file.gcount() == static_cast<std::streamsize>(raw.size())) {
      for (char c : raw) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc > 0x7E) {
          break;
        }
        title += c;
      }
    }
  }

  // trim spaces
  while (!title.empty() && title.back() == ' ') {
    title.pop_back();
  }

  if (title.empty()) {
    title = path.stem().string(); // garbagio
  }
  return title;
}

static std::vector<RomEntry> scan_rom_folder(const std::filesystem::path &dir) {
  std::vector<RomEntry> library;
  std::error_code ec;

  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    if (entry.path().extension() != ".gb") {
      continue;
    }

    if (entry.file_size() < 0x150) {
      continue;
    }

    library.push_back(
        {entry.path().string(), header_title_or_fallback(entry.path())});
  }

  std::sort(
      library.begin(), library.end(),
      [](const RomEntry &a, const RomEntry &b) { return a.title < b.title; });

  return library;
}

enum class AppState { Menu, Playing };

struct Emulator {
  std::vector<u8> rom_data;
  Bus bus;
  Ppu ppu;
  bool frame_ready = false;
  Cpu cpu;
  Texture2D texture{};

  std::string rom_path;

  explicit Emulator(const std::string &rom_path_arg)
      : rom_data{load_and_validate_rom(rom_path_arg)},
        bus{rom_data, save_path_for(rom_path_arg)}, ppu{bus},
        cpu{bus, [this](u32 cycles) {
              bus.tick(cycles);
              if (ppu.tick(cycles)) {
                frame_ready = true;
              }
            }} {
    rom_path = rom_path_arg;
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

  void run_frame(bool fast_forward) {
    update_joypad(bus);

    const int frames = fast_forward ? 4 : 1;

    for (int f = 0; f < frames; ++f) {
      frame_ready = false;

      while (!frame_ready) {
        (void)cpu.step();
      }
    }

    {
      std::lock_guard<std::mutex> lock{g_audio_mutex};
      auto &s = bus.apu_mut().samples();

      if (g_audio_fifo.size() < 8820) {
        g_audio_fifo.insert(g_audio_fifo.end(), s.begin(), s.end());
      }
      s.clear();
    }

    if (!fast_forward) {
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
  }

  void draw() {
    UpdateTexture(texture, ppu.framebuffer().data());
    DrawTextureEx(texture, {0, 0}, 0.0f, static_cast<float>(SCALE), WHITE);
  }

  void save_state() const {
    StateWriter w;

    // header: magic, version, ROM identity
    const std::array<u8, 4> magic{'G', 'B', 'S', 'T'};
    w.write_array(magic);
    w.write<u16>(STATE_VERSION);
    w.write<u64>(rom_data.size());
    w.write_bytes(rom_data.data() + 0x134, 16); // header title
    w.write<u8>(rom_data[0x14E]);               // global checksum
    w.write<u8>(rom_data[0x14F]);

    cpu.save_state(w);
    bus.save_state(w);
    ppu.save_state(w);

    save_state_to_file(state_path_for(rom_path), w);
  }

  void load_state() {
    StateReader r = load_state_from_file(state_path_for(rom_path));

    std::array<u8, 4> magic{};
    r.read_array(magic);
    if (magic != std::array<u8, 4>{'G', 'B', 'S', 'T'}) {
      throw std::runtime_error{"state: not a state file"};
    }
    if (r.read<u16>() != STATE_VERSION) {
      throw std::runtime_error{"state: version mismatch"};
    }
    if (r.read<u64>() != rom_data.size()) {
      throw std::runtime_error{"state: wrong ROM (size)"};
    }
    std::array<u8, 16> title{};
    r.read_array(title);
    if (!std::equal(title.begin(), title.end(), rom_data.begin() + 0x134)) {
      throw std::runtime_error{"state: wrong ROM (title)"};
    }
    const u8 csum_hi = r.read<u8>();
    const u8 csum_lo = r.read<u8>();
    if (csum_hi != rom_data[0x14E] || csum_lo != rom_data[0x14F]) {
      throw std::runtime_error{"state: wrong ROM (checksum)"};
    }

    cpu.load_state(r);
    bus.load_state(r);
    ppu.load_state(r);

    if (!r.at_end()) {
      throw std::runtime_error{"state: trailing data"};
    }

    frame_ready = false;
    clear_audio_fifo(); 
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

  bool running = true;
  int selected = 0;
  int scroll = 0;
  bool paused = false;

  static constexpr const char *ROM_DIR = "roms";
  std::vector<RomEntry> library = scan_rom_folder(ROM_DIR);

  auto start_game = [&](const std::string &path) {
    try {
      paused = false;
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
  // from Windows Explorer may simply never arrive
  if (argc == 2) {
    start_game(argv[1]);
  }

  while (running && !WindowShouldClose()) {
    switch (state) {

    case AppState::Menu: {
      if (IsKeyPressed(KEY_ESCAPE)) {
        running = false;
        break;
      }

      if (IsFileDropped()) {
        FilePathList dropped = LoadDroppedFiles();
        std::string path = (dropped.count > 0) ? dropped.paths[0] : "";
        UnloadDroppedFiles(dropped); // ALWAYS unload

        if (!path.empty()) {
          start_game(path);
          break;
        }
      }

      const int count = static_cast<int>(library.size());

      if (count > 0) {
        if (selected >= count) {
          selected = count - 1;
        }

        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
          selected = (selected + 1) % count;
        }
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
          selected = (selected + count - 1) % count;
        }

        if (IsKeyPressed(KEY_ENTER)) {
          start_game(library[selected].path);
          break;
        }
      }

      // DRAWING
      const int line_h = 26;
      const int top = 20;
      const int visible =
          (GB_HEIGHT * SCALE - top - 10) / line_h; // rows that fit

      if (selected < scroll) {
        scroll = selected;
      }
      if (selected >= scroll + visible) {
        scroll = selected - visible + 1;
      }

      BeginDrawing();
      ClearBackground(BLACK);

      if (count == 0) {
        DrawText("No ROMs found - put .gb files in roms/", 20, 20, 20,
                 RAYWHITE);
        DrawText(status.c_str(), 20, 50, 20, GRAY);
      } else {
        const int last = std::min(count, scroll + visible);

        for (int i = scroll; i < last; ++i) {
          const int y = top + (i - scroll) * line_h;
          const bool is_selected = (i == selected);

          if (is_selected) {
            DrawRectangle(10, y - 3, GB_WIDTH * SCALE - 20, line_h, DARKGREEN);
          }

          DrawText(library[i].title.c_str(), 20, y, 20,
                   is_selected ? RAYWHITE : GRAY);
        }
      }

      EndDrawing();

    } break;

    case AppState::Playing: {
      if (IsKeyPressed(KEY_ESCAPE)) {
        emu.reset();        // dtor fires -> battery save
        clear_audio_fifo(); // don't let the old game's audio tail play
        status = "DROP A .gb ROM HERE";
        state = AppState::Menu;
        library = scan_rom_folder(ROM_DIR);

        // FLUSH KEY_ESCAPE input
        BeginDrawing();
        ClearBackground(BLACK);
        EndDrawing();

        break;
      }

      if (IsKeyPressed(KEY_P)) {
        paused = !paused;
      }

      const bool fast_forward = IsKeyDown(KEY_TAB);

      if (!paused) {
        emu->run_frame(fast_forward);
      }

      if (IsKeyPressed(KEY_F5)) {
        try {
          emu->save_state();
          std::cout << "State Saved\n";
        } catch (const std::exception &error) {
          std::cerr << "Save state failed: " << error.what() << '\n';
        }
      }

      if (IsKeyPressed(KEY_F8)) {
        try {
          emu->load_state();
          std::cout << "State loaded\n";
        } catch (const std::exception &error) {
          std::cerr << "Load state failed: " << error.what() << '\n';
        }
      }

      BeginDrawing();
      ClearBackground(BLACK);
      emu->draw();

      if (paused) {
        DrawRectangle(0, 0, GB_WIDTH * SCALE, GB_HEIGHT * SCALE,
                      Color{0, 0, 0, 160}); // dim the game
        DrawText("PAUSED", GB_WIDTH * SCALE / 2 - 40,
                 GB_HEIGHT * SCALE / 2 - 10, 20, RAYWHITE);
      } else if (fast_forward) {
        DrawText(">>", GB_WIDTH * SCALE - 40, 10, 30, YELLOW);
      }

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