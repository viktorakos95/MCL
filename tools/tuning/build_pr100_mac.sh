#!/usr/bin/env bash
# Build the Gearmulator MachineDrum/Monomachine plugin from joelanders/gearmulator-md-mm PR #100
# (the one with X.14 / OS-variant support) on macOS. Needs: git, cmake, Xcode command line tools.
# Not tested on macOS by the author of this script - if a step fails, send me the last lines.
#
#   bash build_pr100_mac.sh            # builds into ./gearmulator-md-mm-pr100
#
# Afterwards: keep your official 1.63 .bin in the plugin's roms folder, open the plugin,
# Settings -> OS version -> import your X.14 .syx, then select it.
set -euo pipefail
DIR="${1:-gearmulator-md-mm-pr100}"
git clone https://github.com/joelanders/gearmulator-md-mm "$DIR"
cd "$DIR"
git fetch origin pull/100/head
git checkout -B pr100 FETCH_HEAD
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -Dgearmulator_SYNTH_OSIRUS=OFF -Dgearmulator_SYNTH_OSTIRUS=OFF -Dgearmulator_SYNTH_VAVRA=OFF \
  -Dgearmulator_SYNTH_XENIA=OFF -Dgearmulator_SYNTH_NODALRED2X=OFF -Dgearmulator_SYNTH_JE8086=OFF
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"
echo "Built. Plugins/standalone are somewhere under: $(pwd)/build"
find build -maxdepth 6 \( -name "*.vst3" -o -name "*.component" -o -name "*.app" \) -not -path "*/CMakeFiles/*" | head
