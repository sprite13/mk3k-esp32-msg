#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifact_root="$repo_root/artifacts"
default_package="$artifact_root/mk3k-esp32-preview"

runtime_files=(
    index.html
    styles/main.css
    assets/m5stick-outline.svg
    dist/mk3k-simulator.mjs
    dist/mk3k-simulator.wasm
    src/accessibility.mjs
    src/app.mjs
    src/audio.mjs
    src/controls.mjs
    src/device-renderer.mjs
    src/frame-scheduler.mjs
    src/interactions.mjs
    src/scenario-controller.mjs
    src/scenarios.mjs
    src/startup-controller.mjs
    src/wasm-client.mjs
)

die() {
    echo "error: $*" >&2
    exit 1
}

validate_package() {
    local package_dir="$1"
    [[ -d "$package_dir" ]] || die "package directory does not exist: $package_dir"
    [[ ! -L "$package_dir" ]] || die "package root must not be a symlink"

    local expected actual
    expected="$({
        printf 'd %s\n' assets dist src styles
        printf 'f %s\n' "${runtime_files[@]}" SHA256SUMS
    } | LC_ALL=C sort)"
    actual="$(cd "$package_dir" && find . -mindepth 1 -printf '%y %P\n' | LC_ALL=C sort)"
    [[ "$actual" == "$expected" ]] || {
        echo "error: package entries or types differ from the runtime allowlist" >&2
        diff -u <(printf '%s\n' "$expected") <(printf '%s\n' "$actual") >&2 || true
        exit 1
    }
    if find "$package_dir" -mindepth 1 -type l -print -quit | grep -q .; then
        die "package contains a symlink"
    fi
    if find "$package_dir" -mindepth 1 \( -name '.*' -o -name '*.map' \) -print -quit | grep -q .; then
        die "package contains a dotfile or source map"
    fi
    grep -Fq '<meta name="robots" content="noindex,nofollow">' "$package_dir/index.html" \
        || die "index.html is missing the robots noindex meta tag"
    if grep -RInE --include='*.html' --include='*.css' --include='*.mjs' \
        '(^|["'"'"'(=[:space:]])/(assets|dist|src|styles)/' "$package_dir" >/dev/null; then
        die "package contains an absolute-root runtime URL"
    fi
    if grep -RInE --include='*.html' --include='*.css' --include='*.mjs' \
        'https?://[^/]' "$package_dir" >/dev/null; then
        die "package contains a remote HTTP resource"
    fi
    if grep -RInE --include='*.html' --include='*.css' --include='*.mjs' \
        '//[A-Za-z0-9][A-Za-z0-9.-]*[/:]' "$package_dir" >/dev/null; then
        die "package contains a protocol-relative remote resource"
    fi
    while IFS= read -r directory; do
        [[ "$(stat -c '%a' "$directory")" == 755 ]] || die "directory mode is not 0755: $directory"
    done < <(find "$package_dir" -type d -print)
    while IFS= read -r file; do
        [[ "$(stat -c '%a' "$file")" == 644 ]] || die "file mode is not 0644: $file"
        [[ "$(stat -c '%h' "$file")" == 1 ]] || die "runtime file must not be a hardlink: $file"
    done < <(find "$package_dir" -type f -print)
    (cd "$package_dir" && sha256sum -c SHA256SUMS >/dev/null) \
        || die "SHA256SUMS verification failed"
    local manifest_files
    manifest_files="$(sed -E 's/^[0-9a-f]{64}  //' "$package_dir/SHA256SUMS" | LC_ALL=C sort)"
    local runtime_sorted
    runtime_sorted="$(printf '%s\n' "${runtime_files[@]}" | LC_ALL=C sort)"
    [[ "$manifest_files" == "$runtime_sorted" ]] \
        || die "SHA256SUMS must cover every runtime file exactly once"
}

