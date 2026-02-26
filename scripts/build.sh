#!/bin/bash
# Build the simulation from scratch
set -e

cd "$(dirname "$0")/.."

echo "=== Cleaning ==="
make clean

echo "=== Building ==="
make all

echo "=== Build complete ==="
