# VibeOS

> 🛑 **CRITICAL WARNING** 🛑
>
> **DO NOT RUN THIS OPERATING SYSTEM ON REAL HARDWARE.**
>
> This is an experimental **research kernel** written from scratch. It is **NOT** a Linux fork. The filesystem drivers and memory management are in early development stages.
>
> Running VibeOS on a physical computer (laptop, desktop) carries significant risks:
> - **Permanent Data Loss:** The experimental FAT32 write drivers (`edit`, `mkdir`) are not stable and **will corrupt** your partitions or filesystem tables if bugs occur.
> - **Hardware Stress:** The kernel lacks ACPI power management, which may cause CPU overheating on physical devices.
> - **UEFI Corruption:** Incorrect interaction with NVRAM could potentially damage motherboard settings.
>
> **The author is NOT responsible for any damage to your hardware or data.**
> **This project is designed strictly for use inside the QEMU emulator.**

---

**VibeOS** is a custom 64-bit Operating System written entirely from scratch in C++. It does not rely on the standard library or any existing kernel (like Linux or Windows). It implements its own bootloader, graphics rendering, and filesystem drivers, targeting the UEFI specification.

## Features

- **UEFI Compliant Core**: Written to the UEFI specification (optimized for QEMU/OVMF).
- **Custom Graphics**: Implements a native Graphics Output Protocol (GOP) driver for high-resolution rendering.
- **Experimental Filesystem**: Custom FAT32 implementation with support for:
  - `ls` (list directory)
  - `cd` (change directory)
  - `mkdir` (create directory - *experimental*)
  - `edit` (write to files - *experimental*)
  - `cat` (read files)
- **Interactive Shell**: A custom command-line interface with color support.
- **Pure C++**: Built without `libc` or assembly glue code where possible.

## Prerequisites

To build and run this project, you need a Linux environment with the following tools:

- **Clang & LLD**: LLVM compiler and linker.
- **QEMU**: Specifically `qemu-system-x86_64` with OVMF (UEFI firmware).
- **Mtools**: For manipulating FAT disk images without mounting.

## Building & Running

**Note:** This project is configured to run automatically inside an emulator.

1. Clone the repository.
2. Ensure you have the permissions to execute the build script.
3. Run the automation script:

```bash
chmod +x build_and_run.sh
./build_and_run.sh
```

This script will compile the kernel, generate a bootable UEFI disk image, and launch QEMU.

## Disclaimer of Warranty

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
