<div align="center">

# Shin Megami Tensei: Persona (PSP) — Native PC Port (Recompiled)

[![GitHub Release](https://img.shields.io/github/v/release/rexkarbu/P1-recomp-psp?style=for-the-badge&color=8A2BE2)](https://github.com/rexkarbu/P1-recomp-psp/releases)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6?style=for-the-badge&logo=windows)](https://github.com/rexkarbu/P1-recomp-psp)
[![License](https://img.shields.io/badge/License-GPL--3.0-green.svg?style=for-the-badge)](LICENSE)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg?style=for-the-badge)](https://github.com/rexkarbu/P1-recomp-psp/actions)

**A high-performance, native PC static recompilation of _Shin Megami Tensei: Persona_ (PlayStation Portable, USA — `ULUS-10432`).**

*Not an emulator. Game code is statically recompiled into native x86_64 machine code for blazing-fast 60 FPS performance, low system requirements, and zero input lag.*

[Key Features](#-key-features) • [Installation (Players)](#-how-to-play-players-guide) • [Controls](#-controls) • [Building From Source](#-building-from-source-developers) • [FAQ & Legal](#-legal--disclaimer)

</div>

---

## 🌟 Overview

This project brings Atlus's classic PlayStation Portable masterpiece **Shin Megami Tensei: Persona (2009)** natively to modern PCs via **Static Binary Recompilation**. By translating MIPS R4000 instructions directly into native C11 and executing them on the host CPU, this port achieves seamless 60 FPS gameplay, crystal-clear high-definition presentation, responsive controls, and rich quality-of-life enhancements.

### ⚖️ Legal Distribution Model (Bring Your Own Game — BYOG)
Following the clean distribution philosophy of projects like **Zelda 64 Recomp**, **OpenGOAL (Jak & Daxter)**, and **Sonic CD Recompilation**:
- **No copyrighted game assets, ROMs, ISOs, audio files, or proprietary BIOS binaries are contained in this repository.**
- Players supply their own legally dumped ISO image (`Shin Megami Tensei - Persona (USA)`).
- The included **Standalone Launcher (`persona_launcher.exe`)** extracts, decrypts, and links game assets on your computer locally in seconds.

---

## ✨ Key Features

- **🚀 Native 60 FPS Performance**:
  - Direct execution on host hardware—no heavy emulator virtualization overhead.
  - Sub-microsecond cooperative thread scheduling (`PSPRECOMP_REAL_THREADS`).
- **🎮 Modern Gamepad & Dual-Keyboard Controls**:
  - Full XInput controller support (Xbox, DualShock/DualSense via XInput, 8BitDo) with rumble and analog-stick navigation.
  - Dual-layout keyboard configuration (WASD + Z/X/C/V or Arrow Keys + J/K/U/I).
- **🖥️ Hardware-Accelerated OpenGL Display**:
  - Double-buffered WGL presentation context with integer scaling and bilinear filtering.
  - Dynamic aspect ratios (16:9 native, 4:3 classic, borderless fullscreen via `F11` / `Alt+Enter`).
  - Crisp text and character portraits with zero scanline striping or pixel artifacts.
- **🔊 Host PC Audio Streamer**:
  - Direct low-latency PCM audio streaming via Win32 `waveOut` backend.
  - Crystal-clear playback of Shoji Meguro's soundtrack and character battle voices.
- **💾 Native Save & Load Compatibility**:
  - Full support for original PSP / PPSSPP save files (`./savedata/ULUS104320000/`).
  - Seamless migration: Copy your existing PSP memory stick save folder to continue your journey!
- **⚙️ In-Game Settings Overlay (`F1`)**:
  - Instant access to Video options (Resolution Scale 1x..4x, Target FPS, FXAA, OSD FPS counter).
  - Audio Master Volume slider.
  - Built-in CWCheat integration (Infinite Battle HP/SP, Max Yen, Max Casino Coins, No Encounters).
  - Instant Save State (`F5`) and Load State (`F6`).

---

## 🚀 How to Play (Players Guide)

### Prerequisites
- A 64-bit Windows PC (Windows 10 / 11 recommended).
- An official ISO dump of **Shin Megami Tensei: Persona (USA)** (`ULUS-10432`).
  - *Redump SHA-1: `ec190259efdac60801e25c9f7d1ce51cda8f94aa`*

### Steps
1. Download the latest `persona1_pc_windows_x64.zip` from the [Releases](https://github.com) page.
2. Extract the archive into a folder of your choice.
3. Launch **`persona_launcher.exe`**.
4. Drag and drop your **Persona 1 ISO** file onto the launcher window (or click **"Select ISO File..."**).
5. The launcher will verify the game checksum, extract game assets, and decrypt the executable automatically.
6. Click **"PLAY PERSONA 1"** to start the game!

> [!TIP]
> You only need to perform the setup once. Subsequent launches will take you straight to the game.

---

## 🎮 Controls

### Gamepad (Xbox / Generic XInput)
| Action | Gamepad |
| :--- | :--- |
| **Movement** | D-Pad / Left Thumbstick |
| **Confirm (Cross)** | `A` Button |
| **Cancel (Circle)** | `B` Button |
| **Menu (Square)** | `X` Button |
| **Persona (Triangle)** | `Y` Button |
| **Left Trigger (L)** | `LB` / `LT` |
| **Right Trigger (R)** | `RB` / `RT` |
| **Start / Pause** | `Start` Button |
| **Map / System** | `Back` Button |
| **Settings Menu** | `Xbox Guide` or `Back + Start` |

### Keyboard
| Action | Primary Keys | Alternate Keys |
| :--- | :--- | :--- |
| **Movement** | `W` / `A` / `S` / `D` | `Up` / `Left` / `Down` / `Right` |
| **Confirm (Cross)** | `Z` / `Space` | `J` |
| **Cancel (Circle)** | `X` / `Escape` | `K` |
| **Menu (Square)** | `C` | `U` |
| **Persona (Triangle)** | `V` | `I` |
| **L / R Triggers** | `Q` / `E` | `LShift` / `RShift` |
| **Start / Select** | `Enter` | `Tab` / `Backspace` |
| **In-Game Settings**| `F1` | — |
| **Toggle Fullscreen**| `F11` or `Alt + Enter` | — |
| **Instant Screenshot**| `F2` (saved to `./screenshots/`) | — |
| **Toggle FPS Counter**| `F4` | — |
| **Save State** | `F5` (saved to `./states/`) | — |
| **Load State** | `F6` | — |

---

## 🛠️ Building From Source (Developers)

If you wish to build the executable from source code:

### Requirements
- **Git**
- **CMake (>= 3.16)**
- **Ninja Build System**
- **MinGW-w64 GCC (>= 11.0)** or **Clang**

### Build Commands
```bash
# 1. Clone this repository
git clone https://github.com/rexkarbu/P1-recomp-psp.git
cd P1-recomp-psp

# 2. Build the psprecomp runtime library
cmake -B tools/psprecomp/build -S tools/psprecomp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build tools/psprecomp/build --config Release

# 3. Build persona_pc.exe and persona_launcher.exe
cmake -B build_pc -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_pc --config Release

# 4. The resulting executables will be located in ./build_pc/
```

---

## 📁 Repository Structure

```
persona1-pc-recomp/
├── .github/
│   └── workflows/
│       └── build.yml          # GitHub Actions automated CI/CD multi-platform build
├── src/
│   ├── main.c                 # Host entry point, memory mapping & engine loop
│   ├── hle_custom.c           # High-Level Emulation hooks (Display, Savedata, Audio)
│   └── launcher/
│       ├── main.c             # Win32 desktop GUI launcher
│       └── iso_extractor.c    # Native ISO9660 & Kirk decryption engine
├── recomp_src/                # Statically recompiled C11 game code functions
├── tools/
│   └── psprecomp/             # Modular PSP static recompilation framework
├── CMakeLists.txt             # Root CMake build configuration
├── .gitignore                 # Strict exclusion list for clean BYOG compliance
└── README.md                  # Project documentation & guides
```

---

## ⚖️ Legal & Disclaimer

- **Shin Megami Tensei: Persona** is a registered trademark of **ATLUS / SEGA**.
- This project is an independent open-source initiative and is **not affiliated with, endorsed by, or sponsored by Atlus or SEGA**.
- **No Atlus game code, proprietary artwork, video sequences, sound effects, or musical compositions are stored in this repository.**
- Users must dump their own physical or digital copy of the game to play.
- This software is distributed under the **GPLv3 License** strictly for educational, archival, and preservation purposes.

---

<div align="center">
<i>Crafted with passion for JRPG preservation and the Persona community.</i>
</div>

