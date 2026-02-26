#ifndef GRID_H
#define GRID_H

#include "types.h"
#include <stdint.h>

/*
 * Initialise a stack-allocated SubGrid using the partition to compute
 * local dimensions and offsets.  Allocates the halo-padded cells array.
 */
void subgrid_create(SubGrid *sg, Partition *p,
                    int global_w, int global_h);

/*
 * Deterministically initialise every owned cell.
 * Cell type and resources are derived from a per-cell RNG seed so the
 * global grid is identical regardless of the MPI decomposition.
 */
void subgrid_init(SubGrid *sg, Partition *p, uint64_t seed);

/*
 * Advance the subgrid by one cycle: regenerate resources according to
 * the current season, update cell accessibility, and clamp values.
 * Uses OpenMP parallel for collapse(2) over the owned interior.
 */
void subgrid_update(SubGrid *sg, Season season);

/* Free the cells array (SubGrid itself is stack-allocated). */
void subgrid_destroy(SubGrid *sg);

#endif /* GRID_H */
