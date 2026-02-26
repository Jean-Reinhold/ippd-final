#ifdef USE_MPI

#include "migrate.h"
#include "types.h"
#include <mpi.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

extern int partition_rank_for_global(const Partition *p, int gx, int gy,
                                     int global_w, int global_h);

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

/*
 * Migração all-to-all em duas fases:
 *   Fase 1 — classifica agentes em locais/migrantes, agrupa por rank
 *            destino, troca contagens via MPI_Alltoall.
 *   Fase 2 — troca structs Agent via MPI_Alltoallv.
 * Ao final, compacta o array local removendo migrantes e
 * anexando os agentes recebidos.
 */

void migrate_agents(Agent **agents, int *count, int *capacity,
                    Partition *p, SubGrid *sg,
                    int global_w, int global_h)
{
    const int nprocs  = p->size;
    const int my_rank = p->rank;

    const int x0 = sg->offset_x;
    const int y0 = sg->offset_y;
    const int x1 = x0 + sg->local_w - 1;
    const int y1 = y0 + sg->local_h - 1;

    Agent *ag    = *agents;
    int    n     = *count;

    int *send_counts = calloc(nprocs, sizeof(int));

    int **per_rank_idx = calloc(nprocs, sizeof(int *));
    int  *per_rank_cap = calloc(nprocs, sizeof(int));

    int stay_count = 0;

    for (int i = 0; i < n; i++) {
        if (!ag[i].alive) continue;

        int gx = ag[i].gx;
        int gy = ag[i].gy;

        if (gx >= x0 && gx <= x1 && gy >= y0 && gy <= y1) {
            stay_count++;
            continue;
        }

        int dest = partition_rank_for_global(p, gx, gy, global_w, global_h);
        if (dest == my_rank) {
            /* Caso raro: partition_rank_for_global pode retornar o próprio rank em bordas. */
            stay_count++;
            continue;
        }

        if (send_counts[dest] >= per_rank_cap[dest]) {
            per_rank_cap[dest] = per_rank_cap[dest] ? per_rank_cap[dest] * 2 : 8;
            per_rank_idx[dest] = realloc(per_rank_idx[dest],
                                         sizeof(int) * per_rank_cap[dest]);
        }
        per_rank_idx[dest][send_counts[dest]] = i;
        send_counts[dest]++;
    }

    int *recv_counts = malloc(sizeof(int) * nprocs);
    MPI_Alltoall(send_counts, 1, MPI_INT,
                 recv_counts, 1, MPI_INT, p->cart_comm);

    int *send_displs = malloc(sizeof(int) * nprocs);
    int *recv_displs = malloc(sizeof(int) * nprocs);

    int total_send = 0, total_recv = 0;
    for (int r = 0; r < nprocs; r++) {
        send_displs[r] = total_send;
        recv_displs[r] = total_recv;
        total_send += send_counts[r];
        total_recv += recv_counts[r];
    }

    MPI_Datatype agent_t = migrate_agent_type();

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

    for (int r = 0; r < nprocs; r++) {
        for (int j = 0; j < send_counts[r]; j++) {
            ag[per_rank_idx[r][j]].alive = 0;
        }
    }

    int write_idx = 0;
    for (int i = 0; i < n; i++) {
        if (ag[i].alive) {
            if (write_idx != i)
                ag[write_idx] = ag[i];
            write_idx++;
        }
    }

    int new_count = write_idx + total_recv;
    if (new_count > *capacity) {
        int new_cap = *capacity;
        while (new_cap < new_count)
            new_cap = new_cap ? new_cap * 2 : 16;
        ag = realloc(ag, sizeof(Agent) * new_cap);
        *agents   = ag;
        *capacity = new_cap;
    }

    memcpy(&ag[write_idx], recv_buf, sizeof(Agent) * total_recv);
    *count = new_count;

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
