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

/* Per-cycle performance breakdown for the TUI dashboard */
typedef struct {
    double cycle_time;      /* Total wall time for the cycle       */
    double compute_time;    /* Agent processing + subgrid update   */
    double halo_time;       /* Halo exchange                       */
    double migrate_time;    /* Agent migration                     */
    double metrics_time;    /* Metrics reduction                   */
    double render_time;     /* TUI gather + render                 */
    int    mpi_size;        /* Total MPI ranks                     */
    int    omp_threads;     /* OMP threads per rank                */
    double load_balance;    /* min_agents/max_agents across ranks  */
    double comm_compute;    /* (halo+migrate+metrics)/compute      */
} CyclePerf;

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
