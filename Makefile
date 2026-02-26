# ── Compiler setup ──────────────────────────────────────────────
# On macOS with Homebrew, gcc-15 is required for OpenMP support.
# OMPI_CC tells mpicc to use gcc-15 instead of the default (clang).
export OMPI_CC = gcc-15

CC       = mpicc
CFLAGS   = -std=c11 -Wall -Wextra -O2 -fopenmp -Iinclude -DUSE_MPI
LDFLAGS  = -fopenmp -lm

# ── Sources / objects ───────────────────────────────────────────
SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)

# ── Main target ─────────────────────────────────────────────────
.PHONY: all clean test test-unit test-mpi

all: sim

sim: $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

# ── Unit tests (no MPI, compiled with gcc-15 directly) ─────────
# These test pure-logic modules that don't depend on MPI.
UNIT_TEST_SRC  = tests/test_main.c \
                 $(wildcard tests/test_*.c)
# Filter out MPI test files
UNIT_TEST_SRC := $(filter-out tests/test_mpi_%.c, $(UNIT_TEST_SRC))

# Source files needed by unit tests (no MPI-dependent modules)
UNIT_SRC = src/rng.c src/season.c src/workload.c src/grid.c src/agent.c

test-unit: $(UNIT_TEST_SRC) $(UNIT_SRC) | build
	gcc-15 -std=c11 -Wall -Wextra -O2 -Iinclude -fopenmp \
		$(UNIT_TEST_SRC) $(UNIT_SRC) \
		-o test_unit -lm -fopenmp
	./test_unit

# ── MPI integration tests ──────────────────────────────────────
MPI_TEST_SRC = $(wildcard tests/test_mpi_*.c)
MPI_TEST_BIN = $(MPI_TEST_SRC:tests/%.c=%)

# All non-main source files for MPI tests
MPI_SRC = $(filter-out src/main.c, $(wildcard src/*.c))

test-mpi: $(MPI_TEST_BIN)
	@for bin in $(MPI_TEST_BIN); do \
		echo "=== Running $$bin with 4 ranks ==="; \
		mpirun --oversubscribe -np 4 ./$$bin || exit 1; \
	done

$(MPI_TEST_BIN): %: tests/%.c $(MPI_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── Combined test target ───────────────────────────────────────
test: test-unit test-mpi

# ── Cleanup ─────────────────────────────────────────────────────
clean:
	rm -rf build/ sim test_unit test_mpi_*
