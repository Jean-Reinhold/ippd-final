#include "agent.h"
#include "config.h"
#include "partition.h"
#include "season.h"
#include "workload.h"

#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/* ── Direction offsets: N, S, E, W, NE, NW, SE, SW, Stay ── */
/* dx affects column (gx), dy affects row (gy).               */
static const int dx[9] = {  0,  0,  1, -1,  1, -1,  1, -1,  0 };
static const int dy[9] = { -1,  1,  0,  0, -1, -1,  1,  1,  0 };

/* ------------------------------------------------------------------ */

void agents_init(Agent *agents, int *count, int num_total,
                 SubGrid *sg, Partition *p,
                 int global_w, int global_h,
                 double initial_energy, uint64_t seed) {
    /*
     * Deterministic placement: a single global RNG sequence (seeded from
     * `seed`) assigns every agent a position.  Each rank keeps only the
     * agents that land inside its owned sub-grid, so the result is the
     * same regardless of how many MPI ranks participate.
     */
    *count = 0;
    RngState grng = rng_seed(seed ^ 0xA6E47ULL);

    for (int i = 0; i < num_total; i++) {
        int gx = (int)(rng_next(&grng) % (uint64_t)global_w);
        int gy = (int)(rng_next(&grng) % (uint64_t)global_h);

        if (partition_owns_global(p, sg, gx, gy)) {
            Agent *a  = &agents[*count];
            a->id     = i;
            a->gx     = gx;
            a->gy     = gy;
            a->energy = initial_energy;
            a->alive  = 1;
            (*count)++;
        }
    }
}

/* ------------------------------------------------------------------ */

void agent_decide(Agent *a, SubGrid *sg, Season season, RngState *rng,
                  double energy_gain, double energy_loss) {
    if (!a->alive) return;
    (void)season;  /* accessibility is pre-computed in cells by subgrid_update */

    /* Current position in halo coordinates (interior starts at (1,1)). */
    int lc = a->gx - sg->offset_x + 1;   /* column */
    int lr = a->gy - sg->offset_y + 1;   /* row    */

    double best_resource = -1.0;
    int    best_dir      = 8;   /* default: stay */
    int    tie_count     = 0;

    for (int d = 0; d < 9; d++) {
        int nc = lc + dx[d];
        int nr = lr + dy[d];

        /* Stay within halo bounds (0 .. halo_w-1, 0 .. halo_h-1). */
        if (nc < 0 || nc >= sg->halo_w || nr < 0 || nr >= sg->halo_h)
            continue;

        Cell *cell = &sg->cells[CELL_AT(sg, nr, nc)];
        if (!cell->accessible)
            continue;

        if (cell->resource > best_resource) {
            best_resource = cell->resource;
            best_dir      = d;
            tie_count     = 1;
        } else if (cell->resource == best_resource) {
            tie_count++;
            /* Reservoir-sampling tie-break: replace with probability 1/k. */
            if ((int)(rng_next(rng) % (uint64_t)tie_count) == 0)
                best_dir = d;
        }
    }

    /* ── Move ── */
    a->gx += dx[best_dir];
    a->gy += dy[best_dir];

    /* ── Consume / energy bookkeeping ── */
    int new_lc = a->gx - sg->offset_x + 1;
    int new_lr = a->gy - sg->offset_y + 1;

    if (new_lc >= 0 && new_lc < sg->halo_w &&
        new_lr >= 0 && new_lr < sg->halo_h) {
        Cell *cell = &sg->cells[CELL_AT(sg, new_lr, new_lc)];
        if (cell->accessible && cell->resource > 0.0) {
            double consumed = (energy_gain < cell->resource)
                              ? energy_gain : cell->resource;
            cell->resource -= consumed;
            a->energy += energy_gain;
        } else {
            a->energy -= energy_loss;
        }
    } else {
        a->energy -= energy_loss;
    }

    if (a->energy <= 0.0)
        a->alive = 0;
}

/* ------------------------------------------------------------------ */

void agents_process(Agent *agents, int count, SubGrid *sg,
                    Season season, int max_workload, uint64_t seed) {
    double energy_gain = DEFAULT_ENERGY_GAIN;
    double energy_loss = DEFAULT_ENERGY_LOSS;

    #pragma omp parallel
    {
        int tid = 0;
#ifdef _OPENMP
        tid = omp_get_thread_num();
#endif
        /* Per-thread RNG seeded deterministically from global seed + tid. */
        RngState rng = rng_seed(seed ^ ((uint64_t)(tid + 1) * 2654435761ULL));

        #pragma omp for schedule(dynamic, 32)
        for (int i = 0; i < count; i++) {
            if (!agents[i].alive) continue;

            /* Synthetic workload proportional to current cell's resource. */
            int lc = agents[i].gx - sg->offset_x + 1;
            int lr = agents[i].gy - sg->offset_y + 1;
            if (lc >= 1 && lc <= sg->local_w &&
                lr >= 1 && lr <= sg->local_h) {
                int idx = CELL_AT(sg, lr, lc);
                workload_compute(sg->cells[idx].resource, max_workload);
            }

            /* Decision & movement. */
            agent_decide(&agents[i], sg, season, &rng,
                         energy_gain, energy_loss);
        }
    } /* end parallel */
}
