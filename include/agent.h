#ifndef AGENT_H
#define AGENT_H

#include "types.h"
#include "rng.h"
#include <stdint.h>

/*
 * Cria e distribui agentes deterministicamente entre ranks MPI.
 * Uma única sequência global de RNG decide a posição inicial de cada agente;
 * cada rank mantém apenas os que caem na sua sub-grade.
 * O caller fornece o array `agents` pré-alocado.
 */
void agents_init(Agent *agents, int *count, int num_total,
                 SubGrid *sg, Partition *p,
                 int global_w, int global_h,
                 double initial_energy, uint64_t seed);

/*
 * Passo de decisão de um agente.
 * Examina 8 vizinhos + célula atual, filtra por acessibilidade,
 * e move para a célula com mais recurso (empates resolvidos por RNG).
 * O agente ganha energia ao consumir recurso, perde caso contrário,
 * e morre (alive = 0) se a energia cair a zero ou abaixo.
 */
void agent_decide(Agent *a, SubGrid *sg, Season season, RngState *rng,
                  double energy_gain, double energy_loss);

/*
 * Processa todos os agentes vivos em paralelo (OpenMP).
 * Para cada agente: executa workload_compute na célula atual,
 * depois agent_decide. Migração é tratada externamente por migrate_agents().
 */
void agents_process(Agent *agents, int count, SubGrid *sg,
                    Season season, int max_workload, uint64_t seed,
                    double energy_gain, double energy_loss);

#endif /* AGENT_H */
