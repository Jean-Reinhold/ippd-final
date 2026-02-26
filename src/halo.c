#ifdef USE_MPI

#include "halo.h"
#include "types.h"
#include <mpi.h>
#include <stdlib.h>
#include <stddef.h>

/* ────────────────────────────────────────────────────────────────────────── *
 * MPI Datatype for Cell                                                     *
 * ────────────────────────────────────────────────────────────────────────── */

MPI_Datatype halo_cell_type(void)
{
    const int nfields = 4;
    int          block_lengths[4] = {1, 1, 1, 1};
    MPI_Datatype field_types[4]   = {MPI_INT, MPI_DOUBLE, MPI_DOUBLE, MPI_INT};
    MPI_Aint     offsets[4];

    offsets[0] = offsetof(Cell, type);
    offsets[1] = offsetof(Cell, resource);
    offsets[2] = offsetof(Cell, max_resource);
    offsets[3] = offsetof(Cell, accessible);

    MPI_Datatype dt;
    MPI_Type_create_struct(nfields, block_lengths, offsets, field_types, &dt);
    MPI_Type_commit(&dt);
    return dt;
}

/* ────────────────────────────────────────────────────────────────────────── *
 * Helper: pack / unpack a column from the halo-padded grid                  *
 * ────────────────────────────────────────────────────────────────────────── */

/* Pack column `col` for rows [1 .. local_h] into `buf` (local_h cells). */
static void pack_column(const SubGrid *sg, int col, Cell *buf)
{
    for (int r = 1; r <= sg->local_h; r++)
        buf[r - 1] = sg->cells[CELL_AT(sg, r, col)];
}

/* Unpack `buf` (local_h cells) into column `col` for rows [1 .. local_h]. */
static void unpack_column(SubGrid *sg, int col, const Cell *buf)
{
    for (int r = 1; r <= sg->local_h; r++)
        sg->cells[CELL_AT(sg, r, col)] = buf[r - 1];
}

/* ────────────────────────────────────────────────────────────────────────── *
 * Halo exchange — non-blocking                                              *
 * ────────────────────────────────────────────────────────────────────────── *
 * We post all Isend/Irecv pairs for 4 cardinal + 4 diagonal directions,     *
 * then wait for completion in one MPI_Waitall call.                         *
 *                                                                           *
 * Interior: rows [1..local_h], cols [1..local_w]                            *
 * North halo row = 0           South halo row = local_h + 1                 *
 * West  halo col = 0           East  halo col = local_w + 1                 *
 * ────────────────────────────────────────────────────────────────────────── */

void halo_exchange(SubGrid *sg, Partition *p)
{
    MPI_Datatype cell_t = halo_cell_type();

    const int local_w = sg->local_w;
    const int local_h = sg->local_h;
    const int halo_w  = sg->halo_w;

    /* Temporary buffers for column packing (E/W) */
    Cell *send_west = malloc(sizeof(Cell) * local_h);
    Cell *send_east = malloc(sizeof(Cell) * local_h);
    Cell *recv_west = malloc(sizeof(Cell) * local_h);
    Cell *recv_east = malloc(sizeof(Cell) * local_h);

    /* We need at most 16 requests: 8 sends + 8 recvs */
    MPI_Request reqs[16];
    int nreq = 0;

    /* ── North / South ─────────────────────────────────────────────────── */

    /* Send my north border (row 1) to north neighbor,
       recv into my north halo (row 0) from north neighbor. */
    MPI_Isend(&sg->cells[CELL_AT(sg, 1, 1)],       local_w, cell_t,
              p->neighbors[DIR_N], TAG_SOUTH, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, 0, 1)],       local_w, cell_t,
              p->neighbors[DIR_N], TAG_NORTH, p->cart_comm, &reqs[nreq++]);

    /* Send my south border (row local_h) to south neighbor,
       recv into my south halo (row local_h+1) from south neighbor. */
    MPI_Isend(&sg->cells[CELL_AT(sg, local_h, 1)],     local_w, cell_t,
              p->neighbors[DIR_S], TAG_NORTH, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, local_h + 1, 1)], local_w, cell_t,
              p->neighbors[DIR_S], TAG_SOUTH, p->cart_comm, &reqs[nreq++]);

    /* ── East / West (packed columns) ─────────────────────────────────── */

    pack_column(sg, 1, send_west);          /* west border  = col 1       */
    pack_column(sg, local_w, send_east);    /* east border  = col local_w */

    MPI_Isend(send_west, local_h, cell_t,
              p->neighbors[DIR_W], TAG_EAST, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(recv_west, local_h, cell_t,
              p->neighbors[DIR_W], TAG_WEST, p->cart_comm, &reqs[nreq++]);

    MPI_Isend(send_east, local_h, cell_t,
              p->neighbors[DIR_E], TAG_WEST, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(recv_east, local_h, cell_t,
              p->neighbors[DIR_E], TAG_EAST, p->cart_comm, &reqs[nreq++]);

    /* ── Diagonals (single corner cells) ──────────────────────────────── */

    /* NW: send my [1][1], recv into [0][0] */
    MPI_Isend(&sg->cells[CELL_AT(sg, 1, 1)],                  1, cell_t,
              p->neighbors[DIR_NW], TAG_SE, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, 0, 0)],                  1, cell_t,
              p->neighbors[DIR_NW], TAG_NW, p->cart_comm, &reqs[nreq++]);

    /* NE: send my [1][local_w], recv into [0][halo_w-1] */
    MPI_Isend(&sg->cells[CELL_AT(sg, 1, local_w)],            1, cell_t,
              p->neighbors[DIR_NE], TAG_SW, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, 0, halo_w - 1)],        1, cell_t,
              p->neighbors[DIR_NE], TAG_NE, p->cart_comm, &reqs[nreq++]);

    /* SW: send my [local_h][1], recv into [halo_h-1][0] */
    MPI_Isend(&sg->cells[CELL_AT(sg, local_h, 1)],            1, cell_t,
              p->neighbors[DIR_SW], TAG_NE, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, sg->halo_h - 1, 0)],    1, cell_t,
              p->neighbors[DIR_SW], TAG_SW, p->cart_comm, &reqs[nreq++]);

    /* SE: send my [local_h][local_w], recv into [halo_h-1][halo_w-1] */
    MPI_Isend(&sg->cells[CELL_AT(sg, local_h, local_w)],      1, cell_t,
              p->neighbors[DIR_SE], TAG_NW, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, sg->halo_h - 1, halo_w - 1)], 1, cell_t,
              p->neighbors[DIR_SE], TAG_SE, p->cart_comm, &reqs[nreq++]);

    /* ── Wait for all ─────────────────────────────────────────────────── */
    MPI_Waitall(nreq, reqs, MPI_STATUSES_IGNORE);

    /* Unpack received columns into halo columns */
    unpack_column(sg, 0,           recv_west);   /* west halo  = col 0           */
    unpack_column(sg, local_w + 1, recv_east);   /* east halo  = col local_w + 1 */

    /* Cleanup */
    free(send_west);
    free(send_east);
    free(recv_west);
    free(recv_east);
    MPI_Type_free(&cell_t);
}

#endif /* USE_MPI */
