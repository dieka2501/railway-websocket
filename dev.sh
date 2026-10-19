#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="$ROOT_DIR/dev.sh"
PID_FILE="$ROOT_DIR/.dev-runner.pid"
CHILD_PID=""

is_managed_runner() {
  local pid="$1"
  local recorded_root="$2"
  local command_line
  local process_cwd

  [[ "$recorded_root" == "$ROOT_DIR" ]] || return 1
  command_line="$(ps -p "$pid" -o command= 2>/dev/null || true)"
  [[ "$command_line" == *"dev.sh"* ]] || return 1

  if command -v lsof >/dev/null 2>&1; then
    process_cwd="$(lsof -a -p "$pid" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p' | head -n 1)"
    [[ "$process_cwd" == "$ROOT_DIR" ]] || return 1
  fi
}

stop_previous_runner() {
  local old_pid
  local old_root
  local attempt

  [[ -f "$PID_FILE" ]] || return 0
  old_pid="$(head -n 1 "$PID_FILE")"
  old_root="$(sed -n '2p' "$PID_FILE")"
  if [[ "${old_pid:-}" =~ ^[0-9]+$ ]] && kill -0 "$old_pid" 2>/dev/null && is_managed_runner "$old_pid" "${old_root:-}"; then
    echo "Stopping previous dev runner (PID $old_pid)..."
    kill -TERM "$old_pid"
    for attempt in {1..50}; do
      kill -0 "$old_pid" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "$old_pid" 2>/dev/null; then
      echo "Previous dev runner did not stop in time; refusing to start another instance." >&2
      return 1
    fi
  fi
  rm -f "$PID_FILE"
}

cleanup() {
  local exit_code=$?
  trap - EXIT INT TERM
  if [[ -n "$CHILD_PID" ]] && kill -0 "$CHILD_PID" 2>/dev/null; then
    kill -TERM "$CHILD_PID" 2>/dev/null || true
    wait "$CHILD_PID" 2>/dev/null || true
  fi
  if [[ -f "$PID_FILE" ]] && [[ "$(head -n 1 "$PID_FILE")" == "$$" ]]; then
    rm -f "$PID_FILE"
  fi
  exit "$exit_code"
}

on_signal() {
  exit "$1"
}

stop_previous_runner
printf '%s\n%s\n' "$$" "$ROOT_DIR" > "$PID_FILE"
trap cleanup EXIT
trap 'on_signal 130' INT
trap 'on_signal 143' TERM

cd "$ROOT_DIR"
if [[ ! -d "$ROOT_DIR/node_modules" ]]; then
  npm install
fi

echo "Starting development watcher from $ROOT_DIR"
npm run dev:watch &
CHILD_PID=$!
wait "$CHILD_PID"
