#ifdef USE_MPI

#include "halo.h"
#include "types.h"
#include <mpi.h>
#include <stdlib.h>
#include <stddef.h>

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

static void pack_column(const SubGrid *sg, int col, Cell *buf)
{
    for (int r = 1; r <= sg->local_h; r++)
        buf[r - 1] = sg->cells[CELL_AT(sg, r, col)];
}

static void unpack_column(SubGrid *sg, int col, const Cell *buf)
{
    for (int r = 1; r <= sg->local_h; r++)
        sg->cells[CELL_AT(sg, r, col)] = buf[r - 1];
}

/*
 * Troca de halos não-bloqueante: posta pares Isend/Irecv para as
 * 4 direções cardinais + 4 diagonais, e espera com MPI_Waitall.
 *
 * Interior: linhas [1..local_h], colunas [1..local_w].
 * Halo norte = linha 0,  halo sul = linha local_h+1.
 * Halo oeste = coluna 0, halo leste = coluna local_w+1.
 */

void halo_exchange(SubGrid *sg, Partition *p)
{
    MPI_Datatype cell_t = halo_cell_type();

    const int local_w = sg->local_w;
    const int local_h = sg->local_h;
    const int halo_w  = sg->halo_w;

    Cell *send_west = malloc(sizeof(Cell) * local_h);
    Cell *send_east = malloc(sizeof(Cell) * local_h);
    Cell *recv_west = malloc(sizeof(Cell) * local_h);
    Cell *recv_east = malloc(sizeof(Cell) * local_h);

    MPI_Request reqs[16];
    int nreq = 0;

    MPI_Isend(&sg->cells[CELL_AT(sg, 1, 1)],       local_w, cell_t,
              p->neighbors[DIR_N], TAG_SOUTH, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, 0, 1)],       local_w, cell_t,
              p->neighbors[DIR_N], TAG_NORTH, p->cart_comm, &reqs[nreq++]);

    MPI_Isend(&sg->cells[CELL_AT(sg, local_h, 1)],     local_w, cell_t,
              p->neighbors[DIR_S], TAG_NORTH, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, local_h + 1, 1)], local_w, cell_t,
              p->neighbors[DIR_S], TAG_SOUTH, p->cart_comm, &reqs[nreq++]);

    pack_column(sg, 1, send_west);
    pack_column(sg, local_w, send_east);

    MPI_Isend(send_west, local_h, cell_t,
              p->neighbors[DIR_W], TAG_EAST, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(recv_west, local_h, cell_t,
              p->neighbors[DIR_W], TAG_WEST, p->cart_comm, &reqs[nreq++]);

    MPI_Isend(send_east, local_h, cell_t,
              p->neighbors[DIR_E], TAG_WEST, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(recv_east, local_h, cell_t,
              p->neighbors[DIR_E], TAG_EAST, p->cart_comm, &reqs[nreq++]);

    MPI_Isend(&sg->cells[CELL_AT(sg, 1, 1)],                  1, cell_t,
              p->neighbors[DIR_NW], TAG_SE, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, 0, 0)],                  1, cell_t,
              p->neighbors[DIR_NW], TAG_NW, p->cart_comm, &reqs[nreq++]);

    MPI_Isend(&sg->cells[CELL_AT(sg, 1, local_w)],            1, cell_t,
              p->neighbors[DIR_NE], TAG_SW, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, 0, halo_w - 1)],        1, cell_t,
              p->neighbors[DIR_NE], TAG_NE, p->cart_comm, &reqs[nreq++]);

    MPI_Isend(&sg->cells[CELL_AT(sg, local_h, 1)],            1, cell_t,
              p->neighbors[DIR_SW], TAG_NE, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, sg->halo_h - 1, 0)],    1, cell_t,
              p->neighbors[DIR_SW], TAG_SW, p->cart_comm, &reqs[nreq++]);

    MPI_Isend(&sg->cells[CELL_AT(sg, local_h, local_w)],      1, cell_t,
              p->neighbors[DIR_SE], TAG_NW, p->cart_comm, &reqs[nreq++]);
    MPI_Irecv(&sg->cells[CELL_AT(sg, sg->halo_h - 1, halo_w - 1)], 1, cell_t,
              p->neighbors[DIR_SE], TAG_SE, p->cart_comm, &reqs[nreq++]);

    MPI_Waitall(nreq, reqs, MPI_STATUSES_IGNORE);

    unpack_column(sg, 0,           recv_west);
    unpack_column(sg, local_w + 1, recv_east);

    free(send_west);
    free(send_east);
    free(recv_west);
    free(recv_east);
    MPI_Type_free(&cell_t);
}

#endif /* USE_MPI */
