#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Configuration via environment variables (with defaults)
PACKET_LOSS_RATE=${PACKET_LOSS_RATE:-10}      # Packet loss percentage (0-100)
CHECK_INTERVAL=${CHECK_INTERVAL:-0.01}       # Interval to check for traffic (seconds, default: 10ms)
IDLE_TIMEOUT=${IDLE_TIMEOUT:-0.1}            # Time to wait before disabling packet loss when no traffic (seconds, default: 100ms)
INTERFACE_A=${INTERFACE_A:-eth0}              # First network interface
INTERFACE_B=${INTERFACE_B:-eth1}              # Second network interface

echo "=========================================="
echo "Firewall Configuration:"
echo "  Packet Loss Rate: ${PACKET_LOSS_RATE}%"
echo "  Check Interval: ${CHECK_INTERVAL}s ($(echo "${CHECK_INTERVAL} * 1000" | bc 2>/dev/null || echo "${CHECK_INTERVAL}")ms)"
echo "  Idle Timeout: ${IDLE_TIMEOUT}s ($(echo "${IDLE_TIMEOUT} * 1000" | bc 2>/dev/null || echo "${IDLE_TIMEOUT}")ms)"
echo "  Interface A: ${INTERFACE_A}"
echo "  Interface B: ${INTERFACE_B}"
echo "=========================================="

# Function to enable IP forwarding
enable_ip_forwarding() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Enabling IP forwarding..."
    sysctl -w net.ipv4.ip_forward=1
}

# Function to set up basic forwarding rules
setup_forwarding_rules() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Setting up iptables forwarding rules..."
    # Forward traffic from netA to netB
    iptables -A FORWARD -i ${INTERFACE_A} -o ${INTERFACE_B} -j ACCEPT
    # Forward traffic from netB to netA
    iptables -A FORWARD -i ${INTERFACE_B} -o ${INTERFACE_A} -j ACCEPT
    # Allow established connections
    iptables -A FORWARD -m state --state ESTABLISHED,RELATED -j ACCEPT
}

# Function to cleanup existing tc rules
cleanup_tc() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Cleaning up existing tc rules..."
    # Remove existing qdiscs if they exist
    tc qdisc del dev ${INTERFACE_A} root 2>/dev/null || true
    tc qdisc del dev ${INTERFACE_B} root 2>/dev/null || true
    tc qdisc del dev ${INTERFACE_A} ingress 2>/dev/null || true
    tc qdisc del dev ${INTERFACE_B} ingress 2>/dev/null || true
}

# Function to enable packet loss on both interfaces
enable_packet_loss() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] 🔴 ENABLING packet loss (${PACKET_LOSS_RATE}%)..."
    
    # Apply packet loss to interface A (outgoing traffic)
    tc qdisc add dev ${INTERFACE_A} root netem loss ${PACKET_LOSS_RATE}%
    
    # Apply packet loss to interface B (outgoing traffic)
    tc qdisc add dev ${INTERFACE_B} root netem loss ${PACKET_LOSS_RATE}%
}

# Function to disable packet loss on both interfaces
disable_packet_loss() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] 🟢 DISABLING packet loss (normal forwarding)..."
    
    # Remove packet loss rules
    tc qdisc del dev ${INTERFACE_A} root 2>/dev/null || true
    tc qdisc del dev ${INTERFACE_B} root 2>/dev/null || true
}

# Function to get total packet count from both interfaces
get_packet_count() {
    local count_a=0
    local count_b=0
    
    # Get packet count from interface A (tx + rx)
    if [ -f "/sys/class/net/${INTERFACE_A}/statistics/tx_packets" ]; then
        count_a=$(cat /sys/class/net/${INTERFACE_A}/statistics/tx_packets 2>/dev/null || echo 0)
        count_a=$((count_a + $(cat /sys/class/net/${INTERFACE_A}/statistics/rx_packets 2>/dev/null || echo 0)))
    fi
    
    # Get packet count from interface B (tx + rx)
    if [ -f "/sys/class/net/${INTERFACE_B}/statistics/tx_packets" ]; then
        count_b=$(cat /sys/class/net/${INTERFACE_B}/statistics/tx_packets 2>/dev/null || echo 0)
        count_b=$((count_b + $(cat /sys/class/net/${INTERFACE_B}/statistics/rx_packets 2>/dev/null || echo 0)))
    fi
    
    echo $((count_a + count_b))
}

# Function to start the firewall loop - only enable when traffic is detected
start_firewall_loop() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Starting traffic-aware firewall..."
    echo "Firewall will enable packet loss ONLY when traffic is detected"
    echo "Press Ctrl+C to stop"
    echo ""
    
    # Initial state: packet loss disabled (normal forwarding)
    local is_enabled=false
    local last_packet_count=$(get_packet_count)
    local idle_checks=0
    # Calculate how many checks equal IDLE_TIMEOUT
    local max_idle_checks=$(echo "scale=0; ${IDLE_TIMEOUT} / ${CHECK_INTERVAL}" | bc 2>/dev/null || echo "10")
    # Fallback if bc is not available: use integer division
    if ! command -v bc &> /dev/null; then
        max_idle_checks=$(awk "BEGIN {printf \"%.0f\", ${IDLE_TIMEOUT} / ${CHECK_INTERVAL}}")
    fi
    
    while true; do
        sleep ${CHECK_INTERVAL}
        
        local current_packet_count=$(get_packet_count)
        local packet_delta=$((current_packet_count - last_packet_count))
        
        # Check if there's traffic (packet count increased)
        if [ $packet_delta -gt 0 ]; then
            idle_checks=0  # Reset idle counter
            
            # If packet loss is not enabled, enable it
            if [ "$is_enabled" = false ]; then
                enable_packet_loss
                is_enabled=true
            fi
        else
            # No traffic detected
            if [ "$is_enabled" = true ]; then
                idle_checks=$((idle_checks + 1))
                # If we've been idle for enough checks, disable packet loss
                if [ $idle_checks -ge $max_idle_checks ]; then
                    disable_packet_loss
                    is_enabled=false
                    idle_checks=0
                fi
            fi
        fi
        
        last_packet_count=$current_packet_count
    done
}

# Function to handle cleanup on exit
cleanup_on_exit() {
    echo ""
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Cleaning up and exiting..."
    disable_packet_loss
    cleanup_tc
    exit 0
}

# Set up signal handlers for graceful shutdown
trap cleanup_on_exit SIGTERM SIGINT

# Main execution
enable_ip_forwarding
setup_forwarding_rules
cleanup_tc
start_firewall_loop
