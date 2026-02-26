#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

#ifdef USE_MPI
#include <mpi.h>
#endif

/* ── Cell types representing Amazonian land-use categories ── */
typedef enum {
    ALDEIA      = 0,  /* Village — always accessible, no regeneration */
    PESCA       = 1,  /* Fishing — accessible in dry season */
    COLETA      = 2,  /* Gathering — always accessible */
    ROCADO      = 3,  /* Slash-and-burn farming — accessible in wet season */
    INTERDITADA = 4   /* Forbidden — never accessible */
} CellType;

typedef enum {
    DRY = 0,
    WET = 1
} Season;

/* ── Grid cell ── */
typedef struct {
    CellType type;
    double   resource;
    double   max_resource;
    int      accessible;
} Cell;

/* ── Mobile agent ── */
typedef struct {
    int    id;
    int    gx;       /* global x coordinate */
    int    gy;       /* global y coordinate */
    double energy;
    int    alive;
} Agent;

/*
 * SubGrid — the local partition each MPI rank owns.
 * The cells array is a flat buffer with 1-cell halo padding on every side,
 * so its dimensions are (local_h + 2) * (local_w + 2).
 */
typedef struct {
    int   local_w;
    int   local_h;
    int   offset_x;   /* global x origin of this partition */
    int   offset_y;   /* global y origin of this partition */
    int   halo_w;     /* = local_w + 2 */
    int   halo_h;     /* = local_h + 2 */
    Cell *cells;      /* flat array of size halo_h * halo_w */
} SubGrid;

/* ── Simulation configuration ── */
typedef struct {
    int      global_w;
    int      global_h;
    int      total_cycles;
    int      season_length;
    int      num_agents;
    int      max_workload;
    double   consumption_rate;
    double   energy_gain;
    double   energy_loss;
    double   initial_energy;
    uint64_t seed;
    int      tui_enabled;
    int      tui_interval;
} SimConfig;

/* ── MPI Cartesian partition metadata ── */
typedef struct {
    int px;          /* number of columns in the process grid */
    int py;          /* number of rows in the process grid */
    int my_row;
    int my_col;
    int rank;
    int size;
    int neighbors[8]; /* N, S, E, W, NE, NW, SE, SW */
#ifdef USE_MPI
    MPI_Comm cart_comm;
#else
    int cart_comm;   /* placeholder when MPI is absent */
#endif
} Partition;

/*
 * CELL_AT — index into the halo-padded flat array.
 * Row r and column c are in halo coordinates (0 = top/left halo row/col,
 * so the interior starts at (1,1)).
 */
#define CELL_AT(sg, r, c) ((r) * (sg)->halo_w + (c))

#endif /* TYPES_H */
