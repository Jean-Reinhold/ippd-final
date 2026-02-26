#include "grid.h"
#include "partition.h"
#include "rng.h"
#include "season.h"

#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

static const double max_resources[5] = {
    0.5,  /* ALDEIA      */
    1.0,  /* PESCA       */
    0.8,  /* COLETA      */
    0.9,  /* ROCADO      */
    0.0   /* INTERDITADA */
};

void subgrid_create(SubGrid *sg, Partition *p,
                    int global_w, int global_h) {
    int local_w, local_h, offset_x, offset_y;
    partition_subgrid_dims(p, global_w, global_h,
                           &local_w, &local_h, &offset_x, &offset_y);

    sg->local_w  = local_w;
    sg->local_h  = local_h;
    sg->offset_x = offset_x;
    sg->offset_y = offset_y;
    sg->halo_w   = local_w + 2;
    sg->halo_h   = local_h + 2;

    sg->cells = calloc((size_t)sg->halo_h * sg->halo_w, sizeof(Cell));
}

void subgrid_init(SubGrid *sg, Partition *p, uint64_t seed) {
    (void)p;  /* offsets já estão armazenados no SubGrid */

    for (int r = 1; r <= sg->local_h; r++) {
        for (int c = 1; c <= sg->local_w; c++) {
            int gx = sg->offset_x + (c - 1);
            int gy = sg->offset_y + (r - 1);

            uint64_t cseed = rng_cell_seed(seed, gx, gy);
            RngState rng   = rng_seed(cseed);

            CellType type    = (CellType)(rng_next(&rng) % 5);
            double   max_res = max_resources[type];

            int idx = CELL_AT(sg, r, c);
            sg->cells[idx].type         = type;
            sg->cells[idx].max_resource = max_res;
            sg->cells[idx].resource     = 0.0;
            sg->cells[idx].accessible   = 1;
        }
    }
}

void subgrid_update(SubGrid *sg, Season season) {
    #pragma omp parallel for collapse(2) schedule(static)
    for (int r = 1; r <= sg->local_h; r++) {
        for (int c = 1; c <= sg->local_w; c++) {
            int   idx  = CELL_AT(sg, r, c);
            Cell *cell = &sg->cells[idx];

            double regen = season_regen_rate(cell->type, season);
            cell->resource += regen * (cell->max_resource - cell->resource);

            if (cell->resource < 0.0)
                cell->resource = 0.0;
            if (cell->resource > cell->max_resource)
                cell->resource = cell->max_resource;

            cell->accessible = season_accessibility(cell->type, season);
        }
    }
}

void subgrid_destroy(SubGrid *sg) {
    if (sg) {
        free(sg->cells);
        sg->cells = NULL;
    }
}
