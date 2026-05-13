#!/bin/bash
# run_linux_benchmark.sh

# Usage: ./run_linux_benchmark.sh [on|off]
# Default is ON if no argument provided.

CULLING_STATE="--culling-on"
if [ "$1" == "off" ]; then
    CULLING_STATE="--culling-off"
    echo "Mode: Frustum Culling OFF"
else
    echo "Mode: Frustum Culling ON"
fi

# Create benchmarks directory if it doesn't exist
mkdir -p ./benchmarks

# Check if MangoHud is installed
if ! command -v mangohud &> /dev/null
then
    echo "MangoHud is not installed. Please install it using:"
    echo "sudo apt install mangohud"
    exit 1
fi

echo "------------------------------------------------"
echo "Steer Engine Automated Benchmark (Linux)"
echo "------------------------------------------------"
echo "Startup Delay: 60 Seconds (Waiting for engine to load...)"
echo "Duration: 60 Seconds"
echo "Mode: Auto-start logging + Deterministic Mode"
echo "Culling: $CULLING_STATE"
echo "Output: ./benchmarks/"
echo "------------------------------------------------"

# Run the engine
# Added 'log_delay=60' to the config string below
# MANGOHUD_CONFIG="log_duration=60,output_folder=./benchmarks,full,autostart_log=1" \
MANGOHUD_CONFIG="log_delay=60,log_duration=60,output_folder=./benchmarks,full,autostart_log=1" \
mangohud ./Bin/Engine --benchmark $CULLING_STATE

echo ""
echo "Benchmark finished. Check the ./benchmarks/ folder for the CSV file."
