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

static constexpr Color UI_BG = GB_COLORS[3]; 
static constexpr Color UI_PANEL = GB_COLORS[2];
static constexpr Color UI_ACCENT = GB_COLORS[1];
static constexpr Color UI_TEXT = GB_COLORS[0];

struct Toast {
  std::string text;
  double until{0.0};
};

static void show_toast(Toast &toast, const std::string &text) {
  toast.text = text;
  toast.until = GetTime() + 2.0;
}

static void draw_toast(const Toast &toast) {
  if (GetTime() >= toast.until) {
    return;
  }
  const int w = GB_WIDTH * SCALE;
  const int h = GB_HEIGHT * SCALE;
  const int tw = MeasureText(toast.text.c_str(), 20);
  DrawRectangleRounded({(w - tw) / 2.0f - 14.0f, h - 48.0f, tw + 28.0f, 34.0f},
                       0.5f, 8, Fade(UI_BG, 0.85f));
  DrawText(toast.text.c_str(), (w - tw) / 2, h - 41, 20, UI_TEXT);
}

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

struct BrowserEntry {
  std::string name;
  std::filesystem::path path;
  bool is_dir{false};
};

static std::vector<BrowserEntry>
scan_browse_dir(const std::filesystem::path &dir) {
  std::vector<BrowserEntry> out;
  std::error_code ec;

  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    const std::string name = entry.path().filename().string();
    if (name.empty() || name.front() == '.') {
      continue;
    }

    std::error_code tec;
    if (entry.is_directory(tec)) {
      out.push_back({name + "/", entry.path(), true});
    } else if (entry.is_regular_file(tec) &&
               entry.path().extension() == ".gb") {
      out.push_back({name, entry.path(), false});
    }
  }

  std::sort(out.begin(), out.end(),
            [](const BrowserEntry &a, const BrowserEntry &b) {
              if (a.is_dir != b.is_dir)
                return a.is_dir;
              return a.name < b.name;
            });

  return out;
}

