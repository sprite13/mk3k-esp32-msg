#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version_file="$repo_root/.emscripten-version"
version="$(tr -d '\r\n' < "$version_file")"
emsdk_root="${MK3K_EMSDK_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/mk3k-emsdk}"
expected_origin="https://github.com/emscripten-core/emsdk.git"

if [[ "$version" != "4.0.18" ]] || [[ "$(wc -l < "$version_file")" -ne 1 ]]; then
    echo "error: .emscripten-version must contain exactly 4.0.18" >&2
    exit 1
fi

if [[ ! -e "$emsdk_root" ]]; then
    mkdir -p "$(dirname "$emsdk_root")"
    git clone --depth 1 --branch "$version" "$expected_origin" "$emsdk_root"
elif [[ ! -d "$emsdk_root/.git" ]]; then
    echo "error: refusing to overwrite non-emsdk path: $emsdk_root" >&2
    exit 1
fi

origin="$(git -C "$emsdk_root" remote get-url origin)"
if [[ "$origin" != "$expected_origin" ]]; then
    echo "error: unexpected emsdk origin at $emsdk_root: $origin" >&2
    exit 1
fi

head_commit="$(git -C "$emsdk_root" rev-parse HEAD)"
tag_commit="$(git -C "$emsdk_root" rev-parse "refs/tags/$version^{commit}" 2>/dev/null || true)"
if [[ -z "$tag_commit" ]] || [[ "$head_commit" != "$tag_commit" ]]; then
    echo "error: $emsdk_root is not checked out at exact tag $version" >&2
    exit 1
fi

"$emsdk_root/emsdk" install "$version"
"$emsdk_root/emsdk" activate "$version"
echo "emsdk $version is active at $emsdk_root"
