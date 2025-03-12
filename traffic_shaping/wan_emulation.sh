#!/bin/bash
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# Author: Gaetano Carlucci

tc="sudo /sbin/tc"
modprobe="sudo /sbin/modprobe"
ip="sudo /sbin/ip"

# This function disabled the NIC optimizations that interfere with the experiment
# INPUT PARAMETER
# 1 : Device interface that receives the traffic: example eth0
function disabe_nic_opt()
{
   DEV=$1
   echo "Optimization on $DEV disabled"
   sudo ethtool -K $DEV gro off
   sudo ethtool -K $DEV tso off
   sudo ethtool -K $DEV gso off
}

# This function applies egress bandwidth control, delay, and optional loss
# INPUT PARAMETERS:
# 1 - Bottleneck buffer size in KB (e.g., 30 for 30KB)
# 2 - Device interface (e.g., eno1)
# 3 - Bandwidth limit in KBps (e.g., 1250 for ~10MBps)
# 4 - Delay in ms (e.g., 50 for 50ms)
# 5 - Netem queue limit in packets (e.g., 1000)
# 6 - Packet loss percentage (e.g., 0.5 for 0.5%)
function tc_egress_with_delay() {
   QUEUE=$1
   DEV=$2
   KBPS=$3
   DELAY=$4
   LIMIT_PACKETS=$5
   LOSS=$6
   BDP_BYTES=$7
   BRATE=$((KBPS * 8)) # Convert KBps to kbps
   MTU=1000
   LIMIT=$((MTU * QUEUE)) # Queue length in bytes

   echo "Applying egress bandwidth, delay, and loss on $DEV"
   echo "* Bandwidth: ${BRATE}kbit (${KBPS} KBps)"
   echo "* Delay: ${DELAY}ms"
   echo "* Loss: ${LOSS}%"
   echo "* Netem Queue Limit: ${LIMIT_PACKETS} packets"

   # Add TBF as root qdisc for bandwidth control
   $tc qdisc add dev $DEV root handle 1: tbf rate ${BRATE}kbit \
       minburst $MTU burst $((BDP_BYTES * 10)) limit $LIMIT

   # Add NetEm as a child qdisc for delay, loss, with packet limit
   $tc qdisc add dev $DEV parent 1:1 handle 10: netem \
       delay ${DELAY}ms loss ${LOSS}% limit ${LIMIT_PACKETS}
}

# This function removes both egress bandwidth control and delay
# INPUT PARAMETER: 1 - Device interface (e.g., eno1)
function tc_del_egress_with_delay() {
   DEV=$1
   echo "Removing egress bandwidth, delay, and loss on $DEV"
   $tc qdisc del dev $DEV root
}

# This function introduces link capacity constraints on incoming traffic that comes from an IP address
# INPUT PARAMETER
# 1 : IP address of the sender machine: example 192.168.0.10
# 2 : Bottleneck buffer size in KB (1000 byte): example 30 (30KB)
# 3 : Device interface that receives the traffic: example eth0
# 4 : Capacity constraint: example 250 KBps (equivalent to 2Mbps)
function tc_ingress()
{
   SRC=$1
   QUEUE=$2
   DEV=$3
   KBPS=$4 # kilo bytes per seconds
   BRATE=$((KBPS*8)) #BRATE should be in kbps
   MTU=1000
   LIMIT=$((MTU*QUEUE)) #Queue length in bytes

   echo "TC SHAPER INGRESS ON $HOSTNAME"
   echo "* rate ${BRATE}kbit ($KBPS kbyte/s)"
   echo "* ip src: $SRC"
   echo "* dev $DEV"

   $modprobe ifb
   $ip link set  dev ifb1 up
   $tc qdisc add dev $DEV ingress

   $tc filter add dev $DEV parent ffff: protocol ip u32 match ip src $SRC flowid 1:1 action mirred egress redirect dev ifb1
   $tc qdisc add dev ifb1 root handle 1: tbf rate ${BRATE}kbit \
       minburst $MTU burst $((MTU*10)) limit $LIMIT
}

# This function introduces link capacity constraints on incoming traffic
# INPUT PARAMETER
# 1 : Bottleneck buffer size in KB (1000 byte): example 30 (30KB)
# 2 : Device interface that receives the traffic: example eth0
# 3 : Capacity constraint: example 250 KBps (equivalent to 2Mbps)
function tc_ingress_all()
{
   QUEUE=$1
   DEV=$2
   KBPS=$3 # kilo bytes per seconds
   BRATE=$((KBPS*8)) #BRATE should be in kbps
   MTU=1000
   LIMIT=$((MTU*QUEUE)) #Queue length in bytes

   echo "TC SHAPER INGRESS ON $HOSTNAME"
   echo "* rate ${BRATE}kbit ($KBPS KB/s)"
   echo "* dev $DEV"

   $modprobe ifb
   $ip link set  dev ifb1 up
   $tc qdisc add dev $DEV ingress

   $tc filter add dev $DEV parent ffff: protocol ip u32 match u32 0 0 flowid 1:1 \
       action mirred egress redirect dev ifb1
   $tc qdisc add dev ifb1 root handle 1: tbf rate ${BRATE}kbit \
       minburst $MTU burst $((MTU*10)) limit $LIMIT
}

