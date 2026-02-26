#ifndef HALO_H
#define HALO_H

#include "types.h"

/* Tags de direção para mensagens MPI */
#define TAG_NORTH 0
#define TAG_SOUTH 1
#define TAG_EAST  2
#define TAG_WEST  3
#define TAG_NE    4
#define TAG_NW    5
#define TAG_SE    6
#define TAG_SW    7

/* Índices dos vizinhos (correspondem a Partition.neighbors[]) */
#define DIR_N  0
#define DIR_S  1
#define DIR_E  2
#define DIR_W  3
#define DIR_NE 4
#define DIR_NW 5
#define DIR_SE 6
#define DIR_SW 7

#ifdef USE_MPI

/*
 * Cria um MPI_Datatype committed descrevendo a struct Cell.
 * O caller deve chamar MPI_Type_free ao terminar.
 */
MPI_Datatype halo_cell_type(void);

/*
 * Troca células de halo (ghost) com ranks MPI vizinhos.
 * Usa MPI_Isend/MPI_Irecv não-bloqueantes + MPI_Waitall.
 *
 * Trocas:
 *   - N/S: linhas completas (local_w células)
 *   - E/W: colunas completas (local_h células, empacotadas em buffers contíguos)
 *   - Diagonais: célula de canto única
 */
void halo_exchange(SubGrid *sg, Partition *p);

#endif /* USE_MPI */
#endif /* HALO_H */
