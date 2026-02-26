#ifndef METRICS_H
#define METRICS_H

#include "types.h"

/* Métricas agregadas da simulação */
typedef struct {
    double total_resource;
    double avg_energy;
    double max_energy;
    double min_energy;
    int    alive_agents;
} SimMetrics;

/* Desempenho por ciclo para o dashboard TUI */
typedef struct {
    double cycle_time;
    double compute_time;
    double halo_time;
    double migrate_time;
    double metrics_time;
    double render_time;
    int    mpi_size;
    int    omp_threads;
    double load_balance;
    double comm_compute;
} CyclePerf;

/*
 * Calcula métricas locais a partir da sub-grade e do array de agentes.
 * Soma recursos apenas sobre as células interiores (sem halo).
 */
void metrics_compute_local(const SubGrid *sg, const Agent *agents,
                           int count, SimMetrics *local);

#ifdef USE_MPI
/*
 * Reduz métricas locais de todos os ranks em métricas globais.
 *
 * Reduções:
 *   total_resource → MPI_SUM
 *   alive_agents   → MPI_SUM
 *   max_energy     → MPI_MAX
 *   min_energy     → MPI_MIN
 *   avg_energy     → (soma das energias) / (soma dos vivos)
 */
void metrics_reduce_global(const SimMetrics *local, SimMetrics *global,
                           MPI_Comm comm);
#endif /* USE_MPI */

#endif /* METRICS_H */