# This function removes the capacity constraint on the incoming traffic
# INPUT PARAMETER
# 1 : Device interface that receives the traffic: example eth0
function tc_del_ingress() {
   DEV=$1
   $tc qdisc del dev $DEV ingress 2>/dev/null
   $tc qdisc del dev ifb1 root 2>/dev/null
   $ip link set dev ifb1 down
   echo "Bandwidth constraint ingress turned off on $DEV"
}

# This function introduces link capacity constraints on outgoing traffic
# INPUT PARAMETER
# 1 : Bottleneck buffer size in KB (1000 byte): example 30 (30KB)
# 2 : Device interface that sends the traffic: example eth0
# 3 : Capacity constraint: example 250 KBps (equivalent to 2Mbps)
function tc_egress() {
   QUEUE=$1
   DEV=$2
   KBPS=$3 # kilo bytes per seconds
   BRATE=$((KBPS*8)) #BRATE should be in kbps
   MTU=1000
   LIMIT=$((MTU*QUEUE)) #Queue length in bytes

   echo "TC SHAPER EGRESS ON $HOSTNAME"
   echo "* rate ${BRATE}kbit ($KBPS KB/s)"
   echo "* dev $DEV"

   $tc qdisc add dev $DEV root handle 1: tbf rate ${BRATE}kbit \
       minburst $MTU burst $((MTU*10)) limit $LIMIT
}

# This function removes the capacity constraint on the outgoing traffic
# INPUT PARAMETER
# 1 : Device interface that sends the traffic: example eth0
function tc_del_egress() {
   DEV=$1
   $tc qdisc del dev $DEV root 2>/dev/null
   echo "Bandwidth constraint egress turned off on $DEV"
}

# This function introduces propagation delay on the traffic over the specified interface
# INPUT PARAMETER
# 1 : Device interface that introduces the delay on the traffic: example eth0
# 2 : Delay we want to set in ms: example 50 ms
function tc_delay() {
   DEV=$1
   DELAY=$2
   echo "tc_delay: dev: $DEV delay: $DELAY ms"
   $tc qdisc add dev $DEV root netem delay ${DELAY}ms
}

# This function removes propagation delay 
# INPUT PARAMETER
# 1 : Device interface that introduces the delay on the traffic: example eth0
function tc_del_delay()
{
   DEV=$1
   $tc qdisc del dev $DEV root 2>/dev/null
   echo "Delay turned off on $DEV"
}

# This function adds fair queuing policy on incoming traffic 
# Must be executed after tc_ingress or tc_ingress_all
function add_sfq_ingress()
{
   $tc qdisc add dev ifb1 parent 1:1 handle 10: sfq perturb 10  
}

# This function adds fair queuing policy on outgoing traffic 
# Must be execute after tc_egress
# INPUT PARAMETER
# 1 : Device interface 
function add_sfq_egress()
{
   DEV=$1
   $tc qdisc add dev $DEV parent 1:1 handle 10: sfq perturb 10
}

# This function applies INGRESS bandwidth control, delay, and optional loss,
# similarly to tc_egress_with_delay, but via an ifb device.
# INPUT PARAMETERS:
# 1 - Bottleneck buffer size in KB (e.g., 30 for 30KB)
# 2 - Device interface (e.g., eth0)
# 3 - Bandwidth limit in KBps (e.g., 1250 for ~10Mb/s)
# 4 - Delay in ms (e.g., 50 for 50ms)
# 5 - Netem queue limit in packets (e.g., 1000)
# 6 - Packet loss percentage (e.g., 0.5 for 0.5%)
function tc_ingress_with_delay() {
   QUEUE=$1
   DEV=$2
   KBPS=$3
   DELAY=$4
   LIMIT_PACKETS=$5
   LOSS=$6
   BDP_BYTES=$7
   BRATE=$((KBPS * 8)) # Convert KBps to kbps
   MTU=1000
   LIMIT=$((MTU * QUEUE)) # Queue length in bytes

   echo "Applying ingress bandwidth + delay + loss on $DEV using ifb1"
   echo "* Bandwidth: ${BRATE}kbit (${KBPS} KBps)"
   echo "* Delay: ${DELAY}ms"
   echo "* Loss: ${LOSS}%"
   echo "* Netem Queue Limit: ${LIMIT_PACKETS} packets"

   # 1) Load the ifb module and bring up the ifb1 interface
   $modprobe ifb
   $ip link set dev ifb1 up

   # 2) Attach ingress qdisc on $DEV, and redirect all traffic to ifb1
   $tc qdisc add dev $DEV ingress
   $tc filter add dev $DEV parent ffff: protocol ip u32 match u32 0 0 \
       flowid 1:1 action mirred egress redirect dev ifb1

   # 3) On ifb1, install TBF as root for bandwidth limiting
   $tc qdisc add dev ifb1 root handle 1: tbf rate ${BRATE}kbit \
       minburst $MTU burst $((BDP_BYTES * 10)) limit $LIMIT

   # 4) Attach netem as a child for delay + loss + queue limit
   $tc qdisc add dev ifb1 parent 1:1 handle 10: netem \
       delay ${DELAY}ms loss ${LOSS}% limit ${LIMIT_PACKETS}
}

