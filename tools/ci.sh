#!/usr/bin/env bash
# Full local verification: the gate every change passes before merge.
#
#   ./tools/ci.sh            # everything
#   CI_SKIP_ARM=1 ./tools/ci.sh   # host + web only (no SDK / toolchain)
#
# Steps: printf ban, web bundle up to date + size budget, web tests,
# host tests (-Werror), host tests under ASan/UBSan, ARM build of both banks,
# flash/RAM budgets, firmware version embedded in both banks.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$ROOT/firmware/test"
HOST_TIMEOUT="${CI_HOST_TIMEOUT:-300}"
SAN_TIMEOUT="${CI_SAN_TIMEOUT:-900}"
WEB_GZ_BUDGET="${CI_WEB_GZ_BUDGET:-24576}"
BANK_BUDGET=$((952 * 1024))
SYSRAM_BUDGET=$((359 * 1024))

pass=()
fail=()

step() { printf '\n==> %s\n' "$1"; }
ok() { pass+=("$1"); printf 'PASS  %s\n' "$1"; }
ko() { fail+=("$1"); printf 'FAIL  %s\n' "$1"; }

run() {
    local name="$1"
    shift
    step "$name"
    if "$@"; then ok "$name"; else ko "$name"; fi
}

host_flags="-Wall -Wextra -std=c11 -pedantic -Werror"
san_flags="$host_flags -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"

no_printf() { make -s -C "$TEST_DIR" check-no-printf; }

# Every firmware source is in the target build, and every listed file exists.
file_lists() {
    local mk="$ROOT/firmware/GCC/Makefile" rc=0 f name
    for f in "$ROOT"/firmware/src/*.c; do
        name="$(basename "$f")"
        case "$name" in bl_*.c) continue ;; esac   # bootloader sources (SDK patches)
        grep -q "APP_PATH_SRC)/$name\b" "$mk" || { echo "not in GCC/Makefile: src/$name" >&2; rc=1; }
    done
    while read -r name; do
        [ -f "$ROOT/firmware/src/$name" ] || { echo "GCC/Makefile lists missing src/$name" >&2; rc=1; }
    done < <(sed -n 's#.*APP_PATH_SRC)/\([A-Za-z0-9_]*\.c\).*#\1#p' "$mk")
    return $rc
}

web_bundle() {
    local out="$ROOT/firmware/src/web_ui_gz.c" bytes
    "$ROOT/tools/web/build.sh" >/dev/null
    if ! git -C "$ROOT" diff --quiet -- firmware/src/web_ui_gz.c; then
        echo "web_ui_gz.c is stale: commit the regenerated file" >&2
        return 1
    fi
    bytes=$(sed -n 's/.*g_web_ui_gz_len = \([0-9]*\).*/\1/p' "$out" | head -1)
    if [ -z "$bytes" ]; then
        bytes=$(grep -o '0x[0-9a-fA-F][0-9a-fA-F]' "$out" | wc -l)
    fi
    echo "web bundle: $bytes bytes gzipped (budget $WEB_GZ_BUDGET)"
    [ "$bytes" -le "$WEB_GZ_BUDGET" ]
}

web_tests() { make -s -C "$ROOT/tools/web" test-web; }

host_tests() {
    make -s -C "$TEST_DIR" BUILD=build-ci CFLAGS="$host_flags" web-ui-gz build-ci/test_host &&
        (cd "$TEST_DIR" && timeout "$HOST_TIMEOUT" ./build-ci/test_host | tail -3)
}

host_sanitizers() {
    make -s -C "$TEST_DIR" BUILD=build-san CFLAGS="$san_flags" \
        LDFLAGS="-fsanitize=address,undefined" build-san/test_host &&
        (cd "$TEST_DIR" && ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
            timeout "$SAN_TIMEOUT" ./build-san/test_host | tail -3)
}

arm_build() {
    (cd "$ROOT" && ./tools/build-firmware.sh >/tmp/ci-arm-build.log 2>&1) || {
        tail -40 /tmp/ci-arm-build.log >&2
        return 1
    }
}

arm_budgets() {
    local flash="$ROOT/firmware/flash" bin size elf data bss ram rc=0
    for bin in petfeeder_a.bin petfeeder_b.bin; do
        size=$(wc -c < "$flash/$bin")
        printf '%s: %d / %d bytes (%d%%)\n' "$bin" "$size" "$BANK_BUDGET" $((size * 100 / BANK_BUDGET))
        [ "$size" -le "$BANK_BUDGET" ] || rc=1
    done
    elf="$(dirname "$(readlink -f "$flash/petfeeder_a.bin")")/petfeeder.elf"
    [ -f "$elf" ] || elf="$ROOT/external/LinkitSDK_OUT/mt7682_hdk/petfeeder/petfeeder.elf"
    if [ -f "$elf" ]; then
        read -r _ data bss _ < <(arm-none-eabi-size "$elf" | tail -1)
        ram=$((data + bss))
        printf 'static RAM (data+bss): %d bytes (SYSRAM+TCM, SYSRAM %d)\n' "$ram" "$SYSRAM_BUDGET"
    fi
    return $rc
}

arm_version() {
    local want rc=0 bin
    want="$(cat "$ROOT/firmware/.fw_version" 2>/dev/null || true)"
    [ -n "$want" ] || { echo "no firmware/.fw_version" >&2; return 1; }
    for bin in petfeeder_a.bin petfeeder_b.bin; do
        if grep -aq -- "$want" "$ROOT/firmware/flash/$bin"; then
            echo "$bin embeds version $want"
        else
            echo "$bin does not embed version $want" >&2
            rc=1
        fi
    done
    return $rc
}

run "no printf in firmware/src" no_printf
run "build file lists match the sources" file_lists
run "web bundle up to date and within budget" web_bundle
run "web tests" web_tests
run "host tests (-Werror)" host_tests
run "host tests (ASan + UBSan)" host_sanitizers

if [ "${CI_SKIP_ARM:-0}" = "1" ]; then
    step "ARM build skipped (CI_SKIP_ARM=1)"
elif ! command -v arm-none-eabi-gcc >/dev/null || [ ! -d "$ROOT/external/linkit-sdk-v4.7.1" ]; then
    step "ARM build skipped (toolchain or SDK missing: run ./tools/bootstrap.sh)"
else
    step "ARM build (bank A + bank B)"
    if arm_build; then
        ok "ARM build (bank A + bank B)"
        run "flash and RAM budgets" arm_budgets
        run "firmware version in both banks" arm_version
    else
        ko "ARM build (bank A + bank B)"
    fi
fi

printf '\n%d passed, %d failed\n' "${#pass[@]}" "${#fail[@]}"
for f in "${fail[@]+"${fail[@]}"}"; do printf '  FAIL %s\n' "$f"; done
[ "${#fail[@]}" -eq 0 ]
