#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MAYHEM_DIR="${1:-}"

if [[ -z "$MAYHEM_DIR" ]]; then
    echo "Usage: $0 /path/to/mayhem-mdk" >&2
    exit 2
fi
MAYHEM_DIR="$(cd -- "$MAYHEM_DIR" && pwd)"

required_files=(
    "src/portapack-external-module/CMakeLists.txt"
    "src/portapack-external-module/main/CMakeLists.txt"
    "src/portapack-external-module/main/main.cpp"
)

for file in "${required_files[@]}"; do
    if [[ ! -f "$MAYHEM_DIR/$file" ]]; then
        echo "Not a compatible mayhem-mdk checkout; missing $file" >&2
        exit 1
    fi
done

if ! git -C "$MAYHEM_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "Use a Git checkout so the installer can protect local changes." >&2
    exit 1
fi

if ! git -C "$MAYHEM_DIR" diff --quiet HEAD -- "${required_files[@]}"; then
    echo "The overlay would replace locally edited Mayhem files." >&2
    echo "Use a fresh branch or save those changes before installing." >&2
    exit 1
fi

if [[ -e "$MAYHEM_DIR/components/thumbcore" ]]; then
    echo "The target already has components/thumbcore; refusing to replace it." >&2
    exit 1
fi

mkdir -p \
    "$MAYHEM_DIR/src/portapack-external-module/main/pandora" \
    "$MAYHEM_DIR/src/common" \
    "$MAYHEM_DIR/components"

cp "$SCRIPT_DIR/overlay/src/portapack-external-module/CMakeLists.txt" \
    "$MAYHEM_DIR/src/portapack-external-module/CMakeLists.txt"
cp "$SCRIPT_DIR/overlay/src/portapack-external-module/main/CMakeLists.txt" \
    "$MAYHEM_DIR/src/portapack-external-module/main/CMakeLists.txt"
cp "$SCRIPT_DIR/overlay/src/portapack-external-module/main/main.cpp" \
    "$MAYHEM_DIR/src/portapack-external-module/main/main.cpp"
cp "$SCRIPT_DIR/overlay/src/portapack-external-module/main/pandora_display_link.cpp" \
    "$MAYHEM_DIR/src/portapack-external-module/main/pandora_display_link.cpp"
cp "$SCRIPT_DIR/overlay/src/portapack-external-module/main/pandora_display_link.hpp" \
    "$MAYHEM_DIR/src/portapack-external-module/main/pandora_display_link.hpp"
cp "$SCRIPT_DIR/overlay/src/portapack-external-module/main/display_test_pattern.cpp" \
    "$MAYHEM_DIR/src/portapack-external-module/main/display_test_pattern.cpp"
cp "$SCRIPT_DIR/overlay/src/portapack-external-module/main/display_test_pattern.hpp" \
    "$MAYHEM_DIR/src/portapack-external-module/main/display_test_pattern.hpp"

cp "$SCRIPT_DIR/protocol/pandora_display_protocol.h" \
    "$MAYHEM_DIR/src/common/pandora_display_protocol.h"

if [[ -f "$SCRIPT_DIR/overlay/src/portapack-external-module/main/firmware.bin" ]]; then
    cp "$SCRIPT_DIR/overlay/src/portapack-external-module/main/firmware.bin" \
        "$MAYHEM_DIR/src/portapack-external-module/main/firmware.bin"
fi

cp "$SCRIPT_DIR/upstream/main/emulator.c" \
    "$MAYHEM_DIR/src/portapack-external-module/main/pandora/emulator.c"
cp "$SCRIPT_DIR/upstream/main/emulator.h" \
    "$MAYHEM_DIR/src/portapack-external-module/main/pandora/emulator.h"
cp "$SCRIPT_DIR/upstream/main/protocol.h" \
    "$MAYHEM_DIR/src/portapack-external-module/main/pandora/protocol.h"
mkdir -p "$MAYHEM_DIR/components/thumbcore"
cp -R "$SCRIPT_DIR/upstream/components/thumbcore/." \
    "$MAYHEM_DIR/components/thumbcore/"

echo "Overlay with RF integration installed."
echo "The emulator task now supports button inputs and a TX/RX RF bridge."
echo "Provide your verified flat firmware image as main/firmware.bin before building."
echo "Build the Pandora View app first, then build the ESP32-S3 module; see README.md."
