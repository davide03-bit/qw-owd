#!/bin/bash
set -e

# This script assumes that the remote traffic shaping script now contains:
#   - tc_bw_delay_both <DEV> <KBPS> <DELAY_MS> <LOSS_PERCENT> [QUEUE_KB] [LIMIT_PKTS]
#       → sets up the TBF (for bandwidth) with a netem child (for delay, loss, queue limit)
#
#   - tc_update_bandwidth_both <DEV> <KBPS> <QUEUE_KB>
#       → updates the TBF parameters using "tc qdisc change"
#
#   - tc_del_bw_delay_both <DEV>
#       → removes the entire qdisc hierarchy (both TBF and netem on egress and ingress)

# Start local HTTP client (50MB file) in the background
echo -n "Starting local hq client for /largefile50M.bin... "
start=$(date +%s)
sleep 0.5 && /qw/proxygen/proxygen/_build/proxygen/httpserver/hq \
    --mode=client \
    --host=10.73.0.20 \
    --outdir=/qw/client \
    --path="/largefile50M.bin" \
    -qlogger_path=/qw/client/logs/ \
    -stream_flow_control=2147483647 > /dev/null 2>&1 &
HQ50_PID=$!
echo "done."

# -------------------------------------------------------------------
# Set static delay, queue, and initial bandwidth shaping once.
#
# This installs a qdisc hierarchy on eno1 (and via ifb1 for ingress) that
# includes a TBF qdisc (handle 1:) for bandwidth limiting and a netem child
# (handle 10:) for delay, loss, and packet queue limits.
#
# In this example:
#   - 1024 KBps is the initial bandwidth,
#   - 10ms is the delay,
#   - 0% loss,
#   - 80 KB is used for the TBF queue size,
#   - 67 packets is the netem limit.
ssh -o StrictHostKeyChecking=no balillus@10.73.0.20 \
  "sudo /home/balillus/qw/traffic_shaping/wan_emulation.sh tc_bw_delay_both eno1 1024 10 0 80 67"
echo "Static delay, queue and initial bandwidth set."
# -------------------------------------------------------------------

# Loop toggling between low and high bandwidth shaping until the client completes
while kill -0 $HQ50_PID 2>/dev/null; do
    # --- LOW BANDWIDTH UPDATE ---
    # Update bandwidth shaping to low: 1024 KBps with an 80KB queue.
    ssh -o StrictHostKeyChecking=no balillus@10.73.0.20 \
      "sudo /home/balillus/qw/traffic_shaping/wan_emulation.sh tc_update_bandwidth_both eno1 1024 80"
    echo "Low bandwidth updated"
    sleep 8

    # Check if the client is still running before switching
    if ! kill -0 $HQ50_PID 2>/dev/null; then
        break
    fi

    # --- HIGH BANDWIDTH UPDATE ---
    # Update bandwidth shaping to high: 2048 KBps with an 80KB queue.
    ssh -o StrictHostKeyChecking=no balillus@10.73.0.20 \
      "sudo /home/balillus/qw/traffic_shaping/wan_emulation.sh tc_update_bandwidth_both eno1 2048 80"
    echo "High bandwidth updated"
    sleep 8

    # Optionally print elapsed time
    end=$(date +%s)
    elapsed=$((end - start))
    echo "Elapsed time: $elapsed s"
done

# -------------------------------------------------------------------
# Clean up: Remove the entire qdisc hierarchy (both static delay and bandwidth shaping)
ssh -o StrictHostKeyChecking=no balillus@10.73.0.20 \
  "sudo /home/balillus/qw/traffic_shaping/wan_emulation.sh tc_del_bw_delay_both eno1"
echo "Static delay, queue and bandwidth shaping removed."
# -------------------------------------------------------------------

echo -n "Waiting for local hq client to complete... "
wait $HQ50_PID
echo "Local hq client transfer completed."

exit 0
