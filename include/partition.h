#ifndef PARTITION_H
#define PARTITION_H

#include "types.h"

/*
 * Inicializa a partição MPI cartesiana 2D.
 * Obtém rank e size de `comm`, fatora size em px * py
 * (minimizando |px - py|), cria um comunicador cartesiano MPI,
 * e calcula os 8 ranks vizinhos (N, S, E, W, NE, NW, SE, SW).
 */
#ifdef USE_MPI
void partition_init(Partition *p, int global_w, int global_h,
                    MPI_Comm comm);
#else
void partition_init(Partition *p, int global_w, int global_h,
                    int comm);
#endif

/*
 * Calcula dimensões locais da sub-grade e offsets globais deste rank.
 * O trabalho é dividido uniformemente; a última coluna/linha absorve o resto.
 */
void partition_subgrid_dims(const Partition *p, int global_w, int global_h,
                            int *local_w, int *local_h,
                            int *offset_x, int *offset_y);

/*
 * Retorna o rank MPI que possui a célula nas coordenadas globais (gx, gy).
 */
int partition_rank_for_global(const Partition *p, int gx, int gy,
                              int global_w, int global_h);

/*
 * Retorna 1 se (gx, gy) cai na região deste rank.
 */
int partition_owns_global(const Partition *p, const SubGrid *sg,
                          int gx, int gy);

/*
 * Libera o comunicador cartesiano (se MPI estiver ativo).
 */
void partition_destroy(Partition *p);

#endif /* PARTITION_H */
