#!/bin/bash
# deploy.sh - Deploy cluster node to remote server
# Usage: ./deploy.sh <node_number>
# Example: ./deploy.sh 1

NODE_ID=$1

# Server configuration arrays
PHYSICAL_IPS=("207.180.251.206" "167.86.66.60" "167.86.83.198" "167.86.81.251")
TAILSCALE_IPS=("100.117.80.126" "100.70.98.49" "100.118.80.33" "100.116.151.88")
SSH_USERS=("Anthony" "Austin" "Edward" "Anthony")
REPO_PATH="database"  # Relative to remote user's home directory

# Validate node ID
if [ -z "$NODE_ID" ] || [ "$NODE_ID" -lt 1 ] || [ "$NODE_ID" -gt 4 ]; then
    echo "Usage: $0 <node_number (1-4)>"
    exit 1
fi

# Get configuration for this node
IDX=$((NODE_ID-1))
PHYSICAL_IP="${PHYSICAL_IPS[$IDX]}"
TAILSCALE_IP="${TAILSCALE_IPS[$IDX]}"
SSH_USER="${SSH_USERS[$IDX]}"

echo "========================================"
echo "Deploying Node $NODE_ID"
echo "Physical IP:  $PHYSICAL_IP"
echo "Tailscale IP: $TAILSCALE_IP"
echo "SSH User:     $SSH_USER"
echo "========================================"

# Helper function for SSH (non-backgrounding; use for commands where you need status)
ssh_exec() {
    ssh "$SSH_USER@$PHYSICAL_IP" "$1"
}

# Helper function for SSH that disables tty and local stdin (use for fire-and-forget)
ssh_exec_nontty() {
    ssh -n -T "$SSH_USER@$PHYSICAL_IP" "$1"
}

# 1. Stop existing processes
echo "[1/7] Stopping existing cluster processes..."
ssh_exec "killall -9 run leader follower 2>/dev/null || true; sleep 1" || true

# 2. Clean old data
echo "[2/7] Cleaning old data..."
ssh_exec "cd $REPO_PATH/Replication && rm -rf data_node* *.log node*.out node*.pid build.log 2>/dev/null" || true

# 3. Pull latest code
echo "[3/7] Pulling latest code from git..."
ssh_exec "cd $REPO_PATH && git reset --hard HEAD && git pull origin main"
if [ $? -ne 0 ]; then
    echo "ERROR: Git pull failed!"
    exit 1
fi

# 4. Build binaries (suppress long compiler output into build.log)
echo "[4/7] Building cluster binaries (compiler output written to build.log)..."
# Run make and capture all output to build.log on the remote host
ssh_exec "cd $REPO_PATH/Replication && make clean && make all > build.log 2>&1"
BUILD_EXIT=$?
if [ $BUILD_EXIT -ne 0 ]; then
    echo "ERROR: Build failed — showing last 200 lines of remote build.log:"
    ssh_exec "cd $REPO_PATH/Replication && tail -n 200 build.log"
    exit 1
fi

# Optionally detect warnings/errors and notify user (without printing full tail)
# If you want to see the warnings, run: ssh $SSH_USER@$PHYSICAL_IP 'tail -n 200 $REPO_PATH/Replication/build.log'
HAS_ISSUES=$(ssh "$SSH_USER@$PHYSICAL_IP" "grep -E -i 'warning:|error:' $REPO_PATH/Replication/build.log >/dev/null; echo \$?")
if [ "$HAS_ISSUES" -eq 0 ]; then
    echo "Build completed with warnings or errors present in build.log (warnings suppressed)."
    echo "To inspect them: ssh $SSH_USER@$PHYSICAL_IP 'tail -n 200 $REPO_PATH/Replication/build.log'"
else
    echo "Build successful."
fi

# 5. Verify build
echo "[5/7] Verifying build..."
ssh_exec "test -x $REPO_PATH/Replication/run && echo 'Binary present' || (echo 'Binary missing' && exit 2)"
if [ $? -ne 0 ]; then
    echo "ERROR: Binary not found or not executable!"
    exit 1
fi

# 6. Start node (detached with setsid; record PID)
echo "[6/7] Starting node $NODE_ID..."
# Use a non-interactive bash dance to detach reliably, write PID to nodeX.pid
ssh_exec_nontty "cd $REPO_PATH/Replication && bash -lc 'setsid nohup ./run $NODE_ID > node${NODE_ID}.out 2>&1 < /dev/null & echo \$! > node${NODE_ID}.pid'"

# 7. Quick verification (check PID / process)
echo "[7/7] Verifying node startup..."
sleep 1
# Corrected if/else without syntax errors
ssh_exec_nontty "cd $REPO_PATH/Replication && if [ -f node${NODE_ID}.pid ]; then printf 'Started: PID=' && cat node${NODE_ID}.pid; else pgrep -af run || echo 'No PID file and no process found'; fi"

echo "  Check logs with: ssh $SSH_USER@$PHYSICAL_IP 'tail -f $REPO_PATH/Replication/node${NODE_ID}.out'"
echo "========================================"
echo "Deployment complete for Node $NODE_ID"
echo "========================================"