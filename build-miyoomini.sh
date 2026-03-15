#!/bin/bash
set -e

RETROARCH_VERSION="${RETROARCH_VERSION:-v1.22.2}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"

echo "=== Building RetroArch ${RETROARCH_VERSION} for Miyoo Mini (armhf) ==="

# Clone RetroArch
if [ ! -d "RetroArch" ]; then
    git clone --depth 1 --branch "$RETROARCH_VERSION" \
        https://github.com/libretro/RetroArch.git
fi

cd RetroArch

# Apply miyoomini-specific patches only (common patches require GFX_WIDGETS/GPU)
for dir in /patches/miyoomini; do
    if [ -d "$dir" ] && ls "$dir"/*.patch 1>/dev/null 2>&1; then
        for patch in "$dir"/*.patch; do
            echo "Applying: $(basename "$patch")"
            git apply "$patch"
        done
    fi
done

# Copy miyoomini custom source files on top of the patched tree
cp -r /miyoomini_src/* .

# Build using the miyoomini Makefile
make -f Makefile.miyoomini -j$(nproc)

# Output
mkdir -p "$OUTPUT_DIR"
cp retroarch "$OUTPUT_DIR/"
/opt/miyoomini-toolchain/usr/bin/arm-linux-gnueabihf-strip "$OUTPUT_DIR/retroarch"

echo "=== Build complete: ${OUTPUT_DIR}/retroarch ==="
