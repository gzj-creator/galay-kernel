#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

TEST_TIMEOUT_SECONDS="${GALAY_TEST_TIMEOUT_SECONDS:-30}"
TEST_TIMEOUT_KILL_AFTER_SECONDS="${GALAY_TEST_TIMEOUT_KILL_AFTER_SECONDS:-2}"
POSITIONAL_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --timeout-seconds)
      TEST_TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --kill-after-seconds)
      TEST_TIMEOUT_KILL_AFTER_SECONDS="$2"
      shift 2
      ;;
    --help)
      cat <<EOF
Usage: $0 [build_dir] [log_dir]
       $0 [--timeout-seconds N] [--kill-after-seconds N] [build_dir] [log_dir]
EOF
      exit 0
      ;;
    *)
      POSITIONAL_ARGS+=("$1")
      shift
      ;;
  esac
done

BUILD_DIR="${POSITIONAL_ARGS[0]:-$PROJECT_ROOT/build}"
BIN_DIR="$BUILD_DIR/bin"
LOG_DIR="${POSITIONAL_ARGS[1]:-$BUILD_DIR/test_matrix_logs}"

export GALAY_TEST_TCP_PORT="${GALAY_TEST_TCP_PORT:-28080}"
export GALAY_TEST_UDP_PORT="${GALAY_TEST_UDP_PORT:-28081}"

if ! [[ "$TEST_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
  echo "timeout-seconds must be a positive integer: $TEST_TIMEOUT_SECONDS" >&2
  exit 1
fi

if ! [[ "$TEST_TIMEOUT_KILL_AFTER_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
  echo "kill-after-seconds must be a positive integer: $TEST_TIMEOUT_KILL_AFTER_SECONDS" >&2
  exit 1
fi

mkdir -p "$LOG_DIR"

if [[ ! -d "$BIN_DIR" ]]; then
  echo "bin dir not found: $BIN_DIR" >&2
  exit 1
fi

PAIRED_TESTS=(
  "T3-tcp_server:T4-tcp_client"
  "T6-udp_server:T7-udp_client"
)

PAIR_EXCLUDES=(
  T3-tcp_server
  T4-tcp_client
  T6-udp_server
  T7-udp_client
)

discover_standalone_tests() {
  local pair_name
  local test_name
  while IFS= read -r source_path; do
    test_name="$(basename "${source_path%.cc}")"
    if [[ ! -f "$BIN_DIR/$test_name" ]]; then
      continue
    fi
    for pair_name in "${PAIR_EXCLUDES[@]}"; do
      if [[ "$test_name" == "$pair_name" ]]; then
        test_name=""
        break
      fi
    done
    if [[ -n "$test_name" ]]; then
      printf '%s\n' "$test_name"
    fi
  done < <(find "$PROJECT_ROOT/test" -maxdepth 1 -type f -name 'T*.cc' | sort)
}

STANDALONE_TESTS=()
while IFS= read -r test_name; do
  STANDALONE_TESTS+=("$test_name")
done < <(discover_standalone_tests)

cleanup_pid() {
  local pid="$1"
  if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid" 2>/dev/null || true
    for _ in {1..10}; do
      if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid" 2>/dev/null || true
        return 0
      fi
      sleep 0.1
    done
    kill -TERM "$pid" 2>/dev/null || true
    for _ in {1..10}; do
      if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid" 2>/dev/null || true
        return 0
      fi
      sleep 0.1
    done
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
}

resolve_timeout_bin() {
  if command -v timeout >/dev/null 2>&1; then
    printf '%s\n' "timeout"
    return 0
  fi
  if command -v gtimeout >/dev/null 2>&1; then
    printf '%s\n' "gtimeout"
    return 0
  fi
  printf '%s\n' ""
}

run_test_with_timeout() {
  local name="$1"
  local logfile="$2"
  shift 2
  local -a cmd=("$@")
  local timeout_bin

  timeout_bin="$(resolve_timeout_bin)"
  if [[ -n "$timeout_bin" ]]; then
    if "$timeout_bin" -k "${TEST_TIMEOUT_KILL_AFTER_SECONDS}s" "${TEST_TIMEOUT_SECONDS}s" "${cmd[@]}" \
      </dev/null >"$logfile" 2>&1; then
      return 0
    fi
    local status=$?
    if [[ "$status" -eq 124 || "$status" -eq 137 ]]; then
      echo "[test-timeout] ${name} exceeded ${TEST_TIMEOUT_SECONDS}s" >>"$logfile"
      echo "WARN: ${name} timed out after ${TEST_TIMEOUT_SECONDS}s" >&2
    fi
    return "$status"
  fi

  local timeout_flag
  timeout_flag="$(mktemp)"
  "${cmd[@]}" </dev/null >"$logfile" 2>&1 &
  local cmd_pid=$!
  (
    sleep "$TEST_TIMEOUT_SECONDS"
    if kill -0 "$cmd_pid" 2>/dev/null; then
      printf 'timeout\n' >"$timeout_flag"
      kill -TERM "$cmd_pid" 2>/dev/null || true
      sleep "$TEST_TIMEOUT_KILL_AFTER_SECONDS"
      kill -KILL "$cmd_pid" 2>/dev/null || true
    fi
  ) &
  local timer_pid=$!
  local status=0
  if ! wait "$cmd_pid"; then
    status=$?
  fi
  kill "$timer_pid" 2>/dev/null || true
  wait "$timer_pid" 2>/dev/null || true

  if [[ -s "$timeout_flag" ]]; then
    rm -f "$timeout_flag"
    echo "[test-timeout] ${name} exceeded ${TEST_TIMEOUT_SECONDS}s" >>"$logfile"
    echo "WARN: ${name} timed out after ${TEST_TIMEOUT_SECONDS}s" >&2
    return 124
  fi

  rm -f "$timeout_flag"
  return "$status"
}

wait_for_pid_with_timeout() {
  local name="$1"
  local logfile="$2"
  local pid="$3"
  local deadline=$((SECONDS + TEST_TIMEOUT_SECONDS))

  while kill -0 "$pid" 2>/dev/null; do
    if (( SECONDS >= deadline )); then
      echo "[test-timeout] ${name} exceeded ${TEST_TIMEOUT_SECONDS}s" >>"$logfile"
      echo "WARN: ${name} timed out after ${TEST_TIMEOUT_SECONDS}s" >&2
      cleanup_pid "$pid"
      return 124
    fi
    sleep 0.1
  done

  wait "$pid"
}

run_one() {
  local name="$1"
  local logfile="$LOG_DIR/${name}.log"
  echo "=== $name ==="
  run_test_with_timeout "$name" "$logfile" "$BIN_DIR/$name"
}

for test_name in "${STANDALONE_TESTS[@]-}"; do
  run_one "$test_name"
done

for pair in "${PAIRED_TESTS[@]}"; do
  IFS=':' read -r server client <<<"$pair"
  server_log="$LOG_DIR/${server}.log"
  client_log="$LOG_DIR/${client}.log"

  echo "=== $server + $client ==="
  "$BIN_DIR/$server" </dev/null >"$server_log" 2>&1 &
  server_pid=$!
  trap 'cleanup_pid "$server_pid"' EXIT
  sleep 1
  run_test_with_timeout "$client" "$client_log" "$BIN_DIR/$client"
  wait_for_pid_with_timeout "$server" "$server_log" "$server_pid"
  trap - EXIT
done

echo "All tests finished. Logs: $LOG_DIR"
