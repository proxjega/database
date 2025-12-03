#!/usr/bin/env bash
set -e

echo "Starting SSH tunnels to remote cluster nodes..."

# Kill any existing tunnels
pkill -f "autossh.*710[1-4]" 2>/dev/null || true
pkill -f "ssh.*710[1-4]" 2>/dev/null || true
pkill -f "autossh.*800[1-4]" 2>/dev/null || true
pkill -f "ssh.*800[1-4]" 2>/dev/null || true

sleep 1

echo "Creating tunnels for client API (7001) and control plane (8001-8004)..."

# Node 1: Anthony (207.180.251.206)
# Client API: Local 7101 -> Remote 7001
# Control plane: Local 8001 -> Remote 8001
autossh -M 0 -f -N \
  -o "ServerAliveInterval 30" -o "ServerAliveCountMax 3" \
  \
  -L 127.0.0.1:7101:127.0.0.1:7001 \
  -L 127.0.0.1:8001:127.0.0.1:8001 \
  cluster-node1 &

# Node 2: Austin (167.86.66.60)
# Client API: Local 7102 -> Remote 7001
# Control plane: Local 8002 -> Remote 8002
autossh -M 0 -f -N \
  -o "ServerAliveInterval 30" -o "ServerAliveCountMax 3" \
  \
  -L 127.0.0.1:7102:127.0.0.1:7001 \
  -L 127.0.0.1:8002:127.0.0.1:8002 \
  cluster-node2 &

# Node 3: Edward (167.86.83.198)
# Client API: Local 7103 -> Remote 7001
# Control plane: Local 8003 -> Remote 8003
autossh -M 0 -f -N \
  -o "ServerAliveInterval 30" -o "ServerAliveCountMax 3" \
  \
  -L 127.0.0.1:7103:127.0.0.1:7001 \
  -L 127.0.0.1:8003:127.0.0.1:8003 \
  cluster-node3 &

# Node 4: Anthony (167.86.81.251)
# Client API: Local 7104 -> Remote 7001
# Control plane: Local 8004 -> Remote 8004
autossh -M 0 -f -N \
  -o "ServerAliveInterval 30" -o "ServerAliveCountMax 3" \
  \
  -L 127.0.0.1:7104:127.0.0.1:7001 \
  -L 127.0.0.1:8004:127.0.0.1:8004 \
  cluster-node4 &

sleep 2
echo "✓ SSH tunnels established:"
echo "  - Client API: 7101-7104"
echo "  - Control plane: 8001-8004"