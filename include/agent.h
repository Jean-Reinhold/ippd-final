#ifndef AGENT_H
#define AGENT_H

#include "types.h"
#include "rng.h"
#include <stdint.h>

/*
 * Deterministically create and distribute agents across MPI ranks.
 * A single global RNG sequence decides every agent's initial position;
 * each rank keeps only the agents that land in its owned sub-grid.
 * The caller provides a pre-allocated `agents` array.
 */
void agents_init(Agent *agents, int *count, int num_total,
                 SubGrid *sg, Partition *p,
                 int global_w, int global_h,
                 double initial_energy, uint64_t seed);

/*
 * Single-agent decision step.
 * Examines 8 neighbours plus the current cell, filters by accessibility,
 * and moves to the cell with the highest resource (ties broken by RNG).
 * The agent gains energy when consuming a resource, loses energy otherwise,
 * and dies (alive = 0) if energy drops to zero or below.
 */
void agent_decide(Agent *a, SubGrid *sg, Season season, RngState *rng,
                  double energy_gain, double energy_loss);

/*
 * Process all alive agents in parallel (OpenMP).
 * For each agent: run workload_compute on the current cell, then
 * agent_decide.  Migration is handled externally by migrate_agents().
 */
void agents_process(Agent *agents, int count, SubGrid *sg,
                    Season season, int max_workload, uint64_t seed,
                    double energy_gain, double energy_loss);

#endif /* AGENT_H */
