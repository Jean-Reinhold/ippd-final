#!/bin/bash
# Benchmark the simulation across various MPI × OpenMP configurations,
# multiple problem sizes, and multiple runs per config.
#
# Key improvements over previous version:
#   - Multiple runs per config (RUNS=3) with stddev
#   - Warmup exclusion (WARMUP=5 cycles skipped from averages)
#   - Multiple problem sizes (WxH:agents:cycles)
#   - Wider NP/thread range (1..8, configurable)
#   - Grid-divisibility check: configs where NP doesn't divide grid cleanly are skipped
#   - Per-size subdirectories with per-run CSVs
#   - Summary CSV with mean ± stddev for all 7 phase columns
#
# Outputs:
#   benchmark_results/<timestamp>/<WxH>/np<N>_t<T>_run<R>.csv  — per-run CSV
#   benchmark_results/<timestamp>/summary.csv                    — aggregated summary
set -e

cd "$(dirname "$0")/.."

# Build first
make clean && make all

# ── Configurable parameters ──
RUNS=${RUNS:-3}
WARMUP=${WARMUP:-5}
SIZES=${SIZES:-"64x64:50:100 128x128:200:100 256x256:500:50"}
NP_LIST=${NP_LIST:-"1 2 4 8"}
THREAD_LIST=${THREAD_LIST:-"1 2 4 8"}

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTDIR="benchmark_results/${TIMESTAMP}"
mkdir -p "$OUTDIR"

echo "============================================="
echo " Benchmark Suite"
echo " Sizes:   ${SIZES}"
echo " NP:      ${NP_LIST}"
echo " Threads: ${THREAD_LIST}"
echo " Runs:    ${RUNS}  Warmup: ${WARMUP} cycles"
echo " Output:  ${OUTDIR}/"
echo "============================================="

# Summary CSV header (mean AND stddev for each of 7 phases + cycle)
SUMMARY="${OUTDIR}/summary.csv"
echo "size,np,threads,mean_season_ms,std_season_ms,mean_halo_ms,std_halo_ms,mean_workload_ms,std_workload_ms,mean_agent_ms,std_agent_ms,mean_grid_ms,std_grid_ms,mean_migrate_ms,std_migrate_ms,mean_metrics_ms,std_metrics_ms,mean_cycle_ms,std_cycle_ms,mean_workload_pct,mean_comm_pct,wall_time_s" \
    > "$SUMMARY"

# ── Helper: compute best factorization px×py for NP ──
# Returns "px py" such that px*py == NP and px <= py (wider grids get more columns).
factorize() {
    local n=$1
    local best_px=1
    local best_py=$n
    local i=1
    while [ $((i * i)) -le "$n" ]; do
        if [ $((n % i)) -eq 0 ]; then
            best_px=$i
            best_py=$((n / i))
        fi
        i=$((i + 1))
    done
    echo "$best_px $best_py"
}

