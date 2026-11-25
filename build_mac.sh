#!/usr/bin/env bash
# Compila EC Audio como binario universal (Intel + Apple Silicon)
set -e
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"
echo "Listo: build/ECAudio_artefacts/Release/EC Audio.app"
