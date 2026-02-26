#ifndef MIGRATE_H
#define MIGRATE_H

#include "types.h"

#ifdef USE_MPI

/*
 * Create a committed MPI_Datatype describing the Agent struct.
 * Caller must call MPI_Type_free when done.
 */
MPI_Datatype migrate_agent_type(void);

/*
 * Migrate agents whose global position (gx, gy) falls outside the local
 * partition to the correct owning rank.
 *
 * Two-phase protocol:
 *   Phase 1 — MPI_Alltoall to exchange per-rank migration counts.
 *   Phase 2 — MPI_Alltoallv to exchange Agent structs.
 *
 * On return the local agent array is compacted: migrated agents are removed,
 * received agents are appended.  *count and *capacity are updated.
 */
void migrate_agents(Agent **agents, int *count, int *capacity,
                    Partition *p, SubGrid *sg,
                    int global_w, int global_h);

#endif /* USE_MPI */
#endif /* MIGRATE_H */
