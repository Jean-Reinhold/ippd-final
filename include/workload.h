#ifndef WORKLOAD_H
#define WORKLOAD_H

/*
 * Executa carga de trabalho sintética proporcional ao nível de recurso
 * da célula. Simula custo computacional variável por célula para
 * exercitar estratégias de balanceamento de carga.
 *
 * Retorna um resultado volatile para impedir o compilador de
 * otimizar o loop.
 */
double workload_compute(double resource, int max_iters);

#endif /* WORKLOAD_H */
