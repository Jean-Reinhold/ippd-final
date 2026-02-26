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

static const int dx[9] = {  0,  0,  1, -1,  1, -1,  1, -1,  0 };
static const int dy[9] = { -1,  1,  0,  0, -1, -1,  1,  1,  0 };

void agents_init(Agent *agents, int *count, int num_total,
                 SubGrid *sg, Partition *p,
                 int global_w, int global_h,
                 double initial_energy, uint64_t seed) {
    /*
     * Posicionamento determinístico: uma única sequência global de RNG
     * (a partir de `seed`) atribui posição a cada agente. Cada rank
     * mantém apenas os agentes que caem na sua sub-grade, garantindo
     * resultado idêntico independentemente do número de ranks MPI.
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

void agent_decide(Agent *a, SubGrid *sg, Season season, RngState *rng,
                  double energy_gain, double energy_loss) {
    if (!a->alive) return;
    (void)season;  /* acessibilidade já pré-computada em subgrid_update */

    int lc = a->gx - sg->offset_x + 1;
    int lr = a->gy - sg->offset_y + 1;

    double best_resource = -1.0;
    int    best_dir      = 8;
    int    tie_count     = 0;

    for (int d = 0; d < 9; d++) {
        int nc = lc + dx[d];
        int nr = lr + dy[d];

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
            /* Desempate por amostragem de reservatório: troca com probabilidade 1/k. */
            if ((int)(rng_next(rng) % (uint64_t)tie_count) == 0)
                best_dir = d;
        }
    }

    a->gx += dx[best_dir];
    a->gy += dy[best_dir];

    int new_lc = a->gx - sg->offset_x + 1;
    int new_lr = a->gy - sg->offset_y + 1;

    if (new_lc >= 0 && new_lc < sg->halo_w &&
        new_lr >= 0 && new_lr < sg->halo_h) {
        Cell *cell = &sg->cells[CELL_AT(sg, new_lr, new_lc)];
        if (cell->accessible && cell->resource > 0.0) {
            double consumed = (energy_gain < cell->resource)
                              ? energy_gain : cell->resource;
            cell->resource -= consumed;
            a->energy += consumed;
        } else {
            a->energy -= energy_loss;
        }
    } else {
        a->energy -= energy_loss;
    }

    if (a->energy <= 0.0)
        a->alive = 0;
}

void agents_process(Agent *agents, int count, SubGrid *sg,
                    Season season, int max_workload, uint64_t seed,
                    double energy_gain, double energy_loss) {

    #pragma omp parallel
    {
        int tid = 0;
#ifdef _OPENMP
        tid = omp_get_thread_num();
#endif
        RngState rng = rng_seed(seed ^ ((uint64_t)(tid + 1) * 2654435761ULL));

        #pragma omp for schedule(dynamic, 32)
        for (int i = 0; i < count; i++) {
            if (!agents[i].alive) continue;

            int lc = agents[i].gx - sg->offset_x + 1;
            int lr = agents[i].gy - sg->offset_y + 1;
            if (lc >= 1 && lc <= sg->local_w &&
                lr >= 1 && lr <= sg->local_h) {
                int idx = CELL_AT(sg, lr, lc);
                workload_compute(sg->cells[idx].resource, max_workload);
            }

            agent_decide(&agents[i], sg, season, &rng,
                         energy_gain, energy_loss);
        }
    }
}
