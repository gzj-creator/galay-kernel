#!/usr/bin/env bash

resolve_benchmark_timeout_bin() {
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

run_benchmark_with_timeout() {
  local name="$1"
  local logfile="$2"
  local timeout_seconds="$3"
  local kill_after_seconds="$4"
  shift 4
  local -a cmd=("$@")
  local timeout_bin

  timeout_bin="$(resolve_benchmark_timeout_bin)"
  if [[ -n "$timeout_bin" ]]; then
    if "$timeout_bin" -k "${kill_after_seconds}s" "${timeout_seconds}s" "${cmd[@]}" \
      </dev/null >"$logfile" 2>&1; then
      return 0
    fi
    local status=$?
    if [[ "$status" -eq 124 || "$status" -eq 137 ]]; then
      echo "[benchmark-timeout] ${name} exceeded ${timeout_seconds}s" >>"$logfile"
      echo "WARN: ${name} timed out after ${timeout_seconds}s"
      return 0
    fi
    return "$status"
  fi

  local timeout_flag
  timeout_flag="$(mktemp)"
  "${cmd[@]}" </dev/null >"$logfile" 2>&1 &
  local cmd_pid=$!
  (
    sleep "$timeout_seconds"
    if kill -0 "$cmd_pid" 2>/dev/null; then
      printf 'timeout\n' >"$timeout_flag"
      kill -TERM "$cmd_pid" 2>/dev/null || true
      sleep "$kill_after_seconds"
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
    echo "[benchmark-timeout] ${name} exceeded ${timeout_seconds}s" >>"$logfile"
    echo "WARN: ${name} timed out after ${timeout_seconds}s"
    return 0
  fi

  rm -f "$timeout_flag"
  return "$status"
}
