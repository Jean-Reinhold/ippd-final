#!/bin/bash
# Analyze benchmark summary CSV: per-phase speedup, Karp-Flatt, Amdahl, Gustafson.
#
# Usage:
#   ./scripts/analyze.sh [summary.csv]
#
# Reads the summary CSV produced by benchmark.sh and prints:
#   1. All Configurations — full table with per-phase means ± stddev, speedup, efficiency
#   2. Per-Phase Speedup Tables (MPI-only, OMP-only, hybrid)
#   3. Karp-Flatt Serial Fraction
#   4. Amdahl Predicted vs Actual
#   5. Gustafson Scaled Speedup (across problem sizes)
#   6. Communication Overhead Decomposition
set -e

if [ -z "$1" ]; then
    LATEST=$(ls -td benchmark_results/*/summary.csv 2>/dev/null | head -1)
    if [ -z "$LATEST" ]; then
        echo "Usage: $0 <summary.csv>"
        echo "No benchmark_results found. Run scripts/benchmark.sh first."
        exit 1
    fi
    SUMMARY="$LATEST"
    echo "Using most recent: $SUMMARY"
else
    SUMMARY="$1"
fi

if [ ! -f "$SUMMARY" ]; then
    echo "Error: $SUMMARY not found"
    exit 1
fi

echo ""
echo "========================================================================"
echo " Parallelism Analysis (Extended)"
echo " Source: $SUMMARY"
echo "========================================================================"

# ── 1. All Configurations ─────────────────────────────────────────────
echo ""
echo "── 1. All Configurations ────────────────────────────────────────────────"
echo ""
awk -F',' '
NR == 1 { next }
{
    size = $1; np = $2; threads = $3
    m_season = $4;   s_season = $5
    m_halo = $6;     s_halo = $7
    m_work = $8;     s_work = $9
    m_agent = $10;   s_agent = $11
    m_grid = $12;    s_grid = $13
    m_migrate = $14; s_migrate = $15
    m_metrics = $16; s_metrics = $17
    m_cycle = $18;   s_cycle = $19
    m_wpct = $20;    m_cpct = $21
    wall = $22
    cores = np * threads

    # Find baseline for this size (NP=1, Threads=1)
    key = size
    if (!(key in base)) base[key] = m_cycle
    speedup = (m_cycle > 0) ? base[key] / m_cycle : 0
    eff = (cores > 0) ? speedup / cores * 100 : 0

    if (!header_done) {
        printf "%-10s %-3s %-3s %-12s %-10s %-10s %-10s %-10s %-10s %-10s %-12s %-7s %-7s %-6s %-6s\n", \
            "Size", "NP", "Thr", "Cycle(ms)", "Season", "Halo", "Workload", "Agent", "Grid", "Migrate", "Metrics", "Spdup", "Eff%", "Wl%", "Cm%"
        printf "%-10s %-3s %-3s %-12s %-10s %-10s %-10s %-10s %-10s %-10s %-12s %-7s %-7s %-6s %-6s\n", \
            "----------","---","---","------------","----------","----------","----------","----------","----------","----------","------------","-------","-------","------","------"
        header_done = 1
    }
    printf "%-10s %-3d %-3d %6.1f±%-5.1f %5.2f±%-4.2f %5.2f±%-4.2f %6.1f±%-4.1f %5.2f±%-4.2f %5.2f±%-4.2f %5.2f±%-4.2f %6.2f±%-5.2f %-7.2f %-7.1f %-6.1f %-6.1f\n", \
        size, np, threads, m_cycle, s_cycle, m_season, s_season, m_halo, s_halo, \
        m_work, s_work, m_agent, s_agent, m_grid, s_grid, m_migrate, s_migrate, \
        m_metrics, s_metrics, speedup, eff, m_wpct, m_cpct
}' "$SUMMARY"

# ── 2. Per-Phase Speedup Tables ──────────────────────────────────────
echo ""
echo "── 2. Per-Phase Speedup ─────────────────────────────────────────────────"

echo ""
echo "  2a. MPI-Only Scaling (Threads=1)"
echo ""
awk -F',' '
NR == 1 { next }
$3 == 1 {
    size = $1; np = $2
    key = size
    if (!(key in b_season))   { b_season[key]=$4; b_halo[key]=$6; b_work[key]=$8; b_agent[key]=$10; b_grid[key]=$12; b_migrate[key]=$14; b_cycle[key]=$18 }
    if (!header) {
        printf "%-10s %-3s %-10s %-8s %-8s %-8s %-8s %-8s %-8s %-8s\n", \
            "Size","NP","Cycle(ms)","S_cycle","S_work","S_agent","S_grid","S_halo","S_migr","Eff%"
        printf "%-10s %-3s %-10s %-8s %-8s %-8s %-8s %-8s %-8s %-8s\n", \
            "----------","---","----------","--------","--------","--------","--------","--------","--------","--------"
        header=1
    }
    s_cy = ($18>0) ? b_cycle[key]/$18 : 0
    s_wk = ($8>0)  ? b_work[key]/$8   : 0
    s_ag = ($10>0) ? b_agent[key]/$10  : 0
    s_gr = ($12>0) ? b_grid[key]/$12   : 0
    s_hl = ($6>0)  ? b_halo[key]/$6    : 0
    s_mg = ($14>0) ? b_migrate[key]/$14 : 0
    eff = (np>0) ? s_cy/np*100 : 0
    printf "%-10s %-3d %-10.2f %-8.2f %-8.2f %-8.2f %-8.2f %-8.2f %-8.2f %-8.1f\n", \
        size, np, $18, s_cy, s_wk, s_ag, s_gr, s_hl, s_mg, eff
}' "$SUMMARY"

echo ""
echo "  2b. OpenMP-Only Scaling (NP=1)"
echo ""
awk -F',' '
NR == 1 { next }
$2 == 1 {
    size = $1; thr = $3
    key = size
    if (!(key in b_work)) { b_work[key]=$8; b_agent[key]=$10; b_grid[key]=$12; b_cycle[key]=$18 }
    if (!header) {
        printf "%-10s %-3s %-10s %-8s %-8s %-8s %-8s %-8s\n", \
            "Size","Thr","Cycle(ms)","S_cycle","S_work","S_agent","S_grid","Eff%"
        printf "%-10s %-3s %-10s %-8s %-8s %-8s %-8s %-8s\n", \
            "----------","---","----------","--------","--------","--------","--------","--------"
        header=1
    }
    s_cy = ($18>0) ? b_cycle[key]/$18 : 0
    s_wk = ($8>0)  ? b_work[key]/$8   : 0
    s_ag = ($10>0) ? b_agent[key]/$10  : 0
    s_gr = ($12>0) ? b_grid[key]/$12   : 0
    eff = (thr>0) ? s_cy/thr*100 : 0
    printf "%-10s %-3d %-10.2f %-8.2f %-8.2f %-8.2f %-8.2f %-8.1f\n", \
        size, thr, $18, s_cy, s_wk, s_ag, s_gr, eff
}' "$SUMMARY"

echo ""
echo "  2c. Hybrid Scaling (NP>1 and Threads>1)"
echo ""
awk -F',' '
NR == 1 { next }
{
    size = $1; key = size
    if ($2==1 && $3==1) { b_cycle[key]=$18; b_work[key]=$8 }
}
$2 > 1 && $3 > 1 {
    size=$1; np=$2; thr=$3; cores=np*thr
    key=size
    s_cy = ($18>0 && (key in b_cycle)) ? b_cycle[key]/$18 : 0
    s_wk = ($8>0  && (key in b_work))  ? b_work[key]/$8   : 0
    eff = (cores>0) ? s_cy/cores*100 : 0
    if (!header) {
        printf "%-10s %-3s %-3s %-5s %-10s %-8s %-8s %-8s\n", \
            "Size","NP","Thr","Cors","Cycle(ms)","S_cycle","S_work","Eff%"
        printf "%-10s %-3s %-3s %-5s %-10s %-8s %-8s %-8s\n", \
            "----------","---","---","-----","----------","--------","--------","--------"
        header=1
    }
    printf "%-10s %-3d %-3d %-5d %-10.2f %-8.2f %-8.2f %-8.1f\n", \
        size, np, thr, cores, $18, s_cy, s_wk, eff
}' "$SUMMARY"

# ── 3. Karp-Flatt Serial Fraction ───────────────────────────────────
echo ""
echo "── 3. Karp-Flatt Serial Fraction ────────────────────────────────────────"
echo ""
echo "  e = (1/S - 1/P) / (1 - 1/P)  where S=speedup, P=cores"
echo "  Increasing e → growing overhead, not just fixed serial fraction."
echo ""
awk -F',' '
NR == 1 { next }
{
    size=$1; np=$2; thr=$3; key=size
    if (np==1 && thr==1) { base[key]=$18; next }
}
{
    size=$1; np=$2; thr=$3; key=size; cores=np*thr
    if (cores <= 1) next
    if (!(key in base)) next
    S = base[key] / $18
    if (S <= 0) next
    e = (1.0/S - 1.0/cores) / (1.0 - 1.0/cores)
    if (!header) {
        printf "%-10s %-3s %-3s %-5s %-8s %-10s\n", \
            "Size","NP","Thr","Cors","Speedup","e (serial)"
        printf "%-10s %-3s %-3s %-5s %-8s %-10s\n", \
            "----------","---","---","-----","--------","----------"
        header=1
    }
    printf "%-10s %-3d %-3d %-5d %-8.2f %-10.4f\n", \
        size, np, thr, cores, S, e
}' "$SUMMARY"

# ── 4. Amdahl Predicted vs Actual ───────────────────────────────────
echo ""
echo "── 4. Amdahl Predicted vs Actual ────────────────────────────────────────"
echo ""
echo "  Serial fraction estimated from baseline: s = (season + metrics) / cycle"
echo "  Amdahl: S_pred = 1 / (s + (1-s)/P)"
echo ""
awk -F',' '
NR == 1 { next }
{
    size=$1; np=$2; thr=$3; key=size
    if (np==1 && thr==1) {
        base_cycle[key]=$18
        serial_frac[key] = ($18 > 0) ? ($4 + $16) / $18 : 0
        next
    }
}
{
    size=$1; np=$2; thr=$3; key=size; cores=np*thr
    if (cores <= 1) next
    if (!(key in base_cycle)) next
    s = serial_frac[key]
    S_actual = base_cycle[key] / $18
    S_amdahl = 1.0 / (s + (1.0 - s) / cores)
    if (!header) {
        printf "%-10s %-3s %-3s %-5s %-6s %-10s %-10s %-10s\n", \
            "Size","NP","Thr","Cors","s(%)","S_actual","S_amdahl","Ratio"
        printf "%-10s %-3s %-3s %-5s %-6s %-10s %-10s %-10s\n", \
            "----------","---","---","-----","------","----------","----------","----------"
        header=1
    }
    ratio = (S_amdahl > 0) ? S_actual / S_amdahl : 0
    printf "%-10s %-3d %-3d %-5d %-6.2f %-10.2f %-10.2f %-10.2f\n", \
        size, np, thr, cores, s*100, S_actual, S_amdahl, ratio
}' "$SUMMARY"

# ── 5. Gustafson Scaled Speedup (across problem sizes) ──────────────
echo ""
echo "── 5. Gustafson Scaled Speedup (across problem sizes) ──────────────────"
echo ""
echo "  For a fixed core count, show how execution time scales with problem size."
echo "  Larger problems → higher efficiency (Gustafson law)."
echo ""
awk -F',' '
NR == 1 { next }
{
    size=$1; np=$2; thr=$3
    key = np "x" thr
    dkey = size "|" key
    data[dkey] = $18
    if (np==1 && thr==1) base[size] = $18
    sizes[size] = 1
    configs[key] = 1
}
END {
    # Collect unique sizes and configs into indexed arrays for ordered iteration
    ns = 0; for (s in sizes) slist[ns++] = s
    nc = 0; for (k in configs) clist[nc++] = k

    # Print header
    printf "%-12s", "Config"
    for (si = 0; si < ns; si++) printf " %-18s", slist[si]
    printf "\n"
    printf "%-12s", "------------"
    for (si = 0; si < ns; si++) printf " %-18s", "------------------"
    printf "\n"

    for (ci = 0; ci < nc; ci++) {
        k = clist[ci]
        printf "%-12s", k
        for (si = 0; si < ns; si++) {
            s = slist[si]
            dkey = s "|" k
            if ((dkey in data) && (s in base) && base[s] > 0) {
                cy = data[dkey]
                sp = base[s] / cy
                eff = 0
                split(k, parts, "x")
                cores = parts[1] * parts[2]
                if (cores > 0) eff = sp / cores * 100
                printf " %6.1fms %4.1fx %3.0f%%", cy, sp, eff
            } else {
                printf " %-18s", "N/A"
            }
        }
        printf "\n"
    }
}' "$SUMMARY"

# ── 6. Communication Overhead Decomposition ──────────────────────────
echo ""
echo "── 6. Communication Overhead Decomposition ─────────────────────────────"
echo ""
awk -F',' '
NR == 1 { next }
{
    size=$1; np=$2; thr=$3
    m_season=$4; m_halo=$6; m_migrate=$14; m_cycle=$18
    total_comm = m_season + m_halo + m_migrate
    season_pct = (m_cycle > 0) ? m_season / m_cycle * 100 : 0
    halo_pct   = (m_cycle > 0) ? m_halo / m_cycle * 100 : 0
    migrate_pct= (m_cycle > 0) ? m_migrate / m_cycle * 100 : 0
    comm_pct   = (m_cycle > 0) ? total_comm / m_cycle * 100 : 0
    if (!header) {
        printf "%-10s %-3s %-3s %-10s %-8s %-8s %-8s %-10s\n", \
            "Size","NP","Thr","Cycle(ms)","Ssn(%)","Halo(%)","Migr(%)","Comm(%)"
        printf "%-10s %-3s %-3s %-10s %-8s %-8s %-8s %-10s\n", \
            "----------","---","---","----------","--------","--------","--------","----------"
        header=1
    }
    printf "%-10s %-3d %-3d %-10.2f %-8.2f %-8.2f %-8.2f %-10.2f\n", \
        size, np, thr, m_cycle, season_pct, halo_pct, migrate_pct, comm_pct
}' "$SUMMARY"

echo ""
echo "========================================================================"
echo " Analysis complete"
echo "========================================================================"
