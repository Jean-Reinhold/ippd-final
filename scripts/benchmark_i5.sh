#!/bin/bash
# Benchmark the simulation across various MPI × OpenMP configurations.
# Runs with --no-tui for accurate timing.
#
# Configurations tested:
#   NP     = 1, 2, 4
#   THREADS = 1, 2, 4
set -e

cd "$(dirname "$0")/.."

# Build first
make clean && make all

WIDTH=${WIDTH:-256}
HEIGHT=${HEIGHT:-256}
CYCLES=${CYCLES:-100}
AGENTS=${AGENTS:-20000}

echo "============================================="
echo " Benchmark: ${WIDTH}x${HEIGHT} grid, ${CYCLES} cycles, ${AGENTS} agents"
echo "============================================="
printf "%-6s %-8s %-12s\n" "NP" "Threads" "Time (s)"
printf "%-6s %-8s %-12s\n" "------" "--------" "------------"

for NP in 1 2 4; do
    for THREADS in 1 2 3 4; do
        export OMP_NUM_THREADS=$THREADS

        # Time the simulation run
        START=$(date +%s%N)
        mpirun --oversubscribe -np "$NP" ./sim \
            -w "$WIDTH" -h "$HEIGHT" -c "$CYCLES" -a "$AGENTS" \
            --no-tui 2>/dev/null
        END=$(date +%s%N)

        # Calculate elapsed time in seconds (with millisecond precision)
        ELAPSED=$(echo "scale=3; ($END - $START) / 1000000000" | bc)

        printf "%-6s %-8s %-12s\n" "$NP" "$THREADS" "$ELAPSED"
    done
done

echo "============================================="
echo " Benchmark complete"
echo "============================================="