# This function removes both ingress bandwidth control, delay, and loss
# INPUT PARAMETER:
# 1 - Device interface (e.g., eth0)
function tc_del_ingress_with_delay() {
   DEV=$1
   echo "Removing ingress bandwidth + delay + loss on $DEV (ifb1)"

   # 1) Delete root qdisc on ifb1, then bring ifb1 down
   $tc qdisc del dev ifb1 root 2>/dev/null
   $ip link set dev ifb1 down

   # 2) Delete the ingress qdisc on $DEV (which removes the redirect)
   $tc qdisc del dev $DEV ingress 2>/dev/null
}


# Usage:
#   tc_bw_delay_both <DEV> <KBPS> <DELAY_MS> <LOSS_PERCENT>
#
# Where:
#   DEV       = network interface (e.g., eno1)
#   KBPS      = bandwidth in kilobytes per second (1 KB = 1024 bytes)
#   DELAY_MS  = one-way delay in milliseconds
#   LOSS      = percentage of packet loss (e.g., 0.5)
#
# Example usage:
#   tc_bw_delay_both eno1 10000 50 0.5
#    -> egress + ingress shaping
#       - ~10,000 KBps limit (≈ 10 MB/s)
#       - 50 ms one-way delay
#       - 0.5% packet loss
#       - auto-calculated queue & limit
function tc_bw_delay_both() {
    local DEV=$1
    local KBPS=$2       # kilobytes per second
    local DELAY_MS=$3
    local LOSS=$4

    echo ">>> Applying BOTH ingress + egress shaping on $DEV"
    echo "    * target rate=${KBPS}KBps, delay=${DELAY_MS}ms, loss=${LOSS}%"

    # 1) Convert KBPS (kilobytes/s) to bytes/s
    #    For 1 KB = 1024 bytes:
    local BYTES_PER_SEC=$(( KBPS * 1024 ))

    # 2) Calculate Bandwidth-Delay Product (BDP) in bytes
    #    BDP = throughput (bytes/s) * delay (seconds)
    #    Here we assume one-way delay, not round-trip.
    local BDP_BYTES=$(( BYTES_PER_SEC * DELAY_MS / 1000 ))

    # 3) Decide a queue size in KB (multiply BDP by a safety factor)
    local FACTOR=4
    local QUEUE_KB=$(( (BDP_BYTES * FACTOR) / 1024 ))
    if [ "$QUEUE_KB" -lt 1 ]; then
        QUEUE_KB=1
    fi

    # 4) Decide limit in packets by dividing BDP by average packet size (1252 bytes)
    local AVG_PKT=1250
    local LIMIT_PKTS=$(( (BDP_BYTES * FACTOR) / AVG_PKT ))
    if [ "$LIMIT_PKTS" -lt 10 ]; then
        LIMIT_PKTS=10
    fi

    echo "    * auto-calculated queue ~${QUEUE_KB}KB, limit ~${LIMIT_PKTS} pkts"

    # Apply egress shaping
    tc_egress_with_delay "$QUEUE_KB" "$DEV" "$KBPS" "$DELAY_MS" "$LIMIT_PKTS" "$LOSS" "$BDP_BYTES"

    # Apply ingress shaping
    tc_ingress_with_delay "$QUEUE_KB" "$DEV" "$KBPS" "$DELAY_MS" "$LIMIT_PKTS" "$LOSS" "$BDP_BYTES"
}


# Usage:
#    tc_del_bw_delay_both  <DEV>
#
# Example:
#    tc_del_bw_delay_both eno1
#
# This removes egress shaping + ingress shaping on the same interface.
function tc_del_bw_delay_both() {
    DEV=$1
    echo ">>> Removing BOTH ingress + egress shaping on $DEV"

    # Remove egress shaping
    tc_del_egress_with_delay "$DEV"

    # Remove ingress shaping
    tc_del_ingress_with_delay "$DEV"
}

