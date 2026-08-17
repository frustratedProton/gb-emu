# GAMEBOY EMULATOR [WIP]

## REFERENCES

https://gbdev.io/pandocs/Specifications.html

http://www.codeslinger.co.uk/pages/projects/gameboy/beginning.html

https://imrannazar.com/series/gameboy-emulation-in-javascript

https://izik1.github.io/gbops/

## BUILD

Build the project with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## RUN

Run deterministic assertions with your real Tetris ROM:

```bash
./build/gbemu_tests "roms/Tetris_(World)_(Rev_1).gb"
```

Run the instruction trace separately:

```bash
./build/gbemu "roms/Tetris_(World)_(Rev_1).gb"
```
