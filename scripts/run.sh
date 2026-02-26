#!/bin/bash
# Run the simulation with configurable parameters via environment variables.
#
# Usage:
#   ./scripts/run.sh                        # defaults
#   NP=2 THREADS=4 WIDTH=64 ./scripts/run.sh
#
# Environment variables:
#   NP       — number of MPI processes  (default: 1)
#   THREADS  — OMP_NUM_THREADS          (default: 2)
#   WIDTH    — grid width               (default: 32)
#   HEIGHT   — grid height              (default: 32)
#   CYCLES   — simulation cycles        (default: 50)
#   AGENTS   — number of agents         (default: 20)
set -e

cd "$(dirname "$0")/.."

NP=${NP:-1}
THREADS=${THREADS:-2}
WIDTH=${WIDTH:-32}
HEIGHT=${HEIGHT:-32}
CYCLES=${CYCLES:-50}
AGENTS=${AGENTS:-20}

export OMP_NUM_THREADS=$THREADS

SIM_ARGS="-w $WIDTH -h $HEIGHT -c $CYCLES -a $AGENTS"

if [[ "$NP" -eq 1 ]]; then
    # Single rank: run directly so TUI output goes straight to terminal
    # (mpirun's stdout pipe buffers ANSI escape codes, breaking the TUI)
    echo "Running: ./sim $SIM_ARGS  (direct, OMP_NUM_THREADS=$THREADS)"
    ./sim -w "$WIDTH" -h "$HEIGHT" -c "$CYCLES" -a "$AGENTS"
else
    echo "Running: mpirun -np $NP ./sim $SIM_ARGS  (OMP_NUM_THREADS=$THREADS)"
    mpirun --oversubscribe -np "$NP" ./sim \
        -w "$WIDTH" -h "$HEIGHT" -c "$CYCLES" -a "$AGENTS" --no-tui
    echo "(TUI disabled for multi-rank — use NP=1 for interactive mode)"
fi
