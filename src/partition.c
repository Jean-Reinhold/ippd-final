#include "partition.h"

#include <math.h>
#include <stdlib.h>

#ifdef USE_MPI
#include <mpi.h>
#endif

/* ------------------------------------------------------------------ */

#ifdef USE_MPI
void partition_init(Partition *p, int global_w, int global_h,
                    MPI_Comm comm) {
    MPI_Comm_size(comm, &p->size);
    MPI_Comm_rank(comm, &p->rank);
#else
void partition_init(Partition *p, int global_w, int global_h,
                    int comm) {
    (void)comm;
    p->size = 1;
    p->rank = 0;
#endif

    /* ── Factor size into px * py, minimising |px - py| ── */
    int best_px = 1, best_py = p->size;
    for (int i = 1; i * i <= p->size; i++) {
        if (p->size % i == 0) {
            int a = i;           /* smaller factor  */
            int b = p->size / i; /* larger factor   */
            if (abs(a - b) < abs(best_px - best_py)) {
                best_px = a;
                best_py = b;
            }
        }
    }

    /*
     * px = columns in process grid, py = rows.
     * Assign the larger factor to the dimension with more cells so that
     * sub-grids stay roughly square.
     */
    if (global_w >= global_h) {
        p->px = best_py;   /* more columns for wider grids */
        p->py = best_px;
    } else {
        p->px = best_px;
        p->py = best_py;
    }

#ifdef USE_MPI
    /* dims[0] = rows (py), dims[1] = columns (px) */
    int dims[2]    = { p->py, p->px };
    int periods[2] = { 0, 0 };
    MPI_Cart_create(comm, 2, dims, periods, 1, &p->cart_comm);

    /* Rank may have been remapped by the Cartesian constructor. */
    MPI_Comm_rank(p->cart_comm, &p->rank);

    int coords[2];
    MPI_Cart_coords(p->cart_comm, p->rank, 2, coords);
    p->my_row = coords[0];
    p->my_col = coords[1];

    /* ── Cardinal neighbours via MPI_Cart_shift ── */
    int north, south, west, east;
    MPI_Cart_shift(p->cart_comm, 0, 1, &north, &south);   /* dim 0 = rows */
    MPI_Cart_shift(p->cart_comm, 1, 1, &west,  &east);    /* dim 1 = cols */
    p->neighbors[0] = north;   /* N  */
    p->neighbors[1] = south;   /* S  */
    p->neighbors[2] = east;    /* E  */
    p->neighbors[3] = west;    /* W  */

    /* ── Diagonal neighbours via MPI_Cart_rank ── */
    int dc[2];

    /* NE: (row-1, col+1) */
    dc[0] = p->my_row - 1;  dc[1] = p->my_col + 1;
    if (dc[0] >= 0 && dc[1] < p->px)
        MPI_Cart_rank(p->cart_comm, dc, &p->neighbors[4]);
    else
        p->neighbors[4] = MPI_PROC_NULL;

    /* NW: (row-1, col-1) */
    dc[0] = p->my_row - 1;  dc[1] = p->my_col - 1;
    if (dc[0] >= 0 && dc[1] >= 0)
        MPI_Cart_rank(p->cart_comm, dc, &p->neighbors[5]);
    else
        p->neighbors[5] = MPI_PROC_NULL;

    /* SE: (row+1, col+1) */
    dc[0] = p->my_row + 1;  dc[1] = p->my_col + 1;
    if (dc[0] < p->py && dc[1] < p->px)
        MPI_Cart_rank(p->cart_comm, dc, &p->neighbors[6]);
    else
        p->neighbors[6] = MPI_PROC_NULL;

    /* SW: (row+1, col-1) */
    dc[0] = p->my_row + 1;  dc[1] = p->my_col - 1;
    if (dc[0] < p->py && dc[1] >= 0)
        MPI_Cart_rank(p->cart_comm, dc, &p->neighbors[7]);
    else
        p->neighbors[7] = MPI_PROC_NULL;

#else
    /* Single-process fallback — no neighbours. */
    p->my_row    = 0;
    p->my_col    = 0;
    p->px        = 1;
    p->py        = 1;
    p->cart_comm = 0;
    for (int i = 0; i < 8; i++)
        p->neighbors[i] = -1;   /* equivalent to MPI_PROC_NULL */
#endif
}

/* ------------------------------------------------------------------ */

void partition_subgrid_dims(const Partition *p, int global_w, int global_h,
                            int *local_w, int *local_h,
                            int *offset_x, int *offset_y) {
    int base_w = global_w / p->px;
    int rem_w  = global_w % p->px;
    int base_h = global_h / p->py;
    int rem_h  = global_h % p->py;

    /* Last column absorbs extra width, last row absorbs extra height. */
    *local_w  = (p->my_col == p->px - 1) ? base_w + rem_w : base_w;
    *local_h  = (p->my_row == p->py - 1) ? base_h + rem_h : base_h;
    *offset_x = p->my_col * base_w;
    *offset_y = p->my_row * base_h;
}

/* ------------------------------------------------------------------ */

int partition_rank_for_global(const Partition *p, int gx, int gy,
                              int global_w, int global_h) {
    int base_w = global_w / p->px;
    int base_h = global_h / p->py;

    int col = (base_w > 0) ? gx / base_w : 0;
    int row = (base_h > 0) ? gy / base_h : 0;
    if (col >= p->px) col = p->px - 1;
    if (row >= p->py) row = p->py - 1;

#ifdef USE_MPI
    int coords[2] = { row, col };
    int target_rank;
    MPI_Cart_rank(p->cart_comm, coords, &target_rank);
    return target_rank;
#else
    (void)row; (void)col;
    return 0;
#endif
}

/* ------------------------------------------------------------------ */

int partition_owns_global(const Partition *p, const SubGrid *sg,
                          int gx, int gy) {
    (void)p;
    return (gx >= sg->offset_x && gx < sg->offset_x + sg->local_w &&
            gy >= sg->offset_y && gy < sg->offset_y + sg->local_h);
}

/* ------------------------------------------------------------------ */

void partition_destroy(Partition *p) {
#ifdef USE_MPI
    if (p->cart_comm != MPI_COMM_NULL)
        MPI_Comm_free(&p->cart_comm);
#else
    (void)p;
#endif
}
