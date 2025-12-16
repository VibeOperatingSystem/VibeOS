# VibeOS

A custom UEFI Operating System written from scratch in C++.

## Features
- **UEFI Bootloader**: Runs natively on modern hardware/QEMU.
- **Graphics Mode**: Custom GOP rendering driver.
- **Filesystem**: FAT32 support with `ls`, `cd`, `mkdir`, `edit`, `cat`.
- **Shell**: Interactive command line interface with color support.

## Building & Running

Requirements: `clang`, `lld`, `qemu`, `mtools`.

```bash
./build_and_run.sh
