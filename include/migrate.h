#ifndef MIGRATE_H
#define MIGRATE_H

#include "types.h"

#ifdef USE_MPI

/*
 * Cria um MPI_Datatype committed descrevendo a struct Agent.
 * O caller deve chamar MPI_Type_free ao terminar.
 */
MPI_Datatype migrate_agent_type(void);

/*
 * Migra agentes cuja posição global (gx, gy) saiu da partição local
 * para o rank correto.
 *
 * Protocolo em duas fases:
 *   Fase 1 — MPI_Alltoall para trocar contagens por rank.
 *   Fase 2 — MPI_Alltoallv para trocar structs Agent.
 *
 * Ao retornar, o array local é compactado: migrantes removidos,
 * recebidos anexados. *count e *capacity são atualizados.
 */
void migrate_agents(Agent **agents, int *count, int *capacity,
                    Partition *p, SubGrid *sg,
                    int global_w, int global_h);

#endif /* USE_MPI */
#endif /* MIGRATE_H */
