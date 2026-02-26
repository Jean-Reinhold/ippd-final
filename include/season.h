#ifndef SEASON_H
#define SEASON_H

#include "types.h"

/* Determina a estação para um dado ciclo de simulação. */
Season season_for_cycle(int cycle, int season_length);

/*
 * Verifica se um tipo de célula é acessível na estação dada.
 * Retorna 1 se acessível, 0 caso contrário.
 */
int season_accessibility(CellType type, Season s);

/*
 * Retorna a taxa de regeneração de recurso para um tipo de célula na estação dada.
 */
double season_regen_rate(CellType type, Season s);

#endif /* SEASON_H */
