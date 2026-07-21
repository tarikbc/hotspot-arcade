#!/usr/bin/env bash
# Print a stable hash of the sources that produce the ESP firmware images.
#
# The .bin images bundled in the fap (flipper/hotspot-arcade/assets/firmware/) are
# committed build outputs, so nothing stops them from going stale when the ESP source
# changes — and a stale image means "Install Firmware" flashes firmware the app then
# rejects as outdated. We can't diff the images themselves: ESP-IDF embeds a build
# timestamp in the app descriptor, so two builds of identical source differ byte-wise.
# Instead we hash the *inputs* and record that alongside the images; CI compares.
#
# tools/build-fap.sh writes the result to flipper/hotspot-arcade/.bundled-fw.sha256
# right after it refreshes the images. The build.yml "bundled-assets" job recomputes it
# and fails if the two disagree.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

# Hash every file that feeds the firmware build: the sketch plus the vendored libs.
# The build/ dir is output, not input. -print0 + sort -z keeps this stable across
# filesystems that hand back a different readdir order.
{
    find esp32/hotspot-arcade-fw -type f ! -path '*/build/*' -print0
    find esp32/libs -type f -print0
} | LC_ALL=C sort -z | xargs -0 shasum -a 256 | shasum -a 256 | cut -d' ' -f1
