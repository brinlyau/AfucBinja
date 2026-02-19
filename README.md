# AFUC Binary Ninja Architecture Plugin

A Binary Ninja architecture plugin for the AFUC (Adreno Five Micro Controller) ISA, supporting Qualcomm Adreno 5xx, 6xx, and 7xx GPU firmware.

## Standing on the Shoulders of Giants

This plugin is built entirely on the foundational work of the [freedreno](https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/freedreno) project. The AFUC ISA was reverse-engineered and documented by **Rob Clark**, **Connor Abbott**, and the many contributors to freedreno over the years. Their work includes the ISA specification, assembler, disassembler, and emulator — without which none of this would exist.

All I did was implement the Binary Ninja-specific hooks (architecture registration, instruction text tokens, low-level IL lifting, and firmware auto-detection) and fix a bug. The hard work of understanding and documenting this ISA belongs to the freedreno community.

## Features

- **Disassembly** of all AFUC instruction types (ALU, branches, memory, control register access, bitfield ops)
- **IL lifting** for data-flow analysis and decompilation
- **Auto-detection** of GPU generation (a5xx/a6xx/a7xx) from firmware ID
- **Firmware loader** that correctly maps the instruction space, skipping the file header

## Building

```
mkdir build && cd build
cmake .. -DBN_API_PATH=/path/to/binaryninja-api -DBN_INSTALL_DIR=/path/to/binaryninja
make -j$(nproc)
```

## Installation

Copy `libarch_afuc.so` (or `.dylib`/`.dll`) to your Binary Ninja plugins directory:

- **Linux:** `~/.binaryninja/plugins/`
- **macOS:** `~/Library/Application Support/Binary Ninja/plugins/`
- **Windows:** `%APPDATA%/Binary Ninja/plugins/`

Or run `make install` after building.

## Usage

Open an AFUC firmware binary in Binary Ninja. The plugin auto-detects the firmware format and GPU generation. You can also manually select `afuc-a5xx`, `afuc-a6xx`, or `afuc-a7xx` as the architecture when loading a raw binary.

## Acknowledgments

- **Rob Clark** — creator of freedreno and the original AFUC reverse engineering work
- **Connor Abbott** — major contributions to AFUC ISA documentation and tooling
- **All freedreno contributors** — for years of work reverse-engineering Adreno GPU firmware

Source: https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/freedreno/afuc
