#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-$PROJECT_ROOT/build}"
BIN_DIR="$BUILD_DIR/bin"
LOG_DIR="${2:-$BUILD_DIR/test_matrix_logs}"

export GALAY_TEST_TCP_PORT="${GALAY_TEST_TCP_PORT:-28080}"
export GALAY_TEST_UDP_PORT="${GALAY_TEST_UDP_PORT:-28081}"

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
  while IFS= read -r test_path; do
    test_name="$(basename "$test_path")"
    for pair_name in "${PAIR_EXCLUDES[@]}"; do
      if [[ "$test_name" == "$pair_name" ]]; then
        test_name=""
        break
      fi
    done
    if [[ -n "$test_name" ]]; then
      printf '%s\n' "$test_name"
    fi
  done < <(find "$BIN_DIR" -maxdepth 1 -type f -name 'T*' | sort)
}

STANDALONE_TESTS=()
while IFS= read -r test_name; do
  STANDALONE_TESTS+=("$test_name")
done < <(discover_standalone_tests)

cleanup_pid() {
  local pid="$1"
  if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
}

run_one() {
  local name="$1"
  local logfile="$LOG_DIR/${name}.log"
  echo "=== $name ==="
  "$BIN_DIR/$name" </dev/null >"$logfile" 2>&1
}

for test_name in "${STANDALONE_TESTS[@]}"; do
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
  "$BIN_DIR/$client" </dev/null >"$client_log" 2>&1
  wait "$server_pid"
  trap - EXIT
done

echo "All tests finished. Logs: $LOG_DIR"
