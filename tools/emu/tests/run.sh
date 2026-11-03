#!/bin/bash
# Native-first tests. Portable suites use real ESP32-S3 Arduino/ESP-IDF;
# host-only UI and failure-injection suites use the desktop harness.
#
#   bash tools/emu/tests/run.sh          # everything
#   bash tools/emu/tests/run.sh battle   # just the ones matching "battle"
#   TAMA_TEST_JOBS=6 bash tools/emu/tests/run.sh
#
# Two of these exist because nothing else can catch what they catch:
#   flush_test  -- a screen that never calls gfx->flush() leaves the panel
#                  frozen, and headless screenshots CANNOT see it: --shot reads
#                  gfx->buffer() straight out and never consults frameReady.
#                  The training submenu shipped frozen exactly this way.
#   i18n_test   -- STRINGS is positional; a short language row is zero-padded by
#                  the compiler with no diagnostic, silently shifting every
#                  string after the gap in that language only.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
EMU="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$EMU/../.." && pwd)"
FILTER="${1:-}"
BACKEND="${TAMA_TEST_BACKEND:-auto}"
SPRITE_DIR="$ROOT/tools/sdcard/mons"

[ "$#" -le 1 ] || { echo "Only one substring filter is supported" >&2; exit 1; }
for tool in node python3 arduino-cli; do
  command -v "$tool" >/dev/null || { echo "Mandatory test tool unavailable: $tool" >&2; exit 1; }
done
bounded() { node "$HERE/bounded.mjs" "$@"; }

if [ -n "${TAMA_TEST_LOG_DIR:-}" ]; then
  mkdir -p "$TAMA_TEST_LOG_DIR"
  OUT="$(mktemp -d "$TAMA_TEST_LOG_DIR/run.XXXXXX")"
else
  OUT="$(mktemp -d)"
fi
OUT_FW="$OUT/firmware.log"
cleanup() {
  kill $(jobs -p) 2>/dev/null || true
  if [ -z "${TAMA_TEST_LOG_DIR:-}" ]; then rm -rf "$OUT"; else echo "Test logs: $OUT"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

bounded 30 -- node "$HERE/backends.mjs" "$FILTER" "$BACKEND" >"$OUT/routing.txt"
selected=(); host_selected=(); native_selected=()
while IFS=$'\t' read -r backend name; do
  selected+=("$HERE/$name.cpp")
  if [ "$backend" = native ]; then
    native_selected+=("$name")
  else
    host_selected+=("$HERE/$name.cpp")
    if [ "$name" = sprite_test ] && [ ! -d "$SPRITE_DIR" ]; then
      echo "Mandatory sprite coverage unavailable: run python3 tools/unpack_bundle.py" >&2
      exit 1
    fi
  fi
done <"$OUT/routing.txt"
ESP_EMULATOR=""
if [ "${#native_selected[@]}" -gt 0 ] || { [ "$BACKEND" = auto ] && [ -z "$FILTER" ]; }; then
  ESP_EMULATOR="$(command -v "${TAMA_ESP_EMULATOR:-esp-emu}" || true)"
  [ -n "$ESP_EMULATOR" ] || {
    echo "Mandatory native emulator unavailable: set TAMA_ESP_EMULATOR=/path/to/esp-emu (v0.44.0). No silent host fallback." >&2
    exit 1
  }
fi
if [ "${#host_selected[@]}" -gt 0 ]; then
  for tool in g++ sdl2-config; do
    command -v "$tool" >/dev/null || { echo "Mandatory host test tool unavailable: $tool" >&2; exit 1; }
  done
  if ! bounded 180 -- bash "$EMU/build.sh" >"$OUT/emulator.log" 2>&1; then
    cat "$OUT/emulator.log"
    echo "=== mandatory emulator build FAILED"
    exit 1
  fi
fi
echo "=== test routing: ${#native_selected[@]} native, ${#host_selected[@]} host (mode=$BACKEND)"
bounded 30 --require 'PASS bounded gates:' -- node "$HERE/bounded.mjs" --self-test

# The emulator generates proto.h with every prototype at the top, so it will
# happily compile a sketch that arduino-cli rejects for using a function before
# it is declared. That shipped once. The mandatory firmware build decides.
fw_pid=""
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
bounded 600 -- arduino-cli compile --fqbn "$FQBN" "$ROOT" >"$OUT_FW" 2>&1 &
fw_pid=$!

# The browser halves of the save backup and the pack upload. They are JavaScript,
# so they cannot run in the C++ harness below -- and they went untested for exactly
# that reason while the firmware side had suites. Both are mandatory.
echo "=== check_savefile (web/savefile.js)"
bounded 60 -- node "$ROOT/tools/check_savefile.mjs"
echo "=== check_packs (web/packs.js)"
bounded 60 -- node "$ROOT/tools/check_packs.mjs"

# arrays, not a string: the sprite dir has to reach the compiler still quoted,
# and passing these through eval silently strips them
CORE=("$ROOT/gbsynth.cpp" "$ROOT/pet.cpp" "$ROOT/i18n.cpp" "$ROOT/party.cpp" "$ROOT/battle.cpp" "$ROOT/link.cpp" "$ROOT/save.cpp" "$ROOT/inventory.cpp" "$ROOT/wild.cpp")
FLAGS=(-std=c++17 -O1 -w -I"$EMU" -I"$ROOT" -DSPRITE_DIR="\"$SPRITE_DIR\"")

# these drive setup()/loop()/render(), so they need the sketch itself
needs_sketch() { case "$1" in touch_test|flush_test|joy_test|anim_test|swipe_test|lan_test|console_test|hit_test|starter_test|release_test|focus_test|explore_test|view_test|transfer_test|dimflash_test) return 0;; *) return 1;; esac; }

