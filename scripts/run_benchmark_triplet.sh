#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
WORKTREE_ROOT="$(dirname "$PROJECT_ROOT")"
COMMON_ROOT="$(dirname "$WORKTREE_ROOT")"
source "$SCRIPT_DIR/benchmark_timeout.sh"

BASELINE_REF=""
REFACTORED_PATH="$PROJECT_ROOT"
OUTPUT_ROOT="$PROJECT_ROOT/build/benchmark-triplet"
REPEAT=3
DRY_RUN=0
BACKEND="auto"
BENCHMARK_FILTER=""
TIMEOUT_SECONDS=""

usage() {
  cat <<EOF
Usage: $0 --baseline-ref REF [options]

Options:
  --baseline-ref REF     Commit/ref for the pre-refactor baseline.
  --refactored-path DIR  Path to refactored worktree (default: current project root).
  --backend NAME         Backend for benchmark builds/compat macros: auto|epoll|io_uring|kqueue.
  --benchmark NAME       Run exactly one benchmark (pair-aware for client/server pairs).
  --output-root DIR      Output root for builds and logs.
  --repeat N             Number of benchmark runs per label (default: 3).
  --timeout-seconds N    Per-benchmark timeout in seconds for matrix and compat runs.
  --dry-run              Print commands without executing them.
EOF
}

