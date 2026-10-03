#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(cd -- "$test_dir/../.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/bubududu-host-tests.XXXXXX")"
compiler="${CXX:-c++}"
flags=(-std=c++11 -Wall -Wextra -Werror
       -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
       -I"$test_dir" -I"$repo_dir/include")

run_test() {
    local source="$1" identity="$2"
    shift 2
    local name="$(basename -- "$source" .cpp)" identity_flags=()
    if [[ -n "$identity" ]]; then
        name="${name}_${identity}"
        identity_flags=(-D"DEVICE_$identity")
    fi
    echo "HOST: $name"
    "$compiler" "${flags[@]}" ${identity_flags[@]+"${identity_flags[@]}"} "$@" \
        "$source" -o "$build_dir/$name"
    "$build_dir/$name"
}

# Bash 3.2 (macOS) and Ubuntu Bash both support these arrays and nullglob.
# Discovery ensures a new production-loop suite cannot be silently omitted.
shopt -s nullglob
suites=("$test_dir"/suites/*_test.cpp)
[[ ${#suites[@]} -gt 0 ]] || { echo "No production-loop suites found" >&2; exit 1; }
for device in BUBU DUDU; do
    for suite in "${suites[@]}"; do
        run_test "$suite" "$device"
    done
    run_test "$test_dir/cc1101_wake_test.cpp" "$device" -I"$test_dir/wake" -I"$test_dir/cc1101"
    run_test "$test_dir/cc1101_wake_tx_test.cpp" "$device" -I"$test_dir/cc1101"
    run_test "$test_dir/cc1101_wake_forward_test.cpp" "$device" -I"$test_dir/wake" -I"$test_dir/cc1101"
    run_test "$test_dir/espnow_drain_test.cpp" "$device" -I"$test_dir/espnow"
    run_test "$test_dir/i2c_startup_test.cpp" "$device" -I"$test_dir/motion"
done
run_test "$test_dir/cc1101_sleep_arm_test.cpp" "" -I"$test_dir/cc1101"
run_test "$test_dir/rtc_state_test.cpp" ""
run_test "$test_dir/motion_sleep_test.cpp" "" -I"$test_dir/motion"
run_test "$test_dir/display_status_test.cpp" "" -I"$test_dir/motion"
python3 "$test_dir/port_selection_test.py"
echo "PASS: ${#suites[@]} production-loop suites per identity; complete host suite"
echo "Host binaries: $build_dir"
