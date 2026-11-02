# WinTetris

A small, dependency-free Tetris-like game for Windows, written in C++ with the
Win32 API and GDI. One window, no engine, no assets. Includes solo play and a
two-player LAN versus mode.

![Gameplay](screenshot.png)

## Features

- Standard 10x20 playfield with all seven tetrominoes
- 7-bag randomizer, SRS rotation with wall kicks
- Ghost piece, hold slot, and a three-piece next queue
- DAS/ARR auto-repeat, soft drop, hard drop, and a 0.5 s lock delay
- Guideline-style scoring: singles/doubles/triples/tetris, combos, level speed-up
- Line-clear flash, pause and game-over overlays
- Two-player LAN versus over TCP: host or join, split-screen boards and
  garbage-line attacks (doubles send 1 row, triples 2, tetrises 4, combos +1)
- Looping chiptune arrangement of Korobeiniki ("Tetris Theme A"), synthesized at
  runtime with no audio files
- Best score saved to `%APPDATA%\WinTetris\best.txt`
- Per-monitor DPI aware; automatically scales down to fit smaller screens
- Dark title bar on Windows 10/11

## Build

Requires Windows 10 or newer and MSVC (Visual Studio 2022+ or Build Tools with the
"Desktop development with C++" workload).

Double-click or run:

```bat
build.bat
```

This produces `tetris.exe`. Alternatively use CMake:

```bat
cmake -B build
cmake --build build --config Release
```

Or compile manually from a Developer Command Prompt:

```bat
cl /nologo /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE audio.cpp game.cpp ^
   multiplayer.cpp net.cpp main.cpp ^
   /link /SUBSYSTEM:WINDOWS /OUT:tetris.exe winmm.lib ws2_32.lib
```

## LAN multiplayer

The game uses TCP on port **7777** and needs no external libraries or servers.
Both players must be on the same local network (or the same PC for testing).

1. Start `tetris.exe` on both machines.
2. On the first machine choose **HOST LAN GAME**. The screen shows the IPv4
   addresses to share.
3. On the second machine choose **JOIN LAN GAME**, type the host's address
   (for example `192.168.1.42`) and press Enter.
4. When the connection opens, both players see a short countdown and then a
   split-screen match begins.

Clearing two or more lines at once sends garbage rows to the opponent. When a
player tops out the match ends; press **R** for a rematch or **Esc** to return
to the menu.

If Windows Firewall prompts the first time you host, allow the app on private
networks. The connection is plain TCP for trusted LANs: there is no encryption,
authentication, or internet matchmaking.

## Controls

### In a match or solo game

| Key | Action |
| --- | --- |
| Left / Right | Move (hold to auto-repeat) |
| Down | Soft drop |
| Space | Hard drop |
| Up or X | Rotate clockwise |
| Z | Rotate counter-clockwise |
| C | Hold / swap piece |
| P or Esc | Pause / resume |
| R | Restart (solo); rematch after a LAN match ends |
| M | Mute / unmute music |
| Enter | Resume or restart from the solo overlays |

### Menus

| Key | Action |
| --- | --- |
| Up / Down or W / S | Select a menu option |
| Enter | Confirm |
| 1 / 2 / 3 | Jump to Solo / Host / Join |
| Esc | Go back, cancel hosting/joining, or quit from the main menu |
| Digits and `.` | Type the host IP on the Join screen |
| Backspace | Edit the host IP |

Music pauses and resumes with the game, and solo play pauses automatically when
the window loses focus.

## Project layout

| File | Contents |
| --- | --- |
| `game.h` / `game.cpp` | Pure game logic: board, pieces, SRS kicks, scoring, garbage |
| `audio.h` / `audio.cpp` | Korobeiniki chiptune synthesis and `waveOut` playback |
| `net.h` / `net.cpp` | Non-blocking TCP host/client sockets and local IP discovery |
| `multiplayer.h` / `multiplayer.cpp` | LAN wire protocol and remote board snapshots |
| `main.cpp` | Win32 window, menus, input, GDI rendering, high-score storage |
| `build.bat` | One-step MSVC build |
| `CMakeLists.txt` | CMake build definition |

## Notes

- The music is a synthesized arrangement of Korobeiniki, a traditional Russian
  folk tune in the public domain. No recordings or audio files are bundled; the
  waveform is generated in memory at startup.
- Gameplay sound effects (moves, line clears) are not implemented yet.
- Scores and progress live only in memory until a best score is saved.
- The code intentionally avoids external libraries so it can be built with only
  the Windows SDK.
