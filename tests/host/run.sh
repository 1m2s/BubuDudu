#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(cd -- "$test_dir/../.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/bubududu-handshake-tests.XXXXXX")"
for device in BUBU DUDU; do
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -D"DEVICE_$device" -I"$test_dir" -I"$repo_dir/include" \
        "$test_dir/sleep_handshake_test.cpp" -o "$build_dir/test_$device"
    "$build_dir/test_$device"
done
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$test_dir/cc1101" -I"$test_dir" -I"$repo_dir/include" \
    "$test_dir/cc1101_sleep_arm_test.cpp" -o "$build_dir/test_cc1101_arm"
"$build_dir/test_cc1101_arm"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$test_dir" -I"$repo_dir/include" \
    "$test_dir/rtc_state_test.cpp" -o "$build_dir/test_rtc_state"
"$build_dir/test_rtc_state"
for device in BUBU DUDU; do
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -D"DEVICE_$device" -I"$test_dir/wake" -I"$test_dir/cc1101" -I"$test_dir" -I"$repo_dir/include" \
        "$test_dir/cc1101_wake_test.cpp" -o "$build_dir/test_cc1101_wake_$device"
    "$build_dir/test_cc1101_wake_$device"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -D"DEVICE_$device" -I"$test_dir/cc1101" -I"$test_dir" -I"$repo_dir/include" \
        "$test_dir/cc1101_wake_tx_test.cpp" -o "$build_dir/test_cc1101_wake_tx_$device"
    "$build_dir/test_cc1101_wake_tx_$device"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -D"DEVICE_$device" -I"$test_dir/wake" -I"$test_dir/cc1101" -I"$test_dir" -I"$repo_dir/include" \
        "$test_dir/cc1101_wake_forward_test.cpp" -o "$build_dir/test_cc1101_wake_forward_$device"
    "$build_dir/test_cc1101_wake_forward_$device"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -D"DEVICE_$device" -I"$test_dir/espnow" -I"$test_dir" -I"$repo_dir/include" \
        "$test_dir/espnow_drain_test.cpp" -o "$build_dir/test_espnow_drain_$device"
    "$build_dir/test_espnow_drain_$device"
done
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$test_dir/motion" -I"$test_dir" -I"$repo_dir/include" \
    "$test_dir/motion_sleep_test.cpp" -o "$build_dir/test_motion_sleep"
"$build_dir/test_motion_sleep"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$test_dir/motion" -I"$test_dir" -I"$repo_dir/include" \
    "$test_dir/display_status_test.cpp" -o "$build_dir/test_display_status"
"$build_dir/test_display_status"
for device in BUBU DUDU; do
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -D"DEVICE_$device" -I"$test_dir/motion" -I"$test_dir" -I"$repo_dir/include" \
        "$test_dir/i2c_startup_test.cpp" -o "$build_dir/test_i2c_startup_$device"
    "$build_dir/test_i2c_startup_$device"
done
echo "Host binaries: $build_dir"
