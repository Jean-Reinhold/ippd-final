#include "metrics.h"
#include "types.h"
#include <float.h>

void metrics_compute_local(const SubGrid *sg, const Agent *agents,
                           int count, SimMetrics *local)
{
    double total_res = 0.0;
    for (int r = 1; r <= sg->local_h; r++) {
        for (int c = 1; c <= sg->local_w; c++) {
            total_res += sg->cells[CELL_AT(sg, r, c)].resource;
        }
    }
    local->total_resource = total_res;

    double sum_energy = 0.0;
    double max_e      = -DBL_MAX;
    double min_e      =  DBL_MAX;
    int    alive      = 0;

    for (int i = 0; i < count; i++) {
        if (!agents[i].alive) continue;
        double e = agents[i].energy;
        sum_energy += e;
        if (e > max_e) max_e = e;
        if (e < min_e) min_e = e;
        alive++;
    }

    local->alive_agents = alive;
    local->max_energy   = (alive > 0) ? max_e : 0.0;
    local->min_energy   = (alive > 0) ? min_e : 0.0;
    /* Guarda a soma por enquanto; o passo de redução calcula a média
       real como soma_global / vivos_global. */
    local->avg_energy   = sum_energy;
}

#ifdef USE_MPI

#include <mpi.h>

void metrics_reduce_global(const SimMetrics *local, SimMetrics *global,
                           MPI_Comm comm)
{
    MPI_Allreduce(&local->total_resource, &global->total_resource,
                  1, MPI_DOUBLE, MPI_SUM, comm);

    MPI_Allreduce(&local->alive_agents, &global->alive_agents,
                  1, MPI_INT, MPI_SUM, comm);

    /* avg_energy armazena a soma local; reduz a soma e divide pelo total de vivos. */
    double energy_sum_local = local->avg_energy;
    double energy_sum_global;
    MPI_Allreduce(&energy_sum_local, &energy_sum_global,
                  1, MPI_DOUBLE, MPI_SUM, comm);
    global->avg_energy = (global->alive_agents > 0)
                       ? energy_sum_global / global->alive_agents
                       : 0.0;

    MPI_Allreduce(&local->max_energy, &global->max_energy,
                  1, MPI_DOUBLE, MPI_MAX, comm);

    /* Sentinela DBL_MAX para ranks sem agentes vivos, evitando que
       um min espúrio contamine o resultado global. */
    double local_min = (local->alive_agents > 0)
                     ? local->min_energy
                     : DBL_MAX;
    MPI_Allreduce(&local_min, &global->min_energy,
                  1, MPI_DOUBLE, MPI_MIN, comm);

    if (global->alive_agents == 0)
        global->min_energy = 0.0;
}

#endif /* USE_MPI */
