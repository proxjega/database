#!/bin/bash
# Restart script for the HTTP server
# With proper signal handling, cleanup should be fast and reliable

cd "$(dirname "$0")"

echo "Stopping any running server instances..."

# Send SIGTERM first (graceful with our signal handler)
pkill -TERM server_app 2>/dev/null && sleep 0.2

# If still running, force kill with SIGKILL
pkill -9 server_app 2>/dev/null && sleep 0.2
killall -9 server_app 2>/dev/null || true

# Wait a moment for OS to release the socket
sleep 0.3

# Check if port is still in use
if lsof -Pi :8080 -sTCP:LISTEN -t >/dev/null 2>&1 ; then
    echo "WARNING: Port 8080 still in use. Forcing cleanup..."
    fuser -k -9 8080/tcp 2>/dev/null || true
    sleep 1

    # Final check
    if lsof -Pi :8080 -sTCP:LISTEN -t >/dev/null 2>&1 ; then
        echo "ERROR: Cannot free port 8080. Manual intervention required:"
        lsof -Pi :8080 -sTCP:LISTEN
        exit 1
    fi
fi

echo "Starting HTTP server..."
./server_app