#ifdef USE_MPI

#include "migrate.h"
#include "types.h"
#include <mpi.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* Defined in partition.c (created by another teammate) */
extern int partition_rank_for_global(const Partition *p, int gx, int gy,
                                     int global_w, int global_h);

/* ────────────────────────────────────────────────────────────────────────── *
 * MPI Datatype for Agent                                                    *
 * ────────────────────────────────────────────────────────────────────────── */

MPI_Datatype migrate_agent_type(void)
{
    const int nfields = 5;
    int          block_lengths[5] = {1, 1, 1, 1, 1};
    MPI_Datatype field_types[5]   = {MPI_INT, MPI_INT, MPI_INT,
                                     MPI_DOUBLE, MPI_INT};
    MPI_Aint     offsets[5];

    offsets[0] = offsetof(Agent, id);
    offsets[1] = offsetof(Agent, gx);
    offsets[2] = offsetof(Agent, gy);
    offsets[3] = offsetof(Agent, energy);
    offsets[4] = offsetof(Agent, alive);

    MPI_Datatype dt;
    MPI_Type_create_struct(nfields, block_lengths, offsets, field_types, &dt);
    MPI_Type_commit(&dt);
    return dt;
}

/* ────────────────────────────────────────────────────────────────────────── *
 * migrate_agents — Two-phase all-to-all migration                           *
 * ────────────────────────────────────────────────────────────────────────── *
 *                                                                           *
 * Phase 1: Scan local agents and partition into staying / migrating.        *
 *          Group migrating agents by destination rank.                      *
 *          Exchange per-rank counts with MPI_Alltoall.                      *
 *                                                                           *
 * Phase 2: Exchange Agent structs via MPI_Alltoallv with computed           *
 *          displacement arrays.                                             *
 *                                                                           *
 * Finally, compact the local array: remove migrated agents, append          *
 * received agents.                                                          *
 * ────────────────────────────────────────────────────────────────────────── */

void migrate_agents(Agent **agents, int *count, int *capacity,
                    Partition *p, SubGrid *sg,
                    int global_w, int global_h)
{
    const int nprocs  = p->size;
    const int my_rank = p->rank;

    /* Region owned by this rank (global coordinates, inclusive) */
    const int x0 = sg->offset_x;
    const int y0 = sg->offset_y;
    const int x1 = x0 + sg->local_w - 1;
    const int y1 = y0 + sg->local_h - 1;

    Agent *ag    = *agents;
    int    n     = *count;

    /* ── Phase 0: classify agents ─────────────────────────────────────── */

    /* send_counts[r] = number of agents migrating to rank r */
    int *send_counts = calloc(nprocs, sizeof(int));

    /* Temporary per-rank lists (indices into the agent array) */
    int **per_rank_idx = calloc(nprocs, sizeof(int *));
    int  *per_rank_cap = calloc(nprocs, sizeof(int));

    int stay_count = 0;

    for (int i = 0; i < n; i++) {
        if (!ag[i].alive) continue;

        int gx = ag[i].gx;
        int gy = ag[i].gy;

        if (gx >= x0 && gx <= x1 && gy >= y0 && gy <= y1) {
            /* Agent stays */
            stay_count++;
            continue;
        }

        /* Agent must migrate */
        int dest = partition_rank_for_global(p, gx, gy, global_w, global_h);
        if (dest == my_rank) {
            /* Edge case: still local (shouldn't happen normally) */
            stay_count++;
            continue;
        }

        /* Add to per-rank list */
        if (send_counts[dest] >= per_rank_cap[dest]) {
            per_rank_cap[dest] = per_rank_cap[dest] ? per_rank_cap[dest] * 2 : 8;
            per_rank_idx[dest] = realloc(per_rank_idx[dest],
                                         sizeof(int) * per_rank_cap[dest]);
        }
        per_rank_idx[dest][send_counts[dest]] = i;
        send_counts[dest]++;
    }

    /* ── Phase 1: exchange counts ─────────────────────────────────────── */

    int *recv_counts = malloc(sizeof(int) * nprocs);
    MPI_Alltoall(send_counts, 1, MPI_INT,
                 recv_counts, 1, MPI_INT, p->cart_comm);

    /* Build displacement arrays for MPI_Alltoallv */
    int *send_displs = malloc(sizeof(int) * nprocs);
    int *recv_displs = malloc(sizeof(int) * nprocs);

    int total_send = 0, total_recv = 0;
    for (int r = 0; r < nprocs; r++) {
        send_displs[r] = total_send;
        recv_displs[r] = total_recv;
        total_send += send_counts[r];
        total_recv += recv_counts[r];
    }

    /* ── Phase 2: exchange agent data ─────────────────────────────────── */

    MPI_Datatype agent_t = migrate_agent_type();

    /* Pack send buffer in rank order */
    Agent *send_buf = malloc(sizeof(Agent) * (total_send > 0 ? total_send : 1));
    int pos = 0;
    for (int r = 0; r < nprocs; r++) {
        for (int j = 0; j < send_counts[r]; j++) {
            send_buf[pos++] = ag[per_rank_idx[r][j]];
        }
    }

    Agent *recv_buf = malloc(sizeof(Agent) * (total_recv > 0 ? total_recv : 1));

    MPI_Alltoallv(send_buf, send_counts, send_displs, agent_t,
                  recv_buf, recv_counts, recv_displs, agent_t,
                  p->cart_comm);

    /* ── Compact local array: keep staying agents, append received ───── */

    /* Mark migrated agents as dead so we can filter them out */
    for (int r = 0; r < nprocs; r++) {
        for (int j = 0; j < send_counts[r]; j++) {
            ag[per_rank_idx[r][j]].alive = 0;
        }
    }

    /* Compact: move alive agents to the front */
    int write_idx = 0;
    for (int i = 0; i < n; i++) {
        if (ag[i].alive) {
            if (write_idx != i)
                ag[write_idx] = ag[i];
            write_idx++;
        }
    }

    /* Ensure capacity for received agents */
    int new_count = write_idx + total_recv;
    if (new_count > *capacity) {
        int new_cap = *capacity;
        while (new_cap < new_count)
            new_cap = new_cap ? new_cap * 2 : 16;
        ag = realloc(ag, sizeof(Agent) * new_cap);
        *agents   = ag;
        *capacity = new_cap;
    }

    /* Append received agents */
    memcpy(&ag[write_idx], recv_buf, sizeof(Agent) * total_recv);
    *count = new_count;

    /* ── Cleanup ──────────────────────────────────────────────────────── */
    MPI_Type_free(&agent_t);
    free(send_buf);
    free(recv_buf);
    free(send_counts);
    free(recv_counts);
    free(send_displs);
    free(recv_displs);
    for (int r = 0; r < nprocs; r++)
        free(per_rank_idx[r]);
    free(per_rank_idx);
    free(per_rank_cap);
}

#endif /* USE_MPI */
