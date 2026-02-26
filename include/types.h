#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

#ifdef USE_MPI
#include <mpi.h>
#endif

typedef enum {
    ALDEIA      = 0,  /* Aldeia — sempre acessível, sem regeneração */
    PESCA       = 1,  /* Pesca — acessível na seca */
    COLETA      = 2,  /* Coleta — sempre acessível */
    ROCADO      = 3,  /* Roçado — acessível na chuva */
    INTERDITADA = 4   /* Interditada — nunca acessível */
} CellType;

typedef enum {
    DRY = 0,
    WET = 1
} Season;

typedef struct {
    CellType type;
    double   resource;
    double   max_resource;
    int      accessible;
} Cell;

typedef struct {
    int    id;
    int    gx;       /* coordenada global x */
    int    gy;       /* coordenada global y */
    double energy;
    int    alive;
} Agent;

/*
 * SubGrid — partição local de cada rank MPI.
 * O array cells é um buffer plano com 1 célula de halo em cada lado,
 * portanto suas dimensões são (local_h + 2) * (local_w + 2).
 */
typedef struct {
    int   local_w;
    int   local_h;
    int   offset_x;   /* origem global x desta partição */
    int   offset_y;   /* origem global y desta partição */
    int   halo_w;     /* = local_w + 2 */
    int   halo_h;     /* = local_h + 2 */
    Cell *cells;      /* array plano de tamanho halo_h * halo_w */
} SubGrid;

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
    int      csv_output;
    char     tui_file[256];
} SimConfig;

typedef struct {
    int px;          /* colunas na grade de processos */
    int py;          /* linhas na grade de processos */
    int my_row;
    int my_col;
    int rank;
    int size;
    int neighbors[8]; /* N, S, E, W, NE, NW, SE, SW */
#ifdef USE_MPI
    MPI_Comm cart_comm;
#else
    int cart_comm;   /* placeholder quando MPI está ausente */
#endif
} Partition;

/*
 * CELL_AT — índice no array plano com halo.
 * r e c estão em coordenadas de halo (0 = linha/coluna de halo,
 * interior começa em (1,1)).
 */
#define CELL_AT(sg, r, c) ((r) * (sg)->halo_w + (c))

#endif /* TYPES_H */
