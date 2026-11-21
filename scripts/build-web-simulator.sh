#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
version="$(tr -d '\r\n' < .emscripten-version)"
emsdk_root="${MK3K_EMSDK_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/mk3k-emsdk}"
build_dir="$repo_root/.web-simulator-build"
dist_dir="$repo_root/simulator/web/dist"
artifacts_dir="$repo_root/simulator/web/artifacts"

if [[ "$version" != "4.0.18" ]]; then
    echo "error: unsupported Emscripten version: $version" >&2
    exit 1
fi
if [[ ! -f "$emsdk_root/emsdk_env.sh" ]]; then
    echo "error: pinned emsdk is missing; run scripts/install-emsdk.sh" >&2
    exit 1
fi

# emsdk_env.sh configures this process only; no global SDK state is changed here.
export EMSDK_QUIET=1
source "$emsdk_root/emsdk_env.sh" >/dev/null
actual_version="$(emcc --version | sed -nE '1s/.*\) ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p')"
if [[ "$actual_version" != "$version" ]]; then
    echo "error: expected emcc $version, got $actual_version" >&2
    exit 1
fi

mkdir -p "$build_dir" "$dist_dir" "$artifacts_dir"
node simulator/web/tests/scenarios.mjs --emit-c > "$build_dir/scenarios.generated.h"

common_sources=(
    components/app_core/app_state.c
    components/app_core/app_screen.c
    components/notification_core/notification_queue.c
    components/notification_core/notification_normalizer.c
    simulator/wasm/simulator_api.c
)
common_flags=(
    -std=c17 -O2 -Wall -Wextra -Werror
    -Icomponents/app_core/include
    -Icomponents/notification_core/include
    -Isimulator/wasm
)

"${CC:-cc}" "${common_sources[@]}" simulator/wasm/native_scenarios.c \
    "${common_flags[@]}" -I"$build_dir" -o "$build_dir/native-scenarios"
"$build_dir/native-scenarios" > "$artifacts_dir/native-scenarios.json"

emcc "${common_sources[@]}" "${common_flags[@]}" \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sWASM_BIGINT=1 \
    -sALLOW_MEMORY_GROWTH=0 -sFILESYSTEM=0 \
    -sEXPORTED_FUNCTIONS=@simulator/wasm/exports.json \
    -sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
    -o "$dist_dir/mk3k-simulator.mjs"

echo "built native scenarios and WebAssembly simulator with emcc $actual_version"
