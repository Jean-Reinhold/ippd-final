#ifndef HALO_H
#define HALO_H

#include "types.h"

/* Direction tags for MPI messages */
#define TAG_NORTH 0
#define TAG_SOUTH 1
#define TAG_EAST  2
#define TAG_WEST  3
#define TAG_NE    4
#define TAG_NW    5
#define TAG_SE    6
#define TAG_SW    7

/* Neighbor index constants (matches Partition.neighbors[]) */
#define DIR_N  0
#define DIR_S  1
#define DIR_E  2
#define DIR_W  3
#define DIR_NE 4
#define DIR_NW 5
#define DIR_SE 6
#define DIR_SW 7

#ifdef USE_MPI

/*
 * Create a committed MPI_Datatype describing the Cell struct.
 * Caller must call MPI_Type_free when done.
 */
MPI_Datatype halo_cell_type(void);

/*
 * Exchange halo (ghost) cells with neighboring MPI ranks.
 * Uses non-blocking MPI_Isend/MPI_Irecv + MPI_Waitall.
 *
 * Exchanges:
 *   - N/S: full rows   (local_w cells each)
 *   - E/W: full columns (local_h cells each, packed into contiguous buffers)
 *   - Diagonals: single corner cells
 */
void halo_exchange(SubGrid *sg, Partition *p);

#endif /* USE_MPI */
#endif /* HALO_H */
