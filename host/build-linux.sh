#!/usr/bin/env sh
set -eu
base="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cmake -S "$base" -B "$base/build-linux" -DCMAKE_BUILD_TYPE=Release
cmake --build "$base/build-linux" --parallel
ctest --test-dir "$base/build-linux" --output-on-failure
cmake --install "$base/build-linux" --prefix "$base/dist-linux"
