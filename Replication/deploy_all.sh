#!/bin/bash
# deploy_all.sh - Deploy to all 4 cluster nodes sequentially
# Usage: ./deploy_all.sh

echo "=========================================="
echo "Deploying to all 4 cluster nodes"
echo "=========================================="

# Deploy to each node sequentially
for i in 1 2 3 4; do
    echo ""
    echo ">>> Starting deployment for Node $i..."
    ./deploy.sh $i
    if [ $? -ne 0 ]; then
        echo "ERROR: Deployment failed for Node $i"
        echo "Aborting remaining deployments."
        exit 1
    fi
    echo ">>> Node $i deployment complete."

    # Wait between deployments to allow nodes to stabilize
    if [ $i -lt 4 ]; then
        echo "Waiting 5 seconds before next deployment..."
        sleep 5
    fi
done

echo ""
echo "=========================================="
echo "All nodes deployed successfully!"
echo "=========================================="

# Wait for cluster to stabilize
echo "Waiting 10 seconds for cluster to stabilize..."
sleep 10

# Verify cluster status
echo ""
echo "Verifying cluster status..."
if [ -x ./client ]; then
    ./client leader
else
    echo "Client binary not found. Skipping cluster status check."
fi

echo ""
echo "=========================================="
echo "Deployment complete!"
echo "=========================================="
