#!/usr/bin/env bash
set -e

echo "Starting SSH tunnels to remote cluster nodes..."

# Kill any existing tunnels
pkill -f "autossh.*710[1-4]" 2>/dev/null || true
pkill -f "ssh.*710[1-4]" 2>/dev/null || true

sleep 1

# Node 1: Anthony (207.180.251.206) - Local 7101 -> Remote 7001
autossh -M 0 -f -N \
  -o "ServerAliveInterval 30" -o "ServerAliveCountMax 3" \
  \
  -L 127.0.0.1:7101:127.0.0.1:7001 cluster-node1 &

# Node 2: Austin (167.86.66.60) - Local 7102 -> Remote 7001
autossh -M 0 -f -N \
  -o "ServerAliveInterval 30" -o "ServerAliveCountMax 3" \
  \
  -L 127.0.0.1:7102:127.0.0.1:7001 cluster-node2 &

# Node 3: Edward (167.86.83.198) - Local 7103 -> Remote 7001
autossh -M 0 -f -N \
  -o "ServerAliveInterval 30" -o "ServerAliveCountMax 3" \
  \
  -L 127.0.0.1:7103:127.0.0.1:7001 cluster-node3 &

# Node 4: Anthony (167.86.81.251) - Local 7104 -> Remote 7001
autossh -M 0 -f -N \
  -o "ServerAliveInterval 30" -o "ServerAliveCountMax 3" \
  \
  -L 127.0.0.1:7104:127.0.0.1:7001 cluster-node4 &

sleep 2
echo "✓ SSH tunnels established on ports 7101-7104"