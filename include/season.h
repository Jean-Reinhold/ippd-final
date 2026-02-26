#ifndef SEASON_H
#define SEASON_H

#include "types.h"

/* Determine the season for a given simulation cycle. */
Season season_for_cycle(int cycle, int season_length);

/*
 * Check whether a cell type is accessible in the given season.
 * Returns 1 if accessible, 0 otherwise.
 */
int season_accessibility(CellType type, Season s);

/*
 * Get the resource regeneration rate for a cell type in the given season.
 */
double season_regen_rate(CellType type, Season s);

#endif /* SEASON_H */
