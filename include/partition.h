#ifndef PARTITION_H
#define PARTITION_H

#include "types.h"

/*
 * Initialise the 2D Cartesian MPI partition.
 * Derives rank and size from `comm`, factors size into px * py
 * (minimising |px - py|), creates an MPI Cartesian communicator,
 * and computes all 8 neighbour ranks (N, S, E, W, NE, NW, SE, SW).
 */
#ifdef USE_MPI
void partition_init(Partition *p, int global_w, int global_h,
                    MPI_Comm comm);
#else
void partition_init(Partition *p, int global_w, int global_h,
                    int comm);
#endif

/*
 * Compute the local sub-grid dimensions and global offsets for this rank.
 * Work is divided evenly; the last column/row absorbs the remainder.
 */
void partition_subgrid_dims(const Partition *p, int global_w, int global_h,
                            int *local_w, int *local_h,
                            int *offset_x, int *offset_y);

/*
 * Return the MPI rank that owns the cell at global coordinates (gx, gy).
 */
int partition_rank_for_global(const Partition *p, int gx, int gy,
                              int global_w, int global_h);

/*
 * Return 1 if (gx, gy) falls within this rank's owned region.
 */
int partition_owns_global(const Partition *p, const SubGrid *sg,
                          int gx, int gy);

/*
 * Free the Cartesian communicator (if MPI is active).
 */
void partition_destroy(Partition *p);

#endif /* PARTITION_H */
