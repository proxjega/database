#!/usr/bin/env bash

echo "Stopping SSH tunnels..."
pkill -f "autossh.*710[1-4]" 2>/dev/null || true
pkill -f "ssh.*710[1-4]" 2>/dev/null || true
sleep 1
echo "✓ Tunnels stopped"
