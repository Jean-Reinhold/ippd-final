#include "season.h"

Season season_for_cycle(int cycle, int season_length) {
    /*
     * Estações alternam a cada season_length ciclos.
     * Divisão inteira dá o índice da época: par = DRY, ímpar = WET.
     */
    return ((cycle / season_length) % 2 == 0) ? DRY : WET;
}

int season_accessibility(CellType type, Season s) {
    switch (type) {
        case ALDEIA:      return 1;         /* aldeia — sempre acessível */
        case PESCA:       return s == DRY;  /* pesca — só na seca */
        case COLETA:      return 1;         /* coleta — sempre acessível */
        case ROCADO:      return s == WET;  /* roçado — só na chuva */
        case INTERDITADA: return 0;         /* interditada — nunca */
    }
    return 0;
}

double season_regen_rate(CellType type, Season s) {
    /*
     * Taxas de regeneração (seca / chuva):
     *   ALDEIA      0.0 / 0.0   (sem regeneração natural)
     *   PESCA       0.03 / 0.01  (peixes prosperam na seca)
     *   COLETA      0.01 / 0.03  (coleta melhora na chuva)
     *   ROCADO      0.02 / 0.04  (roçado beneficia da chuva)
     *   INTERDITADA 0.0 / 0.0   (sem regeneração)
     */
    static const double rates[5][2] = {
        /* DRY    WET */
        { 0.00,  0.00 },   /* ALDEIA */
        { 0.03,  0.01 },   /* PESCA  (was 0.3, 0.1) */
        { 0.01,  0.03 },   /* COLETA (was 0.1, 0.3) */
        { 0.02,  0.04 },   /* ROCADO (was 0.2, 0.4) */
        { 0.00,  0.00 },   /* INTERDITADA */
    };
    return rates[type][s];
}
