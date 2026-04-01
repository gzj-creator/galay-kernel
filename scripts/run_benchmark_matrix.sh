#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
source "$SCRIPT_DIR/benchmark_timeout.sh"
DEFAULT_BUILD_DIR="$PROJECT_ROOT/build"
BUILD_DIR="$DEFAULT_BUILD_DIR"
LOG_ROOT=""
REPEAT=1
LABEL=""
RUN_INDEX=""
ALLOW_MISSING=0
TIMEOUT_SECONDS=""
SKIP_BENCHMARKS=()

POSITIONAL_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --log-root)
      LOG_ROOT="$2"
      shift 2
      ;;
    --repeat)
      REPEAT="$2"
      shift 2
      ;;
    --timeout-seconds)
      TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --label)
      LABEL="$2"
      shift 2
      ;;
    --run-index)
      RUN_INDEX="$2"
      shift 2
      ;;
    --allow-missing)
      ALLOW_MISSING=1
      shift
      ;;
    --skip-benchmark)
      SKIP_BENCHMARKS+=("$2")
      shift 2
      ;;
    --help)
      cat <<EOF
Usage: $0 [build_dir] [log_root]
       $0 [--build-dir DIR] [--log-root DIR] [--repeat N] [--timeout-seconds N] [--label NAME] [--run-index N] [--allow-missing] [--skip-benchmark NAME]
