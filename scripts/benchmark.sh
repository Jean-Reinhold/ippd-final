#!/bin/bash
# Benchmark the simulation across various MPI × OpenMP configurations.
# Uses --csv mode for per-phase timing data.
#
# Configurations tested:
#   NP      = 1, 2, 4
#   THREADS = 1, 2, 4
#
# Outputs:
#   benchmark_results/<timestamp>/np<N>_t<T>.csv   — per-run CSV
#   benchmark_results/<timestamp>/summary.csv       — averages per config
set -e

cd "$(dirname "$0")/.."

# Build first
make clean && make all

WIDTH=${WIDTH:-64}
HEIGHT=${HEIGHT:-64}
CYCLES=${CYCLES:-100}
AGENTS=${AGENTS:-50}

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTDIR="benchmark_results/${TIMESTAMP}"
mkdir -p "$OUTDIR"

echo "============================================="
echo " Benchmark: ${WIDTH}x${HEIGHT} grid, ${CYCLES} cycles, ${AGENTS} agents"
echo " Output:    ${OUTDIR}/"
echo "============================================="

# Summary CSV header
SUMMARY="${OUTDIR}/summary.csv"
echo "np,threads,avg_compute_ms,avg_halo_ms,avg_migrate_ms,avg_metrics_ms,avg_cycle_ms,wall_time_s" \
    > "$SUMMARY"

printf "%-6s %-8s %-12s %-12s %-12s %-12s %-12s\n" \
    "NP" "Threads" "Compute" "Halo" "Migrate" "Cycle(ms)" "Wall(s)"
printf "%-6s %-8s %-12s %-12s %-12s %-12s %-12s\n" \
    "------" "--------" "------------" "------------" "------------" "------------" "------------"

for NP in 1 2 4; do
    for THREADS in 1 2 4; do
        export OMP_NUM_THREADS=$THREADS
        RUNFILE="${OUTDIR}/np${NP}_t${THREADS}.csv"

        # Portable high-res timing (macOS doesn't support date +%s%N)
        T_START=$(python3 -c "import time; print(time.time())")

        mpirun --oversubscribe -np "$NP" ./sim \
            -w "$WIDTH" -h "$HEIGHT" -c "$CYCLES" -a "$AGENTS" \
            --no-tui --csv > "$RUNFILE" 2>/dev/null

        T_END=$(python3 -c "import time; print(time.time())")
        WALL=$(python3 -c "print(f'{${T_END} - ${T_START}:.3f}')")

        # Parse per-run CSV to compute averages (skip header line)
        AVGS=$(awk -F',' 'NR > 1 {
            compute += $3; halo += $4; migrate += $5;
            metrics += $6; cycle += $7; n++
        } END {
            if (n > 0)
                printf "%.3f,%.3f,%.3f,%.3f,%.3f",
                    compute/n, halo/n, migrate/n, metrics/n, cycle/n
            else
                printf "0,0,0,0,0"
        }' "$RUNFILE")

        echo "${NP},${THREADS},${AVGS},${WALL}" >> "$SUMMARY"

        # Extract individual averages for display
        AVG_COMPUTE=$(echo "$AVGS" | cut -d',' -f1)
        AVG_HALO=$(echo "$AVGS" | cut -d',' -f2)
        AVG_MIGRATE=$(echo "$AVGS" | cut -d',' -f3)
        AVG_CYCLE=$(echo "$AVGS" | cut -d',' -f5)

        printf "%-6s %-8s %-12s %-12s %-12s %-12s %-12s\n" \
            "$NP" "$THREADS" "$AVG_COMPUTE" "$AVG_HALO" "$AVG_MIGRATE" "$AVG_CYCLE" "$WALL"
    done
done

echo "============================================="
echo " Benchmark complete"
echo " Summary: ${SUMMARY}"
echo " Per-run CSVs: ${OUTDIR}/np*_t*.csv"
echo "============================================="