# and these are standalone: gbsynth.cpp has no Arduino dependency at all, which
# is the point of it -- linking the game core in would only demand stubs for
# symbols the test never calls.
standalone() { case "$1" in audio_test|synth_test|palette_test|cry_test|crc32_test) return 0;; *) return 1;; esac; }

# sprite_test drives PmdMon straight off the sprite directory, so it needs the
# host's SD stubs but none of the sketch
needs_host() { case "$1" in sprite_test) return 0;; *) return 1;; esac; }

cpu_count=2
if command -v sysctl >/dev/null; then
  cpu_count="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
elif command -v getconf >/dev/null; then
  cpu_count="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
fi
case "$cpu_count" in ''|*[!0-9]*) cpu_count=2;; esac
test_jobs="${TAMA_TEST_JOBS:-$cpu_count}"
case "$test_jobs" in ''|*[!0-9]*|0) echo "TAMA_TEST_JOBS must be a positive integer" >&2; exit 1;; esac

# Bash 3 on macOS has no `wait -n`, so throttle on running jobs, not counting the firmware build.
throttle() {
  while (( $(jobs -rp | grep -vx "${fw_pid:-0}" | wc -l) >= test_jobs )); do
    for running in $(jobs -rp); do
      if [ "$running" != "$fw_pid" ]; then wait "$running" || true; break; fi
    done
  done
}

# Every suite links the same firmware sources, so compile them once rather than per suite.
OBJ="$OUT/obj"
mkdir -p "$OBJ"
CORE_O=()
for f in "${CORE[@]}"; do CORE_O+=("$OBJ/$(basename "$f" .cpp).o"); done
SKETCH_O=("$OBJ/sketch.o" "$OBJ/host_impl.o" "$OBJ/font.o" "$OBJ/clock.o")
HOST_O=("$OBJ/host_impl.o" "$OBJ/font.o")
obj_pids=()
if [ "${#host_selected[@]}" -gt 0 ]; then
for f in "${CORE[@]}" "$EMU/sketch.cpp" "$EMU/host_impl.cpp" "$EMU/font.cpp" "$EMU/clock.cpp"; do
  throttle
  bounded 120 -- g++ "${FLAGS[@]}" -c "$f" -o "$OBJ/$(basename "$f" .cpp).o" >"$OBJ/$(basename "$f" .cpp).log" 2>&1 &
  obj_pids+=("$!")