EOF
      exit 0
      ;;
    *)
      POSITIONAL_ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ ${#POSITIONAL_ARGS[@]} -ge 1 ]]; then
  BUILD_DIR="${POSITIONAL_ARGS[0]}"
fi

if [[ -n "$LOG_ROOT" ]]; then
  :
elif [[ ${#POSITIONAL_ARGS[@]} -ge 2 ]]; then
  LOG_ROOT="${POSITIONAL_ARGS[1]}"
else
  LOG_ROOT="$BUILD_DIR/benchmark_matrix_logs"
fi

BIN_DIR="$BUILD_DIR/bin"

TCP_BENCH_PORT="${GALAY_BENCH_TCP_PORT:-28082}"
TCP_IOV_BENCH_PORT="${GALAY_BENCH_TCP_IOV_PORT:-28083}"
if [[ -n "$TIMEOUT_SECONDS" ]]; then
  BENCH_TIMEOUT_SECONDS="$TIMEOUT_SECONDS"
else
  BENCH_TIMEOUT_SECONDS="${BENCH_TIMEOUT_SECONDS:-60}"
fi
BENCH_TIMEOUT_KILL_AFTER_SECONDS="${BENCH_TIMEOUT_KILL_AFTER_SECONDS:-2}"
BENCH_REPEAT_COOLDOWN_SECONDS="${BENCH_REPEAT_COOLDOWN_SECONDS:-2}"
BENCH_SERVER_STARTUP_SECONDS="${BENCH_SERVER_STARTUP_SECONDS:-1}"

if [[ ! -d "$BIN_DIR" ]]; then
  echo "bin dir not found: $BIN_DIR" >&2
  exit 1
fi

if ! [[ "$REPEAT" =~ ^[1-9][0-9]*$ ]]; then
  echo "repeat must be a positive integer: $REPEAT" >&2
  exit 1
fi

if ! [[ "$BENCH_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
  echo "timeout-seconds must be a positive integer: $BENCH_TIMEOUT_SECONDS" >&2
  exit 1
fi

if [[ -n "$RUN_INDEX" ]]; then
  if ! [[ "$RUN_INDEX" =~ ^[1-9][0-9]*$ ]]; then
    echo "run-index must be a positive integer: $RUN_INDEX" >&2
    exit 1
  fi
  if [[ "$REPEAT" != "1" ]]; then
    echo "run-index requires --repeat 1" >&2
    exit 1
  fi
fi

STANDALONE_BENCHMARKS=(
  B1-ComputeScheduler
  B14-SchedulerInjectedWakeup
  B8-MpscChannel
  B6-Udp
  B7-FileIo
  B9-UnsafeChannel
  B10-Ringbuffer
  B13-Sendfile
)

PAIRED_BENCHMARKS=(
  "B2-TcpServer:B3-TcpClient"
  "B4-UdpServer:B5-UdpClient"
  "B11-TcpIovServer:B12-TcpIovClient"
)

cleanup_pid() {
  local pid="$1"
  if ! kill -0 "$pid" 2>/dev/null; then
    wait "$pid" 2>/dev/null || true
    return 0
  fi

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
}

run_with_timeout() {
  local name="$1"
  local logfile="$2"
  shift 2
  run_benchmark_with_timeout \
    "$name" \
    "$logfile" \
    "$BENCH_TIMEOUT_SECONDS" \
    "$BENCH_TIMEOUT_KILL_AFTER_SECONDS" \
    "$@"
}

run_one() {
  local name="$1"
  local logfile="$2"
  local executable="$BIN_DIR/$name"
  if [[ ! -x "$executable" ]]; then
    if [[ "$ALLOW_MISSING" -eq 1 ]]; then
      echo "[benchmark-unsupported] executable not found: $executable" >"$logfile"
      echo "WARN: skipping missing benchmark $name"
      return 0
    fi
    echo "benchmark executable not found: $executable" >&2
    return 1
  fi
  echo "=== $name ==="
  run_with_timeout "$name" "$logfile" "$executable"
}

should_skip_benchmark() {
  local name="$1"
  local skipped
  if [[ ${#SKIP_BENCHMARKS[@]} -eq 0 ]]; then
    return 1
  fi
  for skipped in "${SKIP_BENCHMARKS[@]}"; do
    if [[ "$skipped" == "$name" ]]; then
      return 0
    fi
  done
  return 1
}

log_dir_for_run() {
  local run_index="$1"
  if [[ -n "$RUN_INDEX" ]]; then
    run_index="$RUN_INDEX"
  fi

  if [[ "$REPEAT" -eq 1 && -z "$LABEL" && -z "$RUN_INDEX" ]]; then
    printf '%s\n' "$LOG_ROOT"
    return
  fi

  local label_dir="${LABEL:-default}"
  printf '%s\n' "$LOG_ROOT/$label_dir/run-$run_index"
}

run_matrix_once() {
  local log_dir="$1"
  mkdir -p "$log_dir"

  for bench_name in "${STANDALONE_BENCHMARKS[@]}"; do
    if should_skip_benchmark "$bench_name"; then
      continue
    fi
    run_one "$bench_name" "$log_dir/${bench_name}.log"
  done

  for pair in "${PAIRED_BENCHMARKS[@]}"; do
    IFS=':' read -r server client <<<"$pair"
    server_log="$log_dir/${server}.log"
    client_log="$log_dir/${client}.log"
    server_cmd=("$BIN_DIR/$server")
    client_cmd=("$BIN_DIR/$client")

    if should_skip_benchmark "$server" || should_skip_benchmark "$client"; then
      printf '[benchmark-skipped] filtered by matrix skip list\n' >"$server_log"
      printf '[benchmark-skipped] filtered by matrix skip list\n' >"$client_log"
      echo "WARN: skipping paired benchmark $server + $client due to skip list"
      continue
    fi

    case "$pair" in
      "B2-TcpServer:B3-TcpClient")
        server_cmd+=("$TCP_BENCH_PORT")
        client_cmd+=(-p "$TCP_BENCH_PORT")
        ;;
      "B11-TcpIovServer:B12-TcpIovClient")
        server_cmd+=("$TCP_IOV_BENCH_PORT")
        client_cmd+=(-p "$TCP_IOV_BENCH_PORT")
        ;;
    esac

    if [[ ! -x "${server_cmd[0]}" || ! -x "${client_cmd[0]}" ]]; then
      if [[ "$ALLOW_MISSING" -eq 1 ]]; then
        echo "[benchmark-unsupported] executable not found: ${server_cmd[0]}" >"$server_log"
        echo "[benchmark-unsupported] executable not found: ${client_cmd[0]}" >"$client_log"
        echo "WARN: skipping missing paired benchmark $server + $client"
        continue
      fi
      echo "paired benchmark executable not found: ${server_cmd[0]} / ${client_cmd[0]}" >&2
      return 1
    fi

    echo "=== $server + $client ==="
    "${server_cmd[@]}" </dev/null >"$server_log" 2>&1 &
    server_pid=$!
    trap 'cleanup_pid "$server_pid"' EXIT
    sleep "$BENCH_SERVER_STARTUP_SECONDS"
    run_with_timeout "$client" "$client_log" "${client_cmd[@]}"
    cleanup_pid "$server_pid"
    trap - EXIT
  done
}

for ((run_index = 1; run_index <= REPEAT; ++run_index)); do
  run_log_dir="$(log_dir_for_run "$run_index")"
  run_matrix_once "$run_log_dir"
  if (( run_index < REPEAT )) && [[ "$BENCH_REPEAT_COOLDOWN_SECONDS" != "0" ]]; then
    sleep "$BENCH_REPEAT_COOLDOWN_SECONDS"
  fi
done

echo "All benchmarks finished. Logs: $LOG_ROOT"