for SIZE_SPEC in $SIZES; do
    # Parse "WxH:agents:cycles"
    IFS=':' read -r WH AGENTS CYCLES <<< "$SIZE_SPEC"
    IFS='x' read -r WIDTH HEIGHT <<< "$WH"

    SIZE_DIR="${OUTDIR}/${WH}"
    mkdir -p "$SIZE_DIR"

    echo ""
    echo "─── Size: ${WIDTH}×${HEIGHT}, ${AGENTS} agents, ${CYCLES} cycles ───"
    printf "%-4s %-4s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n" \
        "NP" "Thr" "Season" "Halo" "Workload" "Agent" "Grid" "Migrate" "Cycle(ms)" "Wall(s)"
    printf "%-4s %-4s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n" \
        "----" "----" "----------" "----------" "----------" "----------" "----------" "----------" "----------" "----------"

    for NP in $NP_LIST; do
        # Check grid divisibility
        read PX PY <<< "$(factorize "$NP")"
        if [ $((WIDTH % PX)) -ne 0 ] || [ $((HEIGHT % PY)) -ne 0 ]; then
            # Try swapped factorization
            if [ $((WIDTH % PY)) -ne 0 ] || [ $((HEIGHT % PX)) -ne 0 ]; then
                echo "  SKIP NP=${NP}: grid ${WIDTH}×${HEIGHT} not divisible by ${PX}×${PY}"
                continue
            fi
        fi

        for THREADS in $THREAD_LIST; do
            export OMP_NUM_THREADS=$THREADS

            # Collect per-run CSVs
            ALL_RUN_FILES=""
            T_WALL_START=$(python3 -c "import time; print(time.time())")

            for RUN in $(seq 1 "$RUNS"); do
                RUNFILE="${SIZE_DIR}/np${NP}_t${THREADS}_run${RUN}.csv"
                mpirun --oversubscribe -np "$NP" ./sim \
                    -w "$WIDTH" -h "$HEIGHT" -c "$CYCLES" -a "$AGENTS" \
                    --no-tui --csv > "$RUNFILE" 2>/dev/null
                ALL_RUN_FILES="${ALL_RUN_FILES} ${RUNFILE}"
            done

            T_WALL_END=$(python3 -c "import time; print(time.time())")
            WALL=$(python3 -c "print(f'{${T_WALL_END} - ${T_WALL_START}:.3f}')")

            # Aggregate across all runs: compute mean and stddev for each phase,
            # excluding the first WARMUP cycles from each run.
            # CSV columns: 1=cycle, 2=season, 3=season_ms, 4=halo_ms, 5=workload_ms,
            #   6=agent_ms, 7=grid_ms, 8=migrate_ms, 9=metrics_ms, 10=cycle_ms,
            #   11=total_agents, 12=total_resource, 13=avg_energy,
            #   14=load_balance, 15=workload_pct, 16=comm_pct
            STATS=$(awk -F',' -v warmup="$WARMUP" '
            NR == 1 { next }  # skip header of first file
            /^cycle,/ { next }  # skip headers of subsequent files
            {
                cyc = $1 + 0
                if (cyc < warmup) next

                n++
                for (i = 1; i <= 7; i++) {
                    col = i + 2   # columns 3..9 = season_ms..metrics_ms
                    v = $(col) + 0
                    sum[i] += v
                    sumsq[i] += v * v
                }
                # cycle_ms is column 10
                sum[8] += $10 + 0
                sumsq[8] += ($10 + 0) * ($10 + 0)
                # workload_pct and comm_pct
                sum_wpct += $15 + 0
                sum_cpct += $16 + 0
            }
            END {
                if (n == 0) { print "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"; exit }
                for (i = 1; i <= 8; i++) {
                    mean[i] = sum[i] / n
                    var = sumsq[i] / n - mean[i] * mean[i]
                    if (var < 0) var = 0
                    std[i] = sqrt(var)
                }
                printf "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f", \
                    mean[1], std[1], mean[2], std[2], mean[3], std[3], \
                    mean[4], std[4], mean[5], std[5], mean[6], std[6], \
                    mean[7], std[7], mean[8], std[8], \
                    sum_wpct / n, sum_cpct / n
            }' $ALL_RUN_FILES)

            echo "${WH},${NP},${THREADS},${STATS},${WALL}" >> "$SUMMARY"

            # Extract means for display
            M_SEASON=$(echo "$STATS" | cut -d',' -f1)
            M_HALO=$(echo "$STATS" | cut -d',' -f3)
            M_WORK=$(echo "$STATS" | cut -d',' -f5)
            M_AGENT=$(echo "$STATS" | cut -d',' -f7)
            M_GRID=$(echo "$STATS" | cut -d',' -f9)
            M_MIGRATE=$(echo "$STATS" | cut -d',' -f11)
            M_CYCLE=$(echo "$STATS" | cut -d',' -f15)

            printf "%-4s %-4s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n" \
                "$NP" "$THREADS" "$M_SEASON" "$M_HALO" "$M_WORK" "$M_AGENT" "$M_GRID" "$M_MIGRATE" "$M_CYCLE" "$WALL"
        done
    done
done

echo ""
echo "============================================="
echo " Benchmark complete"
echo " Summary: ${SUMMARY}"
echo " Per-run CSVs: ${OUTDIR}/<size>/np*_t*_run*.csv"
echo "============================================="
