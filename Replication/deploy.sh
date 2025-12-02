#!/bin/bash
# deploy.sh - Deploy cluster node to remote server
# Usage: ./deploy.sh <node_number>
# Example: ./deploy.sh 1
# Note: Requires SSH key-based authentication

NODE_ID=$1

# Server configuration arrays
PHYSICAL_IPS=("207.180.251.206" "167.86.66.60" "167.86.83.198" "167.86.81.251")
TAILSCALE_IPS=("100.117.80.126" "100.70.98.49" "100.118.80.33" "100.116.151.88")
SSH_HOSTS=("cluster-node1" "cluster-node2" "cluster-node3" "cluster-node1")
REPO_PATH="$HOME/database"

# Validate node ID
if [ -z "$NODE_ID" ] || [ "$NODE_ID" -lt 1 ] || [ "$NODE_ID" -gt 4 ]; then
    echo "Usage: $0 <node_number (1-4)>"
    exit 1
fi

# Get configuration for this node
IDX=$((NODE_ID-1))
PHYSICAL_IP="${PHYSICAL_IPS[$IDX]}"
TAILSCALE_IP="${TAILSCALE_IPS[$IDX]}"
SSH_HOST="${SSH_HOSTS[$IDX]}"
# Removed

echo "========================================"
echo "Deploying Node $NODE_ID"
echo "Physical IP:  $PHYSICAL_IP"
echo "Tailscale IP: $TAILSCALE_IP"
echo "SSH Host:     $SSH_HOST"
echo "========================================"

# Helper function for SSH with password
ssh_exec() {
    ssh "$SSH_HOST" "$1"
}

# 1. Stop existing processes
echo "[1/7] Stopping existing cluster processes..."
ssh_exec "killall -9 run leader follower 2>/dev/null; sleep 1" || true

# 2. Clean old data
echo "[2/7] Cleaning old data..."
ssh_exec "cd $REPO_PATH/Replication && rm -rf data_node* *.log node*.out 2>/dev/null" || true

# 3. Pull latest code
echo "[3/7] Pulling latest code from git..."
ssh_exec "cd $REPO_PATH && git pull origin main"
if [ $? -ne 0 ]; then
    echo "ERROR: Git pull failed!"
    exit 1
fi

# 4. Build binaries
echo "[4/7] Building cluster binaries..."
ssh_exec "cd $REPO_PATH/Replication && make clean && make all 2>&1"
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed!"
    exit 1
fi

# 5. Verify build
echo "[5/7] Verifying build..."
ssh_exec "test -x $REPO_PATH/Replication/run && echo 'Build successful'"
if [ $? -ne 0 ]; then
    echo "ERROR: Binary not found or not executable!"
    exit 1
fi

# 6. Start node (in background with nohup)
echo "[6/7] Starting node $NODE_ID..."
ssh_exec "cd $REPO_PATH/Replication && nohup ./run $NODE_ID > node${NODE_ID}.out 2>&1 &"

# 7. Wait and health check
echo "[7/7] Health check..."
sleep 3
PROCESS_CHECK=$(ssh_exec "pgrep -f './run $NODE_ID'")
if [ -n "$PROCESS_CHECK" ]; then
    echo "✓ Node $NODE_ID started successfully (PID: $PROCESS_CHECK)"
else
    echo "✗ Failed to start node $NODE_ID"
    echo "Fetching logs:"
    ssh_exec "tail -20 $REPO_PATH/Replication/node${NODE_ID}.out"
    exit 1
fi

echo "========================================"
echo "Deployment complete for Node $NODE_ID"
echo "========================================"