done
obj_failed=0
for pid in "${obj_pids[@]}"; do wait "$pid" || obj_failed=1; done
if [ "$obj_failed" -ne 0 ]; then
  echo "=== the shared sources DID NOT COMPILE"
  for log in "$OBJ"/*.log; do
    [ -s "$log" ] && { echo "--- $(basename "$log" .log)"; tail -5 "$log"; }
  done
  exit 1
fi
fi

run_one() {
  src="$1"
  name="$(basename "$src" .cpp)"
  echo "CHECK compiling $name"
  suite_out="$OUT/$name"
  mkdir -p "$suite_out"
  objs=("${CORE_O[@]}")
  standalone "$name" && objs=("$OBJ/gbsynth.o")
  needs_sketch "$name" && objs+=("${SKETCH_O[@]}")
  needs_host "$name" && objs+=("${HOST_O[@]}")
  suite_flags=("${FLAGS[@]}")
  suite_sources=("$src")
  case "$name" in
    audio_test) suite_flags+=(-I"$HERE/audio_stubs" -pthread);;
    workflow_test)
      suite_flags+=(-I"$HERE/audio_stubs" -pthread -DTAMA_REAL_AUDIO -DTAMA_DETERMINISTIC_CLOCK)
      suite_sources+=("$EMU/host_impl.cpp" "$EMU/clock.cpp")
      objs+=("$OBJ/font.o")
      ;;
  esac
  if ! bounded 120 -- g++ "${suite_flags[@]}" -o "$suite_out/test" "${suite_sources[@]}" "${objs[@]}" >"$suite_out/compile.log" 2>&1; then
    { echo "=== $name: DID NOT COMPILE"; tail -5 "$suite_out/compile.log"; } >"$suite_out/output"
    echo fail >"$suite_out/status"
    echo "CHECK completed $name: compile failed"
    return
  fi
  # pipefail matters: piping the test through grep would otherwise report
  # grep's exit status and every failure would be counted as a pass
  test_command=(bounded 180)
  [ "$name" = workflow_test ] && test_command+=(--require 'PASS mandatory workflows complete:')
  test_command+=(-- ./test)
  if (cd "$suite_out" && "${test_command[@]}" 2>&1 | grep -vE '^(TamaDigi fw|emu:|RTC )') >"$suite_out/test.log"; then
    if [ "$name" = workflow_test ]; then
      { echo "=== $name"; grep -E '^PASS (workflow|mandatory)' "$suite_out/test.log"; } >"$suite_out/output"
    else
      { echo "=== $name"; cat "$suite_out/test.log"; } >"$suite_out/output"
    fi
    echo pass >"$suite_out/status"
  else
    { echo "=== $name"; cat "$suite_out/test.log"; echo "    ^ $name FAILED"; } >"$suite_out/output"
    echo fail >"$suite_out/status"
  fi
  echo "CHECK completed $name: $(cat "$suite_out/status")"
}

echo "=== host C++ suites (${#host_selected[@]} total, $test_jobs parallel jobs)"
# Each worker has its own cwd and output files, and emulator NVS lives in process memory.
suite_pids=()
if [ "${#host_selected[@]}" -gt 0 ]; then
for src in "${host_selected[@]}"; do
  throttle
  run_one "$src" &
  suite_pids+=("$!")
done
for pid in "${suite_pids[@]}"; do wait "$pid" || true; done
fi

pass=0; fail=0; skip=0
if [ "${#host_selected[@]}" -gt 0 ]; then
for src in "${host_selected[@]}"; do
  name="$(basename "$src" .cpp)"
  cat "$OUT/$name/output" 2>/dev/null || echo "=== $name: worker exited without a result"
  result="$(cat "$OUT/$name/status" 2>/dev/null || true)"
  case "$result" in
    pass) pass=$((pass+1));;
    skip) skip=$((skip+1));;
    *) fail=$((fail+1));;
  esac
done
fi

if [ "${#native_selected[@]}" -gt 0 ]; then
  echo "=== native ESP32-S3 suites (${#native_selected[@]} total)"
  if TAMA_TEST_LOG_DIR="$OUT" bounded 1250 --require "PASS migrated native suites: ${#native_selected[@]}" -- node "$HERE/native.mjs" "$ESP_EMULATOR" "${native_selected[@]}"; then
    pass=$((pass+${#native_selected[@]}))
  else
    fail=$((fail+${#native_selected[@]}))
  fi
fi

fw_ok=1
if [ -n "$fw_pid" ] && ! wait "$fw_pid"; then
  echo "=== THE FIRMWARE DOES NOT COMPILE (the emulator does; that is not the same thing)"
  tail -20 "$OUT_FW"
  fw_ok=0
fi
mutation_ok=1
native_ok=1
if [ -z "$FILTER" ]; then
  if [ "$BACKEND" = auto ]; then
    echo "=== native PSRAM, migration, reboot and real watchdog probe"
    if ! TAMA_TEST_LOG_DIR="$OUT" bounded 950 --require 'PASS native post-panic fresh-process persistence;' -- node "$HERE/native.mjs" "$ESP_EMULATOR"; then
      native_ok=0
    fi
    echo "=== native assertion-failure and restored-firmware control"
    if ! TAMA_TEST_LOG_DIR="$OUT" bounded 1900 --require 'PASS native failure control:' -- node "$HERE/native.mjs" "$ESP_EMULATOR" --negative-check; then
      native_ok=0
    fi
  fi
  echo "=== original-defect end-to-end mutations"
  if ! bounded 600 --require 'PASS mandatory original-defect mutations complete: 2' -- node "$HERE/mutations.mjs"; then
    mutation_ok=0
  fi
fi
echo
echo "suites passed: $pass, failed: $fail, skipped: $skip"
[ "$pass" -eq "${#selected[@]}" ] && [ "$fail" -eq 0 ] && [ "$skip" -eq 0 ] && [ "$fw_ok" -eq 1 ] && [ "$mutation_ok" -eq 1 ] && [ "$native_ok" -eq 1 ]
