#!/usr/bin/env bash
# Rebuilds the ObCYDian firmware and refreshes this web flasher's copy of the
# binaries, the manifest version and the version shown on the page. Run this
# after any firmware change, before re-publishing the flasher page.
#
#   ./build.sh            # version from the latest git tag (e.g. v0.1.0)
#   ./build.sh 0.2.0      # or give it explicitly
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"

if command -v pio >/dev/null 2>&1; then
    PIO=pio
elif [ -x "$HOME/.platformio/penv/bin/pio" ]; then
    PIO="$HOME/.platformio/penv/bin/pio"
else
    echo "Could not find 'pio' (PlatformIO Core)." >&2
    exit 1
fi

BOOT_APP0=$(find "$HOME/.platformio/packages" -path "*framework-arduinoespressif32*/tools/partitions/boot_app0.bin" | head -1)
if [ -z "$BOOT_APP0" ]; then
    echo "Could not find boot_app0.bin in the installed Arduino ESP32 framework package." >&2
    exit 1
fi

VERSION="${1:-$(git -C "$REPO_ROOT" describe --tags --abbrev=0 2>/dev/null || echo v0.0.0)}"
VERSION="${VERSION#v}"

echo "Building ObCYDian $VERSION..."
cd "$REPO_ROOT"
"$PIO" run -e cyd35
BUILD_DIR="$REPO_ROOT/.pio/build/cyd35"
OUT_DIR="$HERE/firmware"
mkdir -p "$OUT_DIR"
cp "$BUILD_DIR/bootloader.bin" "$OUT_DIR/"
cp "$BUILD_DIR/partitions.bin" "$OUT_DIR/"
cp "$BUILD_DIR/firmware.bin" "$OUT_DIR/"
cp "$BOOT_APP0" "$OUT_DIR/"
echo "Updated $OUT_DIR/ with the latest build."

# Version, date and size shown on the page, and the manifest version
SIZE=$(cat "$OUT_DIR"/*.bin | wc -c | awk '{printf "%.1f MB", $1/1048576}')
TODAY=$(date +%F)
sed -i -E "s/(\"version\": \")[^\"]*/\1$VERSION/" "$HERE/manifest.json"
sed -i -E \
    -e "s/(class=\"fw-version\">)[^<]*/\1$VERSION/g" \
    -e "s/(class=\"fw-date\">)[^<]*/\1$TODAY/g" \
    -e "s/(class=\"fw-size\">)[^<]*/\1$SIZE/g" \
    "$HERE/index.html"
echo "Page and manifest now show version $VERSION ($SIZE, $TODAY)."
