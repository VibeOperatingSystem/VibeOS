#!/bin/bash
set -e

PROJECT_ROOT=$(pwd)
SRC_DIR="$PROJECT_ROOT/src"
BUILD_DIR="$PROJECT_ROOT/build"
DIST_DIR="$PROJECT_ROOT/dist"

mkdir -p "$SRC_DIR" "$BUILD_DIR" "$DIST_DIR"

# Ensure dependencies
DEPS="clang lld qemu-system-x86 edk2-ovmf mtools"
MISSING_DEPS=()
for pkg in $DEPS; do
    if ! pacman -Qi $pkg &> /dev/null; then
        MISSING_DEPS+=($pkg)
    fi
done
if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    sudo pacman -S --needed ${MISSING_DEPS[@]}
fi

# Cleanup
rm -f "$BUILD_DIR/"* "$DIST_DIR/"*

echo "[+] Compiling VibeOS v5 (Final)..."
clang -target x86_64-unknown-windows \
      -ffreestanding \
      -fshort-wchar \
      -mno-red-zone \
      -Wall \
      -c "$SRC_DIR/kernel.cpp" \
      -o "$BUILD_DIR/kernel.obj"

echo "[+] Linking..."
lld-link -subsystem:efi_application \
         -nodefaultlib \
         -entry:EfiMain \
         "$BUILD_DIR/kernel.obj" \
         -out:"$BUILD_DIR/BOOTX64.EFI"

# Disk Image
IMG_FILE="$DIST_DIR/vibeos.img"
dd if=/dev/zero of="$IMG_FILE" bs=1M count=64 status=none
mformat -i "$IMG_FILE" -F ::
mmd -i "$IMG_FILE" ::/EFI
mmd -i "$IMG_FILE" ::/EFI/BOOT
mcopy -i "$IMG_FILE" "$BUILD_DIR/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI

# Find OVMF
OVMF_PATH=""
POSSIBLE_PATHS=(
    "/usr/share/edk2-ovmf/x64/OVMF.4m.fd"
    "/usr/share/edk2/x64/OVMF.fd"
    "/usr/share/ovmf/x64/OVMF.fd"
)
for path in "${POSSIBLE_PATHS[@]}"; do
    if [ -f "$path" ]; then
        OVMF_PATH="$path"
        break
    fi
done

echo "[+] Launching VibeOS v5..."
qemu-system-x86_64 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_PATH" \
    -drive format=raw,file="$IMG_FILE" \
    -net none \
    -m 512M \
    -serial stdio
