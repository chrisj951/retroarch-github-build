#!/bin/bash
set -e

RETROARCH_VERSION="${RETROARCH_VERSION:-v1.22.2}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"

echo "=== Building RetroArch ${RETROARCH_VERSION} for A30 (armhf) ==="

# Clone RetroArch
if [ ! -d "RetroArch" ]; then
    git clone --depth 1 --branch "$RETROARCH_VERSION" \
        https://github.com/libretro/RetroArch.git
fi

cd RetroArch

# Apply patches
if [ -d /patches ] && ls /patches/*.patch 1>/dev/null 2>&1; then
    for patch in /patches/*.patch; do
        echo "Applying: $(basename "$patch")"
        git apply "$patch"
    done
fi

# A30 buildroot toolchain
TOOLCHAIN=/opt/a30
SYSROOT=$TOOLCHAIN/arm-a30-linux-gnueabihf/sysroot
CROSS=arm-a30-linux-gnueabihf

export PATH="$TOOLCHAIN/bin:$PATH"
export CC="${CROSS}-gcc"
export CXX="${CROSS}-g++"
export AR="${CROSS}-ar"
export STRIP="${CROSS}-strip"
export PKG_CONFIG_PATH="$SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export CFLAGS="--sysroot=$SYSROOT -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="--sysroot=$SYSROOT -L$SYSROOT/usr/lib"

# Diagnostic: test the cross-compiler before configure
echo "=== Compiler diagnostic ==="
echo "CC=$CC"
which "$CC" && echo "found at: $(which "$CC")"
cat "$(which "$CC")"
echo "--- test compile ---"
echo 'int main(void){return 0;}' > /tmp/test.c
$CC -o /tmp/test_arm /tmp/test.c -v 2>&1 || echo "COMPILER TEST FAILED (exit $?)"
echo "=== end diagnostic ==="

# Configure for A30: Mali fbdev + SDL2 + GLES2
./configure --host=arm-a30-linux-gnueabihf \
    --disable-x11 \
    --disable-wayland \
    --disable-vulkan \
    --disable-opengl \
    --disable-qt \
    --disable-kms \
    --disable-pulse \
    --disable-jack \
    --disable-oss \
    --disable-discord \
    --disable-udev \
    --enable-opengles \
    --enable-egl \
    --enable-mali_fbdev \
    --enable-sdl2 \
    --enable-alsa \
    --enable-networking \
    --enable-ssl \
    --enable-command \
    --enable-freetype \
    --enable-builtinzlib \
    --enable-zlib \
    --enable-neon

# Build
make -j$(nproc)

# Output
mkdir -p "$OUTPUT_DIR"
cp retroarch "$OUTPUT_DIR/"
${CROSS}-strip "$OUTPUT_DIR/retroarch"

echo "=== Build complete: ${OUTPUT_DIR}/retroarch ==="
