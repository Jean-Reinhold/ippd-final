#!/bin/bash
# Analyze benchmark summary CSV: compute speedup, efficiency, and per-phase breakdown.
#
# Usage:
#   ./scripts/analyze.sh benchmark_results/<timestamp>/summary.csv
#
# Reads the summary CSV produced by benchmark.sh and prints:
#   1. Full results table with speedup and efficiency
#   2. MPI-only scaling (THREADS=1, varying NP)
#   3. OpenMP-only scaling (NP=1, varying THREADS)
#   4. Hybrid scaling (NP>1 and THREADS>1)
set -e

if [ -z "$1" ]; then
    # Try to find the most recent summary
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
echo " Parallelism Analysis"
echo " Source: $SUMMARY"
echo "========================================================================"

# ── Full table with speedup and efficiency ──────────────────────────
echo ""
echo "── All Configurations ─────────────────────────────────────────────────"
awk -F',' '
NR == 1 { next }
NR == 2 { baseline = $7 }
{
    np = $1; threads = $2
    compute = $3; halo = $4; migrate = $5; metrics = $6
    cycle = $7; wall = $8
    cores = np * threads
    speedup = (cycle > 0) ? baseline / cycle : 0
    efficiency = (cores > 0) ? speedup / cores * 100 : 0
    if (NR == 2) {
        printf "%-4s %-7s %-10s %-10s %-10s %-10s %-10s %-8s %-8s %-10s\n", \
            "NP", "Thr", "Comp(ms)", "Halo(ms)", "Migr(ms)", "Metr(ms)", \
            "Cycle(ms)", "Speedup", "Eff(%)", "Wall(s)"
        printf "%-4s %-7s %-10s %-10s %-10s %-10s %-10s %-8s %-8s %-10s\n", \
            "----", "-------", "----------", "----------", "----------", "----------", \
            "----------", "--------", "--------", "----------"
    }
    printf "%-4d %-7d %-10.3f %-10.3f %-10.3f %-10.3f %-10.3f %-8.2f %-8.1f %-10.3f\n", \
        np, threads, compute, halo, migrate, metrics, cycle, speedup, efficiency, wall
}' "$SUMMARY"

# ── MPI-only scaling (THREADS=1) ───────────────────────────────────
echo ""
echo "── MPI-Only Scaling (Threads=1) ─────────────────────────────────────"
awk -F',' '
NR == 1 { next }
$2 == 1 {
    if (!base) base = $7
    np = $1
    speedup = (base > 0 && $7 > 0) ? base / $7 : 0
    efficiency = (np > 0) ? speedup / np * 100 : 0
    if (first++ == 0) {
        printf "%-6s %-12s %-10s %-10s\n", "NP", "Cycle(ms)", "Speedup", "Eff(%)"
        printf "%-6s %-12s %-10s %-10s\n", "------", "------------", "----------", "----------"
    }
    printf "%-6d %-12.3f %-10.2f %-10.1f\n", np, $7, speedup, efficiency
}' "$SUMMARY"

# ── OpenMP-only scaling (NP=1) ─────────────────────────────────────
echo ""
echo "── OpenMP-Only Scaling (NP=1) ───────────────────────────────────────"
awk -F',' '
NR == 1 { next }
$1 == 1 {
    if (!base) base = $7
    threads = $2
    speedup = (base > 0 && $7 > 0) ? base / $7 : 0
    efficiency = (threads > 0) ? speedup / threads * 100 : 0
    if (first++ == 0) {
        printf "%-8s %-12s %-10s %-10s\n", "Threads", "Cycle(ms)", "Speedup", "Eff(%)"
        printf "%-8s %-12s %-10s %-10s\n", "--------", "------------", "----------", "----------"
    }
    printf "%-8d %-12.3f %-10.2f %-10.1f\n", threads, $7, speedup, efficiency
}' "$SUMMARY"

# ── Hybrid scaling (NP>1 and THREADS>1) ────────────────────────────
echo ""
echo "── Hybrid Scaling (NP>1, Threads>1) ─────────────────────────────────"
awk -F',' '
NR == 1 { next }
NR == 2 { baseline = $7 }
$1 > 1 && $2 > 1 {
    np = $1; threads = $2; cores = np * threads
    speedup = (baseline > 0 && $7 > 0) ? baseline / $7 : 0
    efficiency = (cores > 0) ? speedup / cores * 100 : 0
    if (first++ == 0) {
        printf "%-4s %-7s %-7s %-12s %-10s %-10s\n", \
            "NP", "Thr", "Cores", "Cycle(ms)", "Speedup", "Eff(%)"
        printf "%-4s %-7s %-7s %-12s %-10s %-10s\n", \
            "----", "-------", "-------", "------------", "----------", "----------"
    }
    printf "%-4d %-7d %-7d %-12.3f %-10.2f %-10.1f\n", \
        np, threads, cores, $7, speedup, efficiency
}' "$SUMMARY"

# ── Communication overhead analysis ────────────────────────────────
echo ""
echo "── Communication Overhead ───────────────────────────────────────────"
awk -F',' '
NR == 1 { next }
{
    np = $1; threads = $2
    compute = $3; halo = $4; migrate = $5; metrics = $6; cycle = $7
    comm = halo + migrate + metrics
    pct = (cycle > 0) ? comm / cycle * 100 : 0
    if (first++ == 0) {
        printf "%-4s %-7s %-12s %-12s %-12s\n", \
            "NP", "Thr", "Compute(ms)", "Comm(ms)", "Comm(%)"
        printf "%-4s %-7s %-12s %-12s %-12s\n", \
            "----", "-------", "------------", "------------", "------------"
    }
    printf "%-4d %-7d %-12.3f %-12.3f %-12.1f\n", \
        np, threads, compute, comm, pct
}' "$SUMMARY"

echo ""
echo "========================================================================"
echo " Analysis complete"
echo "========================================================================"
