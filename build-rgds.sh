#!/bin/bash
set -e

RETROARCH_VERSION="${RETROARCH_VERSION:-v1.22.2}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"

echo "=== Building RetroArch ${RETROARCH_VERSION} for aarch64 ==="

# Clone RetroArch
if [ ! -d "RetroArch" ]; then
    git clone --depth 1 --branch "$RETROARCH_VERSION" \
        https://github.com/libretro/RetroArch.git
fi

cd RetroArch

# Apply common patches
if [ -d /patches/common ] && ls /patches/common/*.patch 1>/dev/null 2>&1; then
    for patch in /patches/common/*.patch; do
        echo "Applying: $(basename "$patch")"
        git apply "$patch"
    done
fi

# Cross-compilation environment
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++
export AR=aarch64-linux-gnu-ar
export STRIP=aarch64-linux-gnu-strip
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig
export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig

# Configure for universal aarch64 binary
# Uses SDL2 + EGL + GLES + Vulkan. GLES works on all GPUs; Vulkan is available
# on devices with Vulkan drivers (e.g. Mali G57 on Smart Pro S).
./configure --disable-qt \
            --disable-discord \
            --disable-neon \
            --disable-vg \
            --disable-sdl \
            --disable-x11 \
            --disable-vulkan \
            --disable-vulkan_display \
            --disable-opengl1 \
            --disable-opengl_core \
            --disable-jack \
            --enable-alsa \
            --enable-udev \
            --enable-zlib \
            --enable-freetype \
            --enable-sdl2 \
            --enable-kms \
            --enable-ffmpeg \
            --enable-wayland \
            --enable-opengles \
            --enable-opengles3 \
            --enable-opengles3_1 \
            --enable-opengles3_2 \
            --enable-opengl

export CFLAGS="$CFLAGS -DHAVE_SCREEN_ORIENTATION -DGEOMETRY_MENU_ROTATION -D_GNU_SOURCE"
export CXXFLAGS="$CXXFLAGS -DHAVE_SCREEN_ORIENTATION -DGEOMETRY_MENU_ROTATION -D_GNU_SOURCE"

# Build
make HAVE_STATIC_VIDEO_FILTERS=1 HAVE_STATIC_AUDIO_FILTERS=1 -j$(nproc)

# Output
mkdir -p "$OUTPUT_DIR"
cp retroarch "$OUTPUT_DIR/"
aarch64-linux-gnu-strip "$OUTPUT_DIR/retroarch"

echo "=== Build complete: ${OUTPUT_DIR}/retroarch ==="
