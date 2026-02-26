#include "season.h"

Season season_for_cycle(int cycle, int season_length) {
    /*
     * Seasons alternate every season_length cycles.
     * Integer division gives the epoch index; even = DRY, odd = WET.
     */
    return ((cycle / season_length) % 2 == 0) ? DRY : WET;
}

int season_accessibility(CellType type, Season s) {
    switch (type) {
        case ALDEIA:      return 1;         /* village — always open */
        case PESCA:       return s == DRY;  /* fishing — dry season only */
        case COLETA:      return 1;         /* gathering — always open */
        case ROCADO:      return s == WET;  /* farming — wet season only */
        case INTERDITADA: return 0;         /* forbidden — never */
    }
    return 0;
}

double season_regen_rate(CellType type, Season s) {
    /*
     * Regeneration rates (dry / wet):
     *   ALDEIA      0.0 / 0.0   (village, no natural regen)
     *   PESCA       0.3 / 0.1   (fish thrive in dry, slower in wet)
     *   COLETA      0.1 / 0.3   (gathering improves in wet season)
     *   ROCADO      0.2 / 0.4   (farming benefits from rain)
     *   INTERDITADA 0.0 / 0.0   (forbidden, no regen)
     */
    static const double rates[5][2] = {
        /* DRY   WET */
        { 0.0,  0.0 },   /* ALDEIA */
        { 0.3,  0.1 },   /* PESCA */
        { 0.1,  0.3 },   /* COLETA */
        { 0.2,  0.4 },   /* ROCADO */
        { 0.0,  0.0 },   /* INTERDITADA */
    };
    return rates[type][s];
}
