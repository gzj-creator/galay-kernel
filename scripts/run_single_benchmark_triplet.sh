#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TRIPLET_RUNNER="$SCRIPT_DIR/run_benchmark_triplet.sh"

BASELINE_REF=""
REFACTORED_PATH="$PROJECT_ROOT"
OUTPUT_ROOT="$PROJECT_ROOT/build/single-benchmark-triplet"
REPEAT=3
DRY_RUN=0
BENCHMARK=""
TIMEOUT_SECONDS=""

usage() {
  cat <<EOF
Usage: $0 --baseline-ref REF --benchmark NAME [options]

Options:
  --baseline-ref REF     Commit/ref for the pre-refactor baseline.
  --benchmark NAME       Run exactly one benchmark through kqueue, epoll, io_uring.
  --refactored-path DIR  Path to refactored worktree (default: current project root).
  --output-root DIR      Output root for per-backend outputs.
  --repeat N             Number of benchmark runs per label (default: 3).
  --timeout-seconds N    Per-benchmark timeout in seconds.
  --dry-run              Print delegated triplet commands without executing them.
EOF
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

while [[ $# -gt 0 ]]; do
  case "$1" in
    --baseline-ref)
      BASELINE_REF="$2"
      shift 2
      ;;
    --benchmark)
      BENCHMARK="$2"
      shift 2
      ;;
    --refactored-path)
      REFACTORED_PATH="$2"
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

if [[ -z "$BASELINE_REF" || -z "$BENCHMARK" ]]; then
  echo "--baseline-ref and --benchmark are required" >&2
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

for backend in kqueue epoll io_uring; do
  cmd=(
    "$TRIPLET_RUNNER"
    --baseline-ref "$BASELINE_REF"
    --refactored-path "$REFACTORED_PATH"
    --backend "$backend"
    --benchmark "$BENCHMARK"
    --repeat "$REPEAT"
    --output-root "$OUTPUT_ROOT/$backend"
  )
  if [[ -n "$TIMEOUT_SECONDS" ]]; then
    cmd+=(--timeout-seconds "$TIMEOUT_SECONDS")
  fi
  if [[ "$DRY_RUN" -eq 1 ]]; then
    cmd+=(--dry-run)
  fi
  run_cmd "${cmd[@]}"
done
