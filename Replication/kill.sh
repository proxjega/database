#!/usr/bin/env bash

set -u

log() {
  printf '%s %s\n' "$(date +'%Y-%m-%dT%H:%M:%S%z')" "$*"
}

# Ports to close connections on (adjust if needed)
PORTS=(7001 7002 7101 7102 7103 7104 8001 8002 8003 8004)

# Process names to kill
PROCS=(run leader follower)

log "[1/7] Stopping existing cluster processes..."

# Try to force-close connections on listed ports using ss -K if available.
if command -v ss >/dev/null 2>&1; then
  log "Using ss to force-close connections on target ports."
  for p in "${PORTS[@]}"; do
    # ss -K might require privileges; ignore errors and continue
    ss -K dst :${p} 2>/dev/null || true
  done
else
  log "ss not found; skipping force-close by ss."
fi

# Optional: try fuser to kill processes using the ports (best-effort, non-fatal)
if command -v fuser >/dev/null 2>&1; then
  log "Attempting best-effort fuser kill on ports (TCP) — non-fatal."
  for p in "${PORTS[@]}"; do
    # This kills processes using the port (TCP) — run as root if needed
    # Ignore errors
    fuser -k "${p}/tcp" 2>/dev/null || true
  done
fi

# Graceful kill first, then force kill after a short sleep
log "Attempting graceful termination of processes: ${PROCS[*]}"
# Build killall command safely
{
  killall "${PROCS[@]/#/}" 2>/dev/null || true
} || true

sleep 2

log "Attempting forceful termination (SIGKILL) of processes: ${PROCS[*]}"
{
  killall -9 "${PROCS[@]/#/}" 2>/dev/null || true
} || true

log "Done."
exit 0