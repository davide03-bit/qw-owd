#!/bin/bash
set -e

# Start the Docker container on the remote host
echo -n "Starting Docker container on remote host... "
container_id=$(ssh -tt -o StrictHostKeyChecking=no balillus@10.73.0.20 \
    "sudo docker run -d -it --net host --privileged -v /home/balillus/qw:/qw/ quic-westwood" | tr -d '\r\n')
echo "done. Container ID: $container_id"

# Start local HTTP client (50MB file) in background
echo -n "Starting local hq client for /largefile50M.bin... "
first_time=$(date +%s)
/qw/proxygen/proxygen/_build/proxygen/httpserver/hq \
    --mode=client \
    --host=10.73.0.20 \
    --outdir=/qw/client \
    --path="/largefile50M.bin" \
    -qlogger_path=/qw/client/logs/ \
    -stream_flow_control=2147483647 > /dev/null 2>&1 &
HQ50_PID=$!
echo "done."

# Wait a few seconds before starting reverse requests
sleep 4

# --- First Reverse Traffic Request ---
second_time=$(date +%s)
elapsed=$((second_time - first_time))
echo "First reverse traffic started $elapsed s"
ssh -o StrictHostKeyChecking=no balillus@10.73.0.20 \
  "sudo docker exec ${container_id} /qw/proxygen/proxygen/_build/proxygen/httpserver/hq \
    --mode=client \
    --host=10.73.0.160 \
    --outdir=/qw/client \
    --path='/largefile10M.bin' \
    -qlogger_path=/qw/client/logs/ \
    -stream_flow_control=2147483647 > /tmp/hqclient_first.log"
echo "First reverse client request finished."

# Wait a few seconds before the second reverse request
sleep 4

# --- Second Reverse Traffic Request ---
third_time=$(date +%s)
elapsed=$((third_time - first_time))
echo "Second reverse traffic started $elapsed s"
ssh -o StrictHostKeyChecking=no balillus@10.73.0.20 \
  "sudo docker exec ${container_id} /qw/proxygen/proxygen/_build/proxygen/httpserver/hq \
    --mode=client \
    --host=10.73.0.160 \
    --outdir=/qw/client \
    --path='/largefile10M.bin' \
    -qlogger_path=/qw/client/logs/ \
    -stream_flow_control=2147483647 > /tmp/hqclient_second.log"
echo "Second reverse client request finished."

# Clean up: kill the Docker container and wait for local client to finish.
ssh -o StrictHostKeyChecking=no balillus@10.73.0.20 "sudo docker kill ${container_id}"
echo -n "Waiting for local hq client to complete... "
wait ${HQ50_PID}
echo "Local hq client transfer completed."

exit 0
