#ifndef METRICS_H
#define METRICS_H

#include "types.h"

/* Aggregated simulation metrics */
typedef struct {
    double total_resource;
    double avg_energy;
    double max_energy;
    double min_energy;
    int    alive_agents;
} SimMetrics;

/*
 * Compute local metrics from the subgrid and agent array.
 * Only sums resources over owned (interior) cells.
 */
void metrics_compute_local(const SubGrid *sg, const Agent *agents,
                           int count, SimMetrics *local);

#ifdef USE_MPI
/*
 * Reduce local metrics across all ranks into global metrics.
 *
 * Reductions:
 *   total_resource → MPI_SUM
 *   alive_agents   → MPI_SUM
 *   max_energy     → MPI_MAX
 *   min_energy     → MPI_MIN
 *   avg_energy     → (SUM of energy_sums) / (SUM of alive counts)
 */
void metrics_reduce_global(const SimMetrics *local, SimMetrics *global,
                           MPI_Comm comm);
#endif /* USE_MPI */

#endif /* METRICS_H */