# This function applies delay and sets a queue limit (number of packets)
# on both egress and ingress traffic.
#
# INPUT PARAMETERS:
#   1 : Device interface (e.g., eth0)
#   2 : Delay in milliseconds (e.g., 50)
#   3 : Netem queue limit in packets (e.g., 1000)
function tc_delay_queue_limit_both() {
    local DEV=$1
    local DELAY_MS=$2
    local LIMIT_PKTS=$3

    echo ">>> Applying delay (${DELAY_MS}ms) and queue limit (${LIMIT_PKTS} pkts) on both ingress and egress for $DEV"

    # --- EGRESS ---
    # Apply netem directly on the egress side
    $tc qdisc add dev $DEV root netem delay ${DELAY_MS}ms limit ${LIMIT_PKTS}

    # --- INGRESS ---
    # Load the ifb module and bring up ifb1 for ingress shaping
    $modprobe ifb
    $ip link set dev ifb1 up

    # Add an ingress qdisc on $DEV and redirect all traffic to ifb1
    $tc qdisc add dev $DEV ingress
    $tc filter add dev $DEV parent ffff: protocol ip u32 match u32 0 0 \
         action mirred egress redirect dev ifb1

    # Apply netem on the ifb1 interface for ingress delay and limit
    $tc qdisc add dev ifb1 root netem delay ${DELAY_MS}ms limit ${LIMIT_PKTS}
}

# This function applies bandwidth limitations using TBF on both egress and ingress traffic.
#
# INPUT PARAMETERS:
#   1 : Device interface (e.g., eth0)
#   2 : Bandwidth limit in kilobytes per second (KBps)
#   3 : Queue size in KB (this value is used to calculate the byte limit; e.g., 30)
function tc_bandwidth_both() {
    local DEV=$1
    local KBPS=$2
    local QUEUE_KB=$3

    # Convert KBps to kbit/s for tc (1 KBps ~ 8 kbit/s)
    local BRATE=$(( KBPS * 8 ))
    local MTU=1000
    local BURST=$(( MTU * 10 ))
    local LIMIT=$(( MTU * QUEUE_KB ))

    echo ">>> Applying bandwidth limitation on both ingress and egress for $DEV"
    echo "    * Bandwidth: ${BRATE}kbit (${KBPS} KBps)"
    echo "    * Queue size: ${QUEUE_KB}KB (limit: ${LIMIT} bytes)"

    # --- EGRESS ---
    # Install TBF as the root qdisc for egress bandwidth shaping
    $tc qdisc add dev $DEV root handle 1: tbf rate ${BRATE}kbit \
         minburst $MTU burst ${BURST} limit ${LIMIT}

    # --- INGRESS ---
    # Load the ifb module and bring up ifb1 for ingress shaping
    $modprobe ifb
    $ip link set dev ifb1 up

    # Add an ingress qdisc on $DEV and redirect all traffic to ifb1
    $tc qdisc add dev $DEV ingress
    $tc filter add dev $DEV parent ffff: protocol ip u32 match u32 0 0 \
         action mirred egress redirect dev ifb1

    # Install TBF on ifb1 to limit the ingress bandwidth
    $tc qdisc add dev ifb1 root handle 1: tbf rate ${BRATE}kbit \
         minburst $MTU burst ${BURST} limit ${LIMIT}
}

# This function updates the bandwidth limitation (TBF parameters) on both
# egress and ingress traffic, while leaving the static delay/queue (netem)
# configuration intact.
#
# INPUT PARAMETERS:
#   1 : Device interface (e.g., eno1)
#   2 : Bandwidth limit in kilobytes per second (KBps)
#   3 : Queue size in KB for the TBF (e.g., 80)
function tc_update_bandwidth_both() {
    local DEV=$1
    local KBPS=$2
    local QUEUE_KB=$3

    # Convert KBps to kbit/s (1 KBps ≈ 8 kbit/s)
    local BRATE=$(( KBPS * 8 ))
    local MTU=1000
    local BURST=$(( MTU * 10 ))
    local LIMIT=$(( MTU * QUEUE_KB ))

    echo ">>> Updating bandwidth limitation on both ingress and egress for $DEV"
    echo "    * Bandwidth: ${BRATE}kbit (${KBPS} KBps)"
    echo "    * Queue size: ${QUEUE_KB}KB (limit: ${LIMIT} bytes)"

    # Update the TBF qdisc on the egress side (which is at the root on $DEV)
    $tc qdisc change dev $DEV root handle 1: tbf rate ${BRATE}kbit \
         minburst $MTU burst ${BURST} limit ${LIMIT}

    # Update the TBF qdisc on the ingress side (on ifb1)
    $tc qdisc change dev ifb1 root handle 1: tbf rate ${BRATE}kbit \
         minburst $MTU burst ${BURST} limit ${LIMIT}
}


# Finally, run the passed command.
$@