enum class AppState { Menu, Playing, Browse };

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

  bool running = true;
  int selected = 0;
  int scroll = 0;
  bool paused = false;

  Toast toast;
  float cursor_y = -1.0f;

  std::string rom_dir = "roms";
  std::vector<RomEntry> library = scan_rom_folder(rom_dir);

  std::filesystem::path browse_dir;
  std::vector<BrowserEntry> browse_entries;
  int browse_selected = 0;
  int browse_scroll = 0;

  auto start_game = [&](const std::string &path) {
    try {
      paused = false;
      clear_audio_fifo(); // gotcha (a): no leftover audio from the last game
      emu = std::make_unique<Emulator>(path);
      state = AppState::Playing;
    } catch (const std::exception &error) {
      std::cerr << "Error: " << error.what() << '\n';
      show_toast(toast, std::string{"ERROR: "} + error.what());
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

      if (IsKeyPressed(KEY_O)) {
        browse_dir =
            std::filesystem::absolute(std::filesystem::is_directory(rom_dir)
                                          ? std::filesystem::path{rom_dir}
                                          : std::filesystem::current_path());
        browse_entries = scan_browse_dir(browse_dir);
        browse_selected = 0;
        browse_scroll = 0;
        cursor_y = -1.0f;
        state = AppState::Browse;
        break;
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

      const int W = GB_WIDTH * SCALE;
      const int H = GB_HEIGHT * SCALE;
      const int line_h = 28;
      const int list_top = 56;
      const int list_bottom = H - 36;
      const int visible = (list_bottom - list_top) / line_h;

      if (selected < scroll) {
        scroll = selected;
      }
      if (selected >= scroll + visible) {
        scroll = selected - visible + 1;
      }

      BeginDrawing();
      ClearBackground(UI_BG);

      // header
      DrawText("GB EMULATOR", 20, 14, 20, UI_TEXT);
      if (count > 0) {
        const std::string n =
            std::to_string(count) + (count == 1 ? " GAME" : " GAMES");
        DrawText(n.c_str(), W - 20 - MeasureText(n.c_str(), 10), 20, 10,
                 UI_ACCENT);
      }
      DrawRectangle(0, 44, W, 2, UI_PANEL);

      if (count == 0) {
        const char *msg1 = "NO GAMES FOUND";
        const char *msg2 = "put .gb files in the roms/ folder";
        DrawText(msg1, (W - MeasureText(msg1, 20)) / 2, H / 2 - 24, 20,
                 UI_TEXT);
        DrawText(msg2, (W - MeasureText(msg2, 10)) / 2, H / 2 + 4, 10,
                 UI_ACCENT);
      } else {
        // animated selection cursor: eases toward the selected row
        const float target_y =
            static_cast<float>(list_top + (selected - scroll) * line_h);
        if (cursor_y < 0.0f) {
          cursor_y = target_y;
        }
        cursor_y += (target_y - cursor_y) * 0.35f;

        DrawRectangleRounded({10.0f, cursor_y, static_cast<float>(W - 26),
                              static_cast<float>(line_h - 4)},
                             0.45f, 8, UI_ACCENT);

        const int last = std::min(count, scroll + visible);
        for (int i = scroll; i < last; ++i) {
          const int y = list_top + (i - scroll) * line_h + 3;
          const bool is_selected = (i == selected);
          DrawText(library[i].title.c_str(), 24, y, 20,
                   is_selected ? UI_BG : Fade(UI_TEXT, 0.7f));
        }

        // scrollbar
        if (count > visible) {
          const float track_h = static_cast<float>(list_bottom - list_top);
          const float thumb_h = track_h * static_cast<float>(visible) / count;
          const float thumb_y =
              list_top + track_h * static_cast<float>(scroll) / count;
          DrawRectangle(W - 8, list_top, 3, static_cast<int>(track_h),
                        Fade(UI_PANEL, 0.5f));
          DrawRectangle(W - 8, static_cast<int>(thumb_y), 3,
                        static_cast<int>(thumb_h), UI_ACCENT);
        }
      }

      // footer
      DrawRectangle(0, H - 32, W, 2, UI_PANEL);
      DrawText("W/S  MOVE    ENTER  PLAY    O  OPEN ROM    ESC  QUIT", 20,
               H - 22, 10, UI_ACCENT);

      draw_toast(toast);
      EndDrawing();
    } break;

    case AppState::Playing: {
      if (IsKeyPressed(KEY_ESCAPE)) {
        emu.reset();        // dtor fires -> battery save
        clear_audio_fifo(); // don't let the old game's audio tail play
        state = AppState::Menu;
        library = scan_rom_folder(rom_dir);
        cursor_y = -1.0f; // menu cursor snaps instead of sliding across

        // flush one frame so Menu doesn't see this same ESC press-edge
        BeginDrawing();
        ClearBackground(UI_BG);
        EndDrawing();
        break;
      }

      if (IsKeyPressed(KEY_P)) {
        paused = !paused;
      }

      if (IsKeyPressed(KEY_F5)) {
        try {
          emu->save_state();
          show_toast(toast, "STATE SAVED");
        } catch (const std::exception &error) {
          std::cerr << "Save state failed: " << error.what() << '\n';
          show_toast(toast, "SAVE FAILED");
        }
      }

      if (IsKeyPressed(KEY_F8)) {
        try {
          emu->load_state();
          show_toast(toast, "STATE LOADED");
        } catch (const std::exception &error) {
          std::cerr << "Load state failed: " << error.what() << '\n';
          show_toast(toast, "LOAD FAILED");
        }
      }

      const bool fast_forward = IsKeyDown(KEY_TAB);

      if (!paused) {
        emu->run_frame(fast_forward);
      }

      BeginDrawing();
      ClearBackground(UI_BG);
      emu->draw();

      if (paused) {
        DrawRectangle(0, 0, GB_WIDTH * SCALE, GB_HEIGHT * SCALE,
                      Fade(UI_BG, 0.65f));
        const char *msg = "PAUSED";
        DrawText(msg, (GB_WIDTH * SCALE - MeasureText(msg, 30)) / 2,
                 GB_HEIGHT * SCALE / 2 - 15, 30, UI_TEXT);
      } else if (fast_forward) {
        DrawText(">>", GB_WIDTH * SCALE - 46, 10, 30, UI_ACCENT);
      }

      draw_toast(toast);
      EndDrawing();
    } break;

    case AppState::Browse: {
      if (IsKeyPressed(KEY_ESCAPE)) {
        state = AppState::Menu;
        cursor_y = -1.0f;

        BeginDrawing();
        ClearBackground(UI_BG);
        EndDrawing();
        break;
      }

      if (IsKeyPressed(KEY_BACKSPACE)) {
        if (browse_dir.has_parent_path() &&
            browse_dir != browse_dir.root_path()) {
          browse_dir = browse_dir.parent_path();
          browse_entries = scan_browse_dir(browse_dir);
          browse_selected = 0;
          browse_scroll = 0;
          cursor_y = -1.0f;
        }
      }

      if (IsKeyPressed(KEY_L)) {
        rom_dir = browse_dir.string();
        library = scan_rom_folder(rom_dir);
        selected = 0;
        scroll = 0;
        cursor_y = -1.0f;
        show_toast(toast, "LIBRARY FOLDER SET");
        state = AppState::Menu;
        break;
      }

      const int count = static_cast<int>(browse_entries.size());

      if (count > 0) {
        if (browse_selected >= count) {
          browse_selected = count - 1;
        }
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
          browse_selected = (browse_selected + 1) % count;
        }
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
          browse_selected = (browse_selected + count - 1) % count;
        }

        if (IsKeyPressed(KEY_ENTER)) {
          const BrowserEntry &e = browse_entries[browse_selected];
          if (e.is_dir) {
            browse_dir = e.path;
            browse_entries = scan_browse_dir(browse_dir);
            browse_selected = 0;
            browse_scroll = 0;
            cursor_y = -1.0f;

            BeginDrawing();
            ClearBackground(UI_BG);
            EndDrawing();
            break;

          } else {
            start_game(e.path.string());
            break;
          }
        }
      }

      const int W = GB_WIDTH * SCALE;
      const int H = GB_HEIGHT * SCALE;
      const int line_h = 28;
      const int list_top = 68;
      const int list_bottom = H - 36;
      const int visible = (list_bottom - list_top) / line_h;

      if (browse_selected < browse_scroll) {
        browse_scroll = browse_selected; // (your paste had = browse_scroll)
      }
      if (browse_selected >= browse_scroll + visible) {
        browse_scroll = browse_selected - visible + 1;
      }

      BeginDrawing();
      ClearBackground(UI_BG);

      DrawText("OPEN ROM", 20, 14, 20, UI_TEXT);
      DrawRectangle(0, 44, W, 2, UI_PANEL);

      {
        // truncate path from the left
        const std::string full = browse_dir.string();
        std::string shown = full;
        while (MeasureText(shown.c_str(), 10) > W - 40 && shown.size() > 4) {
          shown.erase(0, 1);
        }
        if (shown.size() != full.size()) {
          shown = "..." + shown;
        }
        DrawText(shown.c_str(), 20, 52, 10, Fade(UI_TEXT, 0.6f));
      }

      if (count == 0) {
        const char *msg = "No GameBoy ROM found!";
        DrawText(msg, (W - MeasureText(msg, 20)) / 2, H / 2 - 10, 20,
                 Fade(UI_TEXT, 0.7f));
      } else {
        const float target_y = static_cast<float>(
            list_top + (browse_selected - browse_scroll) * line_h);
        if (cursor_y < 0.0f) {
          cursor_y = target_y;
        }
        cursor_y += (target_y - cursor_y) * 0.35f;

        DrawRectangleRounded({10.0f, cursor_y, static_cast<float>(W - 26),
                              static_cast<float>(line_h - 4)},
                             0.45f, 8, UI_ACCENT);

        const int last = std::min(count, browse_scroll + visible);
        for (int i = browse_scroll; i < last; ++i) {
          const int y = list_top + (i - browse_scroll) * line_h + 3;
          const bool is_selected = (i == browse_selected);
          const Color col =
              is_selected
                  ? UI_BG
                  : (browse_entries[i].is_dir ? UI_TEXT : Fade(UI_TEXT, 0.7f));
          DrawText(browse_entries[i].name.c_str(), 24, y, 20, col);
        }

        if (count > visible) {
          const float track_h = static_cast<float>(list_bottom - list_top);
          const float thumb_h = track_h * static_cast<float>(visible) / count;
          const float thumb_y =
              list_top + track_h * static_cast<float>(browse_scroll) / count;
          DrawRectangle(W - 8, list_top, 3, static_cast<int>(track_h),
                        Fade(UI_PANEL, 0.5f));
          DrawRectangle(W - 8, static_cast<int>(thumb_y), 3,
                        static_cast<int>(thumb_h), UI_ACCENT);
        }
      }

      DrawRectangle(0, H - 32, W, 2, UI_PANEL);
      DrawText("ENTER  OPEN    BKSP  UP    L  SET LIBRARY    ESC  BACK", 20,
               H - 22, 10, UI_ACCENT);

      draw_toast(toast);
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