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
    # Multi-rank: use file-based TUI to bypass mpirun's stdout pipe.
    # Rank 0 writes ANSI frames to a temp file; a shell viewer loop
    # reads and displays them independently.
    TUI_FILE="/tmp/ippd_tui_$$"
    trap 'rm -f "$TUI_FILE" "$TUI_FILE.tmp"' EXIT

    echo "Running: mpirun -np $NP ./sim $SIM_ARGS  (OMP_NUM_THREADS=$THREADS)"
    mpirun --oversubscribe -np "$NP" ./sim \
        -w "$WIDTH" -h "$HEIGHT" -c "$CYCLES" -a "$AGENTS" \
        --tui-file "$TUI_FILE" &
    SIM_PID=$!

    # Wait for first frame to be written
    sleep 0.5
    printf '\033[?25l'  # hide cursor

    # Viewer loop: read file, display with ANSI home cursor reset
    while kill -0 "$SIM_PID" 2>/dev/null; do
        if [[ -f "$TUI_FILE" ]]; then
            printf '\033[H'
            cat "$TUI_FILE"
            printf '\033[J'
        fi
        sleep 0.1
    done

    printf '\033[?25h'  # restore cursor
    wait "$SIM_PID"
fi
