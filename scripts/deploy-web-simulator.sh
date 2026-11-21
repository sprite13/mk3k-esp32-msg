#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
host=""
release=""
rollback=0
package_dir=""
remote_base="${MK3K_REMOTE_BASE:-/var/www/mk3k-esp32-preview}"
ssh_bin="${MK3K_SSH_BIN:-ssh}"

die() {
    echo "error: $*" >&2
    exit 1
}

usage() {
    echo "usage: $0 --host HOST [--release ID] PACKAGE-DIRECTORY" >&2
    echo "       $0 --host HOST --rollback" >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)
            [[ $# -ge 2 ]] || usage
            host="$2"
            shift 2
            ;;
        --release)
            [[ $# -ge 2 ]] || usage
            release="$2"
            shift 2
            ;;
        --rollback)
            rollback=1
            shift
            ;;
        --help|-h)
            usage
            ;;
        --*)
            usage
            ;;
        *)
            [[ -z "$package_dir" ]] || usage
            package_dir="$1"
            shift
            ;;
    esac
done

[[ "$host" =~ ^[A-Za-z0-9]([A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$ ]] \
    || die "invalid host name"
[[ "$remote_base" =~ ^/[A-Za-z0-9._/-]+$ && "$remote_base" != *".."* && "$remote_base" != */ ]] \
    || die "invalid remote base path"
ssh_path="$(command -v -- "$ssh_bin" 2>/dev/null || true)"
[[ -n "$ssh_path" && -x "$ssh_path" ]] || die "ssh executable not found: $ssh_bin"
ssh_bin="$ssh_path"

remote_program() {
    cat <<'REMOTE'
set -euo pipefail
mode="$1"
base="$2"
release="${3:-}"
[[ "$base" =~ ^/[A-Za-z0-9._/-]+$ && "$base" != *".."* && "$base" != */ ]] || exit 64
if [[ -n "$release" ]]; then
    [[ "$release" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] || exit 64
fi

link_target() {
    local name="$1" target
    if [[ ! -e "$base/$name" && ! -L "$base/$name" ]]; then
        printf '%s' ''
        return
    fi
    [[ -L "$base/$name" ]] || { echo "error: $name is not a symlink" >&2; exit 1; }
    target="$(readlink "$base/$name")"
    [[ "$target" =~ ^releases/[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] \
        || { echo "error: unsafe $name target" >&2; exit 1; }
    [[ -d "$base/$target" ]] || { echo "error: missing $name target" >&2; exit 1; }
    printf '%s' "$target"
}

set_link() {
    local name="$1" target="$2" temporary
    temporary="$base/.${name}.new.$$"
    [[ ! -e "$base/$name" || -L "$base/$name" ]] || { echo "error: $name is not a symlink" >&2; return 1; }
    rm -f -- "$temporary"
    if [[ -z "$target" ]]; then
        rm -f -- "$base/$name"
        return
    fi
    [[ "$target" =~ ^releases/[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ && -d "$base/$target" ]] \
        || { echo "error: refusing unsafe symlink target" >&2; return 1; }
    ln -s -- "$target" "$temporary"
    mv -Tf -- "$temporary" "$base/$name"
}

validate_staging() {
    local staging="$1" expected actual manifest_files runtime_files file
    expected="$(cat <<'ENTRIES'
d assets
d dist
d src
d styles
f SHA256SUMS
f assets/m5stick-outline.svg
f dist/mk3k-simulator.mjs
f dist/mk3k-simulator.wasm
f index.html
f src/accessibility.mjs
f src/app.mjs
f src/audio.mjs
f src/controls.mjs
f src/device-renderer.mjs
f src/frame-scheduler.mjs
f src/interactions.mjs
f src/scenario-controller.mjs
f src/scenarios.mjs
f src/startup-controller.mjs
f src/wasm-client.mjs
f styles/main.css
ENTRIES
)"
    actual="$(cd "$staging" && find . -mindepth 1 -printf '%y %P\n' | LC_ALL=C sort)"
    [[ "$actual" == "$expected" ]] || {
        echo "error: staged entries or types differ from the runtime allowlist" >&2
        diff -u <(printf '%s\n' "$expected") <(printf '%s\n' "$actual") >&2 || true
        return 1
    }
    runtime_files="$(printf '%s\n' "$expected" | sed -n 's/^f //p' | grep -v '^SHA256SUMS$')"
    manifest_files="$(sed -E 's/^[0-9a-f]{64}  //' "$staging/SHA256SUMS" | LC_ALL=C sort)"
    [[ "$manifest_files" == "$runtime_files" ]] \
        || { echo "error: staged manifest does not exactly cover runtime files" >&2; return 1; }
    while IFS= read -r file; do
        [[ "$(stat -c '%h' "$staging/$file")" == 1 ]] \
            || { echo "error: staged runtime file is a hardlink: $file" >&2; return 1; }
    done < <(printf '%s\n' "$runtime_files" SHA256SUMS)
    (cd "$staging" && sha256sum -c SHA256SUMS)
}

activate_links() (
    local next_current="$1" next_previous="$2"
    local old_current old_previous committed=0 recovery_started=0 reload_attempted=0
    old_current="$(link_target current)"
    old_previous="$(link_target previous)"

    recover_activation() {
        local status=$? previous_status current_status
        [[ $recovery_started -eq 0 ]] || exit "$status"
        recovery_started=1
        trap - EXIT ERR
        trap '' INT TERM HUP
        if [[ $committed -eq 0 ]]; then
            set +e
            set_link previous "$old_previous"
            previous_status=$?
            set_link current "$old_current"
            current_status=$?
            if [[ $previous_status -ne 0 || $current_status -ne 0 ]]; then
                echo "error: activation recovery could not restore both release links" >&2
                status=1
            elif [[ $reload_attempted -eq 1 && -n "$old_current" ]]; then
                nginx -t >/dev/null 2>&1 && systemctl reload nginx >/dev/null 2>&1 || true
            fi
        fi
        exit "$status"
    }
    activation_error() {
        local status=$?
        trap - ERR
        exit "$status"
    }
    activation_signal() {
        local status="$1"
        trap '' INT TERM HUP
        exit "$status"
    }

    set -E
    trap recover_activation EXIT
    trap activation_error ERR
    trap 'activation_signal 130' INT
    trap 'activation_signal 143' TERM
    trap 'activation_signal 129' HUP

    # previous is bookkeeping; current is service-critical and changes last.
    set_link previous "$next_previous"
    if [[ "${MK3K_DEPLOY_TEST_FAIL_AFTER_PREVIOUS:-}" == "1" ]]; then
        false
    fi
    if [[ "${MK3K_DEPLOY_TEST_TERM_AFTER_PREVIOUS:-}" == "1" ]]; then
        kill -TERM "$BASHPID"
    fi
    set_link current "$next_current"
    nginx -t
    reload_attempted=1
    systemctl reload nginx
    committed=1
    trap - EXIT ERR INT TERM HUP
)

case "$mode" in
    prepare)
        [[ -n "$release" ]] || exit 64
        mkdir -p -- "$base/releases"
        chmod 0755 "$base" "$base/releases"
        [[ ! -e "$base/releases/$release" && ! -L "$base/releases/$release" ]] \
            || { echo "error: release already exists: $release" >&2; exit 1; }
        [[ ! -e "$base/.incoming-$release" && ! -L "$base/.incoming-$release" ]] \
            || { echo "error: staging path already exists: $release" >&2; exit 1; }
        mkdir -- "$base/.incoming-$release"
        chmod 0755 "$base/.incoming-$release"
        ;;
    install)
        [[ -n "$release" ]] || exit 64
        staging="$base/.incoming-$release"
        [[ -d "$staging" && ! -L "$staging" ]] || { echo "error: missing staging directory" >&2; exit 1; }
        validate_staging "$staging"
        chown -R root:root "$staging"
        find "$staging" -type f -exec chmod 0444 {} +
        chmod 0755 "$base" "$base/releases"
        mv -- "$staging" "$base/releases/$release"
        find "$base/releases/$release" -type d -exec chmod 0555 {} +
        current="$(link_target current)"
        activate_links "releases/$release" "$current"
        echo "activated release $release"
        ;;
    rollback)
        current="$(link_target current)"
        previous="$(link_target previous)"
        [[ -n "$current" ]] || { echo "error: no active release" >&2; exit 1; }
        [[ -n "$previous" ]] || { echo "error: no previous release" >&2; exit 1; }
        activate_links "$previous" "$current"
        echo "rolled back to ${previous#releases/}"
        ;;
    *)
        exit 64
        ;;
esac
REMOTE
}

remote_call() {
    local mode="$1" release_arg="${2:-}"
    remote_program | "$ssh_bin" -T "clawd@$host" sudo bash -s -- "$mode" "$remote_base" "$release_arg"
}

if [[ $rollback -eq 1 ]]; then
    [[ -z "$package_dir" && -z "$release" ]] || usage
    remote_call rollback
    exit 0
fi

[[ -n "$package_dir" ]] || usage
package_dir="$(cd -- "$package_dir" 2>/dev/null && pwd -P)" || die "package directory does not exist"
"$repo_root/scripts/package-web-simulator.sh" --check "$package_dir"
if [[ -z "$release" ]]; then
    short_hash="$(sha256sum "$package_dir/SHA256SUMS" | cut -c1-12)"
    release="$(date -u +%Y%m%dT%H%M%SZ)-$short_hash"
fi
[[ "$release" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] || die "invalid release identifier"

remote_call prepare "$release"
if ! tar -C "$package_dir" -cf - . \
    | "$ssh_bin" -T "clawd@$host" sudo tar -xf - -C "$remote_base/.incoming-$release"; then
    die "release upload failed; staging was left for inspection"
fi
remote_call install "$release"
