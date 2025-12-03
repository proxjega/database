#!/bin/bash
# cluster_config.sh - Cluster node configuration (no passwords)
# This file contains the cluster topology and connection information
# Used by deploy.sh and other deployment scripts
#
# Authentication: Uses SSH key-based auth via ~/.ssh/config
# See SSH_SETUP.md for configuration instructions

# Server configuration arrays
# These map node IDs (1-4) to physical and Tailscale IPs

# Physical (public) IP addresses for SSH access
PHYSICAL_IPS=(
    "207.180.251.206"  # Node 1
    "167.86.66.60"     # Node 2
    "167.86.83.198"    # Node 3
    "167.86.81.251"    # Node 4
)

# Tailscale VPN IP addresses for inter-node communication
TAILSCALE_IPS=(
    "100.117.80.126"   # Node 1
    "100.70.98.49"     # Node 2
    "100.118.80.33"    # Node 3
    "100.116.151.88"   # Node 4
)

# SSH host aliases (defined in ~/.ssh/config)
# These reference the SSH config entries that contain user, key, and connection settings
SSH_HOSTS=(
    "cluster-node1"    # Node 1
    "cluster-node2"    # Node 2
    "cluster-node3"    # Node 3
    "cluster-node4"    # Node 4
)

# Repository path on remote servers (relative to user's home directory)
REPO_PATH="database"

# Port configuration
CLIENT_API_PORT=7001           # Client API listens on this port on each node
CONTROL_PLANE_BASE_PORT=8001   # Control plane ports: 8001, 8002, 8003, 8004

# Tunnel configuration (for local development/testing)
# Local ports that tunnel to remote client API
TUNNEL_CLIENT_PORTS=(7101 7102 7103 7104)
# Local ports that tunnel to remote control plane
TUNNEL_CONTROL_PORTS=(8001 8002 8003 8004)

# Export variables for use in other scripts
export PHYSICAL_IPS TAILSCALE_IPS SSH_HOSTS REPO_PATH
export CLIENT_API_PORT CONTROL_PLANE_BASE_PORT
export TUNNEL_CLIENT_PORTS TUNNEL_CONTROL_PORTS