if [[ "${1:-}" == "--check" ]]; then
    [[ $# -le 2 ]] || die "usage: $0 --check [package-directory]"
    validate_package "${2:-$default_package}"
    echo "package is valid: ${2:-$default_package}"
    exit 0
fi
[[ $# -eq 0 ]] || die "usage: $0 [--check [package-directory]]"

"$repo_root/scripts/build-web-simulator.sh"
mkdir -p "$artifact_root"
staging="$(mktemp -d "$artifact_root/.mk3k-esp32-preview.XXXXXX")"
backup=""
new_package_installed=0
cleanup_started=0

remove_temporary_tree() {
    local path="$1"
    case "$path" in
        "$artifact_root"/.mk3k-esp32-preview.*) ;;
        *)
            echo "error: refusing to clean unexpected temporary path: $path" >&2
            return 1
            ;;
    esac
    [[ ! -e "$path" && ! -L "$path" ]] || find "$path" -depth -delete
}

cleanup() {
    local status=$? cleanup_status=0
    [[ $cleanup_started -eq 0 ]] || exit "$status"
    cleanup_started=1
    trap - EXIT
    trap '' INT TERM HUP
    set +e

    if [[ -n "$backup" && -d "$backup" ]]; then
        if [[ $new_package_installed -eq 0 ]]; then
            if [[ ! -e "$default_package" && ! -L "$default_package" ]]; then
                if [[ "${MK3K_PACKAGE_TEST_SECOND_TERM_DURING_RESTORE:-}" == "1" ]]; then
                    kill -TERM "$$"
                fi
                if mv -- "$backup" "$default_package"; then
                    backup=""
                else
                    echo "error: could not restore the previous package: $backup" >&2
                    cleanup_status=1
                fi
            else
                echo "warning: preserving backup because package state is ambiguous: $backup" >&2
            fi
        else
            # Narrow fault injection used only by the package transaction regression test.
            if [[ "${MK3K_PACKAGE_TEST_INTERRUPT_DURING_BACKUP_CLEANUP:-}" == "1" ]]; then
                kill -TERM "$$"
            fi
            if ! remove_temporary_tree "$backup"; then
                echo "warning: new package is valid, but old backup cleanup failed: $backup" >&2
            else
                backup=""
            fi
        fi
    fi
    if [[ -n "$staging" ]] && ! remove_temporary_tree "$staging"; then
        echo "warning: temporary package cleanup failed: $staging" >&2
    fi
    if [[ $cleanup_status -ne 0 ]]; then
        status=$cleanup_status
    fi
    exit "$status"
}

on_signal() {
    local status="$1"
    trap '' INT TERM HUP
    exit "$status"
}

# EXIT covers normal completion, explicit errors, and set -e failures. Signal
# handlers preserve the conventional status and then enter the same transaction.
trap cleanup EXIT
trap 'on_signal 130' INT
trap 'on_signal 143' TERM
trap 'on_signal 129' HUP

for relative in "${runtime_files[@]}"; do
    source_file="$repo_root/simulator/web/$relative"
    [[ -f "$source_file" && ! -L "$source_file" ]] || die "missing runtime source: $relative"
    mkdir -p "$staging/$(dirname "$relative")"
    cp -- "$source_file" "$staging/$relative"
done
find "$staging" -type d -exec chmod 0755 {} +
find "$staging" -type f -exec chmod 0644 {} +
(
    cd "$staging"
    printf '%s\n' "${runtime_files[@]}" | LC_ALL=C sort | xargs -r sha256sum > SHA256SUMS
)
chmod 0644 "$staging/SHA256SUMS"
validate_package "$staging"

if [[ -e "$default_package" || -L "$default_package" ]]; then
    [[ -d "$default_package" && ! -L "$default_package" ]] || die "refusing to replace non-directory package path"
    backup="$artifact_root/.mk3k-esp32-preview.previous.$$"
    [[ ! -e "$backup" ]] || die "temporary backup already exists: $backup"
    mv -- "$default_package" "$backup"
    # Narrow fault injection used only by the package transaction regression test.
    if [[ "${MK3K_PACKAGE_TEST_INTERRUPT_AFTER_BACKUP:-}" == "1" ]]; then
        kill -TERM "$$"
    fi
fi
if ! mv -- "$staging" "$default_package"; then
    die "could not install packaged artifact"
fi
new_package_installed=1
staging=""
echo "packaged WebAssembly simulator: $default_package"
