#!/usr/bin/env bash
set -euo pipefail
launcher_src=$(realpath "${1:?usage: run.sh PATCHED_LAUNCHER_SOURCE}")
out_dir=$(mktemp -d -t matawaa-power-tests-XXXXXX)
trap 'rm -rf "$out_dir"' EXIT
"${CXX:-c++}" -std=c++17 -I"$launcher_src/src" "$(dirname "$0")/test_wifi_policy.cpp" -o "$out_dir/test_wifi_policy"
"$out_dir/test_wifi_policy"