resolve_path() {
  local base="$1"
  local value="$2"
  if [[ "$value" = /* ]]; then
    printf '%s\n' "$value"
  else
    printf '%s\n' "$base/$value"
  fi
}

print_command() {
  printf '$'
  for arg in "$@"; do
    printf ' %q' "$arg"
  done
  printf '\n'
}

run_cmd() {
  print_command "$@"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    return 0
  fi
  "$@"
}

mkdir_cmd() {
  print_command mkdir -p "$1"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    return 0
  fi
  mkdir -p "$1"
}

write_compat_unsupported() {
  local label="$1"
  local benchmark_name="$2"
  local reason="$3"
  local run_index
  for ((run_index = 1; run_index <= REPEAT; ++run_index)); do
    local run_dir="$OUTPUT_ROOT/$label/run-$run_index"
    mkdir -p "$run_dir"
    printf '[benchmark-unsupported] %s\n' "$reason" >"$run_dir/${benchmark_name}.log"
  done
}

main_matrix_extra_skips_for_label() {
  local label="$1"
  if [[ "$BACKEND" == "io_uring" && "$label" == "baseline" ]]; then
    printf '%s\n' "B4-UdpServer"
    printf '%s\n' "B5-UdpClient"
  fi
}

compat_unsupported_reason() {
  local label="$1"
  local benchmark_name="$2"

  if [[ "$BACKEND" == "io_uring" && "$label" == "baseline" && "$benchmark_name" == "B6-Udp" ]]; then
    printf '%s\n' "historical io_uring UDP timeout lifetime path unsupported for $label"
    return 0
  fi

  return 1
}

main_matrix_benchmarks() {
  cat <<'EOF'
B2-TcpServer
B3-TcpClient
B4-UdpServer
B5-UdpClient
B7-FileIo
B9-UnsafeChannel
B11-TcpIovServer
B12-TcpIovClient
B13-Sendfile
EOF
}

selected_main_matrix_benchmarks() {
  case "$BENCHMARK_FILTER" in
    "")
      main_matrix_benchmarks
      ;;
    B2-TcpServer|B3-TcpClient)
      printf '%s\n' "B2-TcpServer" "B3-TcpClient"
      ;;
    B4-UdpServer|B5-UdpClient)
      printf '%s\n' "B4-UdpServer" "B5-UdpClient"
      ;;
    B7-FileIo)
      printf '%s\n' "B7-FileIo"
      ;;
    B9-UnsafeChannel)
      printf '%s\n' "B9-UnsafeChannel"
      ;;
    B11-TcpIovServer|B12-TcpIovClient)
      printf '%s\n' "B11-TcpIovServer" "B12-TcpIovClient"
      ;;
    B13-Sendfile)
      printf '%s\n' "B13-Sendfile"
      ;;
  esac
}

should_run_main_matrix_suite() {
  case "$BENCHMARK_FILTER" in
    ""|B2-TcpServer|B3-TcpClient|B4-UdpServer|B5-UdpClient|B7-FileIo|B9-UnsafeChannel|B11-TcpIovServer|B12-TcpIovClient|B13-Sendfile)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

should_run_compat_suite() {
  case "$BENCHMARK_FILTER" in
    ""|B1-ComputeScheduler|B6-Udp|B8-MpscChannel|B10-Ringbuffer|B14-SchedulerInjectedWakeup)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

should_run_compat_benchmark() {
  local benchmark_name="$1"
  if [[ -z "$BENCHMARK_FILTER" ]]; then
    return 0
  fi
  [[ "$BENCHMARK_FILTER" == "$benchmark_name" ]]
}

selected_main_matrix_contains() {
  local needle="$1"
  local benchmark_name
  while IFS= read -r benchmark_name; do
    if [[ "$benchmark_name" == "$needle" ]]; then
      return 0
    fi
  done < <(selected_main_matrix_benchmarks)
  return 1
}

validate_benchmark_filter() {
  case "$BENCHMARK_FILTER" in
    ""|B1-ComputeScheduler|B2-TcpServer|B3-TcpClient|B4-UdpServer|B5-UdpClient|B6-Udp|B7-FileIo|B8-MpscChannel|B9-UnsafeChannel|B10-Ringbuffer|B11-TcpIovServer|B12-TcpIovClient|B13-Sendfile|B14-SchedulerInjectedWakeup)
      return 0
      ;;
    *)
      echo "unsupported benchmark filter: $BENCHMARK_FILTER" >&2
      exit 1
      ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --baseline-ref)
      BASELINE_REF="$2"
      shift 2
      ;;
    --refactored-path)
      REFACTORED_PATH="$2"
      shift 2
      ;;
    --backend)
      BACKEND="$2"
      shift 2
      ;;
    --benchmark)
      BENCHMARK_FILTER="$2"
      shift 2
      ;;
    --output-root)
      OUTPUT_ROOT="$2"
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
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "$BASELINE_REF" ]]; then
  echo "--baseline-ref is required" >&2
  usage >&2
  exit 1
fi

if ! [[ "$REPEAT" =~ ^[1-9][0-9]*$ ]]; then
  echo "repeat must be a positive integer: $REPEAT" >&2
  exit 1
fi

if [[ -n "$TIMEOUT_SECONDS" ]] && ! [[ "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
  echo "timeout-seconds must be a positive integer: $TIMEOUT_SECONDS" >&2
  exit 1
fi

case "$BACKEND" in
  auto|epoll|io_uring|kqueue)
    ;;
  *)
    echo "backend must be one of: auto, epoll, io_uring, kqueue" >&2
    exit 1
    ;;
esac

validate_benchmark_filter

REFACTORED_PATH="$(resolve_path "$COMMON_ROOT" "$REFACTORED_PATH")"
OUTPUT_ROOT="$(resolve_path "$PROJECT_ROOT" "$OUTPUT_ROOT")"

BASELINE_PATH="$WORKTREE_ROOT/baseline-bench"

BASELINE_BUILD="$OUTPUT_ROOT/build/baseline"
REFACTORED_BUILD="$OUTPUT_ROOT/build/refactored"
TRIPLET_LABEL_COOLDOWN_SECONDS="${TRIPLET_LABEL_COOLDOWN_SECONDS:-10}"
BENCH_TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-${BENCH_TIMEOUT_SECONDS:-60}}"
BENCH_TIMEOUT_KILL_AFTER_SECONDS="${BENCH_TIMEOUT_KILL_AFTER_SECONDS:-2}"

if ! [[ "$BENCH_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
  echo "timeout-seconds must be a positive integer: $BENCH_TIMEOUT_SECONDS" >&2
  exit 1
fi

MATRIX_SCRIPT="$PROJECT_ROOT/scripts/run_benchmark_matrix.sh"
COMPAT_ROOT="$OUTPUT_ROOT"
COMPAT_BENCHMARKS=(
  "b1:B1-ComputeScheduler:benchmark/B1-compute_scheduler.cc"
  "b6:B6-Udp:benchmark/B6-Udp.cc"
  "b8:B8-MpscChannel:benchmark/B8-mpsc_channel.cc"
  "b10:B10-Ringbuffer:benchmark/B10-Ringbuffer.cc"
  "b14:B14-SchedulerInjectedWakeup:benchmark/B14-scheduler_injected_wakeup.cc"
)

ensure_worktree() {
  local path="$1"
  local ref="$2"
  local expected_head
  expected_head="$(git -C "$PROJECT_ROOT" rev-parse "${ref}^{commit}")"

  if [[ -d "$path" ]]; then
    local current_head
    current_head="$(git -C "$path" rev-parse HEAD)"
    if [[ "$current_head" != "$expected_head" ]]; then
      echo "existing worktree at $path is on $current_head, expected $expected_head" >&2
      exit 1
    fi
    return 0
  fi

  run_cmd git -C "$PROJECT_ROOT" worktree add "$path" "$ref"
}

configure_and_build() {
  local source_root="$1"
  local build_root="$2"
  local -a cmake_args=(
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTS=OFF
    -DBUILD_EXAMPLES=OFF
    -DBUILD_BENCHMARKS=ON
  )

  case "$BACKEND" in
    epoll)
      cmake_args+=(-DDISABLE_IOURING=ON)
      ;;
    io_uring)
      cmake_args+=(-DDISABLE_IOURING=OFF)
      ;;
  esac

  mkdir_cmd "$build_root"
  run_cmd cmake -S "$source_root" -B "$build_root" "${cmake_args[@]}"
  run_cmd cmake --build "$build_root" -j4
}

run_matrix_for_label() {
  local build_root="$1"
  local label="$2"
  local allow_missing="$3"
  local run_index="$4"
  local extra_skip
  local benchmark_name
  local -a cmd=(
    "$MATRIX_SCRIPT"
    --build-dir "$build_root"
    --log-root "$OUTPUT_ROOT"
    --repeat 1
    --timeout-seconds "$BENCH_TIMEOUT_SECONDS"
    --run-index "$run_index"
    --label "$label"
    --skip-benchmark B1-ComputeScheduler
    --skip-benchmark B6-Udp
    --skip-benchmark B8-MpscChannel
    --skip-benchmark B10-Ringbuffer
    --skip-benchmark B14-SchedulerInjectedWakeup
  )

  if [[ -n "$BENCHMARK_FILTER" ]]; then
    while IFS= read -r benchmark_name; do
      if [[ -n "$benchmark_name" ]] && ! selected_main_matrix_contains "$benchmark_name"; then
        cmd+=(--skip-benchmark "$benchmark_name")
      fi
    done < <(main_matrix_benchmarks)
  fi

  while IFS= read -r extra_skip; do
    if [[ -n "$extra_skip" ]]; then
      cmd+=(--skip-benchmark "$extra_skip")
    fi
  done < <(main_matrix_extra_skips_for_label "$label")

  if [[ "$allow_missing" -eq 1 ]]; then
    cmd+=(--allow-missing)
    run_cmd "${cmd[@]}"
    return 0
  fi

  run_cmd "${cmd[@]}"
}

run_main_matrix_suite() {
  if ! should_run_main_matrix_suite; then
    return 0
  fi

  local labels=(baseline refactored)
  local build_roots=("$BASELINE_BUILD" "$REFACTORED_BUILD")
  local allow_missing_flags=(0 0)
  local label_count="${#labels[@]}"
  local run_index
  local rotation_start
  local offset
  local index

  for ((run_index = 1; run_index <= REPEAT; ++run_index)); do
    rotation_start=$(((run_index - 1) % label_count))
    for ((offset = 0; offset < label_count; ++offset)); do
      index=$(((rotation_start + offset) % label_count))
      run_matrix_for_label \
        "${build_roots[index]}" \
        "${labels[index]}" \
        "${allow_missing_flags[index]}" \
        "$run_index"

      if (( run_index < REPEAT || offset + 1 < label_count )); then
        cooldown_between_labels
      fi
    done
  done
}

cooldown_between_labels() {
  if [[ "$TRIPLET_LABEL_COOLDOWN_SECONDS" == "0" ]]; then
    return 0
  fi
  print_command sleep "$TRIPLET_LABEL_COOLDOWN_SECONDS"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    return 0
  fi
  sleep "$TRIPLET_LABEL_COOLDOWN_SECONDS"
}

backend_define() {
  if [[ "$BACKEND" != "auto" ]]; then
    case "$BACKEND" in
      epoll)
        printf '%s\n' "USE_EPOLL"
        return 0
        ;;
      io_uring)
        printf '%s\n' "USE_IOURING"
        return 0
        ;;
      kqueue)
        printf '%s\n' "USE_KQUEUE"
        return 0
        ;;
    esac
  fi

  if [[ -n "${GALAY_TRIPLET_COMPAT_BACKEND_DEFINE:-}" ]]; then
    case "$GALAY_TRIPLET_COMPAT_BACKEND_DEFINE" in
      USE_KQUEUE|USE_EPOLL|USE_IOURING)
        printf '%s\n' "$GALAY_TRIPLET_COMPAT_BACKEND_DEFINE"
        return 0
        ;;
      *)
        echo "unsupported GALAY_TRIPLET_COMPAT_BACKEND_DEFINE: $GALAY_TRIPLET_COMPAT_BACKEND_DEFINE" >&2
        exit 1
        ;;
    esac
  fi

  case "$(uname -s)" in
    Darwin)
      printf '%s\n' "USE_KQUEUE"
      ;;
    Linux)
      printf '%s\n' "USE_EPOLL"
      ;;
    *)
      printf '%s\n' ""
      ;;
  esac
}

runtime_library_env_name() {
  case "$(uname -s)" in
    Darwin)
      printf '%s\n' "DYLD_LIBRARY_PATH"
      ;;
    *)
      printf '%s\n' "LD_LIBRARY_PATH"
      ;;
  esac
}

compat_source_root_for_label() {
  case "$1" in
    refactored) printf '%s\n' "$REFACTORED_PATH" ;;
    baseline) printf '%s\n' "$BASELINE_PATH" ;;
    *) return 1 ;;
  esac
}

compat_build_root_for_label() {
  case "$1" in
    refactored) printf '%s\n' "$REFACTORED_BUILD" ;;
    baseline) printf '%s\n' "$BASELINE_BUILD" ;;
    *) return 1 ;;
  esac
}

compat_benchmark_source_root_for_label() {
  case "$1" in
    refactored) printf '%s\n' "$REFACTORED_PATH" ;;
    baseline) printf '%s\n' "$PROJECT_ROOT" ;;
    *) return 1 ;;
  esac
}

compat_binary_path() {
  local compat_slug="$1"
  local label="$2"
  local benchmark_name="$3"
  printf '%s\n' "$COMPAT_ROOT/compat-$compat_slug/$label/${benchmark_name}-Compat"
}

run_compat_benchmark_build() {
  local label="$1"
  local compat_slug="$2"
  local benchmark_name="$3"
  local benchmark_source="$4"
  local unsupported_reason
  local historical_source_root
  local historical_build_root
  local benchmark_source_root
  local compat_dir
  local compat_bin
  local compiler="${CXX:-c++}"
  local backend_macro
  local compile_args=()

  historical_source_root="$(compat_source_root_for_label "$label")"
  historical_build_root="$(compat_build_root_for_label "$label")"
  benchmark_source_root="$(compat_benchmark_source_root_for_label "$label")"
  compat_dir="$COMPAT_ROOT/compat-$compat_slug/$label"
  compat_bin="$(compat_binary_path "$compat_slug" "$label" "$benchmark_name")"
  backend_macro="$(backend_define)"

  if unsupported_reason="$(compat_unsupported_reason "$label" "$benchmark_name")"; then
    if [[ "$DRY_RUN" -eq 0 ]]; then
      write_compat_unsupported "$label" "$benchmark_name" "$unsupported_reason"
    fi
    return 0
  fi

  mkdir_cmd "$compat_dir"

  compile_args=(
    "$compiler"
    -std=c++23
    -O3
    -DNDEBUG
    -I"$historical_source_root"
    -I"$PROJECT_ROOT"
  )
  if [[ -n "$backend_macro" ]]; then
    compile_args+=("-D$backend_macro")
  fi
  if [[ "$(uname -s)" == "Linux" ]]; then
    compile_args+=(-pthread)
  fi
  compile_args+=(
    "$benchmark_source_root/$benchmark_source"
    -L"$historical_build_root/lib"
    "-Wl,-rpath,$historical_build_root/lib"
    -lgalay-kernel
    -o "$compat_bin"
  )

  if ! run_cmd "${compile_args[@]}"; then
    echo "WARN: failed to build compat $benchmark_name for $label, marking unsupported" >&2
    if [[ "$DRY_RUN" -eq 0 ]]; then
      write_compat_unsupported "$label" "$benchmark_name" "compat $benchmark_name build failed for $label"
    fi
    return 0
  fi
}

run_compat_benchmark_run() {
  local label="$1"
  local compat_slug="$2"
  local benchmark_name="$3"
  local run_index="$4"
  local unsupported_reason
  local historical_build_root
  local runtime_env_key
  local runtime_env_value
  local compat_bin
  local run_dir
  local log_path

  historical_build_root="$(compat_build_root_for_label "$label")"
  runtime_env_key="$(runtime_library_env_name)"
  runtime_env_value="$historical_build_root/lib"
  if [[ -n "${!runtime_env_key:-}" ]]; then
    runtime_env_value="$runtime_env_value:${!runtime_env_key}"
  fi

  compat_bin="$(compat_binary_path "$compat_slug" "$label" "$benchmark_name")"
  run_dir="$OUTPUT_ROOT/$label/run-$run_index"
  log_path="$run_dir/${benchmark_name}.log"
  mkdir_cmd "$run_dir"

  if unsupported_reason="$(compat_unsupported_reason "$label" "$benchmark_name")"; then
    if [[ "$DRY_RUN" -eq 0 && ! -f "$log_path" ]]; then
      printf '[benchmark-unsupported] %s\n' "$unsupported_reason" >"$log_path"
    fi
    return 0
  fi

  if [[ "$DRY_RUN" -eq 1 ]]; then
    print_command env "$runtime_env_key=$runtime_env_value" "$compat_bin"
    return 0
  fi

  if [[ ! -x "$compat_bin" ]]; then
    printf '[benchmark-unsupported] compat %s binary missing for %s run-%d\n' "$benchmark_name" "$label" "$run_index" >"$log_path"
    return 0
  fi

  print_command env "$runtime_env_key=$runtime_env_value" "$compat_bin"
  if ! run_benchmark_with_timeout \
    "$benchmark_name" \
    "$log_path" \
    "$BENCH_TIMEOUT_SECONDS" \
    "$BENCH_TIMEOUT_KILL_AFTER_SECONDS" \
    env "$runtime_env_key=$runtime_env_value" "$compat_bin"; then
    echo "WARN: compat $benchmark_name run failed for $label run-$run_index, marking unsupported" >&2
    printf '[benchmark-unsupported] compat %s run failed for %s run-%d\n' "$benchmark_name" "$label" "$run_index" >"$log_path"
  fi
}

run_compat_suite() {
  if ! should_run_compat_suite; then
    return 0
  fi

  local labels=(baseline refactored)
  local benchmark
  local compat_slug
  local benchmark_name
  local benchmark_source
  local run_index
  local label_index
  local rotation_start
  local label

  for benchmark in "${COMPAT_BENCHMARKS[@]}"; do
    for label in "${labels[@]}"; do
      IFS=':' read -r compat_slug benchmark_name benchmark_source <<<"$benchmark"
      if ! should_run_compat_benchmark "$benchmark_name"; then
        continue
      fi
      run_compat_benchmark_build "$label" "$compat_slug" "$benchmark_name" "$benchmark_source"
    done

    if ! should_run_compat_benchmark "$benchmark_name"; then
      continue
    fi

    for ((run_index = 1; run_index <= REPEAT; ++run_index)); do
      rotation_start=$(((run_index - 1) % ${#labels[@]}))
      for ((label_index = 0; label_index < ${#labels[@]}; ++label_index)); do
        label="${labels[$(((rotation_start + label_index) % ${#labels[@]}))]}"
        run_compat_benchmark_run "$label" "$compat_slug" "$benchmark_name" "$run_index"
      done
    done
  done
}

echo "Triplet benchmark orchestration"
echo "baseline source: $BASELINE_PATH"
echo "refactored source: $REFACTORED_PATH"
echo "output root: $OUTPUT_ROOT"

ensure_worktree "$BASELINE_PATH" "$BASELINE_REF"

configure_and_build "$BASELINE_PATH" "$BASELINE_BUILD"
cooldown_between_labels

configure_and_build "$REFACTORED_PATH" "$REFACTORED_BUILD"
cooldown_between_labels
run_main_matrix_suite
run_compat_suite

echo "Triplet benchmark run complete. Logs: $OUTPUT_ROOT"
