#!/bin/bash
set -e

RETROARCH_REF="${RETROARCH_REF:-e5eff6db27cd37c3c318741ee8bb9a3b8b60ec62}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"

echo "=== Building RetroArch for Pixel2 (aarch64 native) ==="
echo "=== Ref: ${RETROARCH_REF} ==="

# Set up ccache
export CCACHE_DIR="${CCACHE_DIR:-/ccache}"
export CC="ccache gcc"
export CXX="ccache g++"
ccache --max-size=500M
ccache --zero-stats

# Clone RetroArch and checkout pinned commit
if [ ! -d "RetroArch" ]; then
    git clone https://github.com/libretro/RetroArch.git
    cd RetroArch
    git checkout "$RETROARCH_REF"
else
    cd RetroArch
fi

# Apply pixel2-specific patches only
if [ -d /patches/pixel2 ] && ls /patches/pixel2/*.patch 1>/dev/null 2>&1; then
    for patch in /patches/pixel2/*.patch; do
        echo "Applying: $(basename "$patch")"
        git apply "$patch"
    done
fi

# Configure — Hario's exact flags for Pixel2 (RK3566 / Mali-G52)
CFLAGS="-Ofast -march=armv8-a -mtune=cortex-a35 -fomit-frame-pointer -DNDEBUG" \
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

# Build
make -j$(nproc)
strip retroarch

# Build filters as external .so plugins
FILTER_CFLAGS="-Ofast -march=armv8-a -mtune=cortex-a35 -fomit-frame-pointer -DNDEBUG"
make -C gfx/video_filters extra_flags="$FILTER_CFLAGS"
make -C libretro-common/audio/dsp_filters extra_flags="$FILTER_CFLAGS"
strip gfx/video_filters/*.so
strip libretro-common/audio/dsp_filters/*.so

# Output binary
mkdir -p "$OUTPUT_DIR"
cp retroarch "$OUTPUT_DIR/"

# Output filters
mkdir -p "$OUTPUT_DIR/filters/video" "$OUTPUT_DIR/filters/audio"
cp gfx/video_filters/*.so gfx/video_filters/*.filt "$OUTPUT_DIR/filters/video/"
cp libretro-common/audio/dsp_filters/*.so libretro-common/audio/dsp_filters/*.dsp "$OUTPUT_DIR/filters/audio/"

echo "=== ccache stats ==="
ccache --show-stats

echo "=== Build complete: ${OUTPUT_DIR}/retroarch ==="
