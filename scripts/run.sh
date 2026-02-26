#!/bin/bash
# Run the simulation with configurable parameters via environment variables.
#
# Usage:
#   ./scripts/run.sh                        # defaults
#   NP=2 THREADS=4 WIDTH=64 ./scripts/run.sh
#
# Environment variables:
#   NP       — number of MPI processes  (default: 4)
#   THREADS  — OMP_NUM_THREADS          (default: 2)
#   WIDTH    — grid width               (default: 32)
#   HEIGHT   — grid height              (default: 32)
#   CYCLES   — simulation cycles        (default: 50)
#   AGENTS   — number of agents         (default: 20)
set -e

cd "$(dirname "$0")/.."

NP=${NP:-4}
THREADS=${THREADS:-2}
WIDTH=${WIDTH:-32}
HEIGHT=${HEIGHT:-32}
CYCLES=${CYCLES:-50}
AGENTS=${AGENTS:-20}

export OMP_NUM_THREADS=$THREADS

echo "Running: mpirun -np $NP ./sim -w $WIDTH -h $HEIGHT -c $CYCLES -a $AGENTS"
echo "OMP_NUM_THREADS=$THREADS"

mpirun --oversubscribe -np "$NP" ./sim \
    -w "$WIDTH" -h "$HEIGHT" -c "$CYCLES" -a "$AGENTS"
