#ifndef GRID_H
#define GRID_H

#include "types.h"
#include <stdint.h>

/*
 * Inicializa um SubGrid alocado na stack usando a partição para calcular
 * dimensões locais e offsets. Aloca o array de células com halo.
 */
void subgrid_create(SubGrid *sg, Partition *p,
                    int global_w, int global_h);

/*
 * Inicializa deterministicamente cada célula local.
 * Tipo e recursos derivam de uma seed por célula, garantindo
 * grade global idêntica independentemente da decomposição MPI.
 */
void subgrid_init(SubGrid *sg, Partition *p, uint64_t seed);

/*
 * Avança a sub-grade por um ciclo: regenera recursos conforme a estação,
 * atualiza acessibilidade e limita valores.
 * Usa OpenMP parallel for collapse(2) sobre o interior.
 */
void subgrid_update(SubGrid *sg, Season season);

/* Libera o array de células (o SubGrid em si é alocado na stack). */
void subgrid_destroy(SubGrid *sg);

#endif /* GRID_H */
