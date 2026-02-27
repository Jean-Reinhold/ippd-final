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
 * Executa a carga sintética (workload_compute) para todos os agentes vivos.
 * Apenas o busy-loop, sem RNG — pode ser cronometrado separadamente.
 */
void agents_workload(Agent *agents, int count, SubGrid *sg, int max_workload);

/*
 * Executa a lógica de decisão (agent_decide) para todos os agentes vivos.
 * Requer PRNG per-thread para desempate por reservoir sampling.
 */
void agents_decide_all(Agent *agents, int count, SubGrid *sg,
                       Season season, uint64_t seed,
                       double energy_gain, double energy_loss);

/*
 * Processa todos os agentes vivos em paralelo (OpenMP).
 * Wrapper que chama agents_workload + agents_decide_all em sequência.
 * Mantido para compatibilidade com testes existentes.
 */
void agents_process(Agent *agents, int count, SubGrid *sg,
                    Season season, int max_workload, uint64_t seed,
                    double energy_gain, double energy_loss);

/*
 * Reprodução: agentes com energia acima de threshold geram um filho.
 * O filho nasce na mesma posição com energy = cost; o pai perde cost.
 * Serial (modifica tamanho do array e next_id).
 */
void agents_reproduce(Agent **agents, int *count, int *capacity,
                      int *next_id, double threshold, double cost);

#endif /* AGENT_H */
