#include "rng.h"

RngState rng_seed(uint64_t seed) {
    /* Evita estado zero, que é o caso degenerado do xorshift. */
    return seed ? seed : 1;
}

uint64_t rng_next(RngState *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

double rng_double(RngState *state) {
    /*
     * Mapeia o intervalo completo de uint64 para [0, 1) dividindo por 2^53.
     * Usa apenas os 53 bits mais significativos para preservar a precisão de double.
     */
    return (rng_next(state) >> 11) * (1.0 / (1ULL << 53));
}

uint64_t rng_cell_seed(uint64_t base_seed, int gx, int gy) {
    /*
     * Hash multiplicativo: as constantes 2654435761 (Knuth) e 40503 espalham
     * os bits das coordenadas pelo espaço de 64 bits, garantindo que células
     * vizinhas recebam seeds bem distintas.
     */
    uint64_t h = base_seed ^ ((uint64_t)gx * 2654435761ULL)
                            ^ ((uint64_t)gy * 40503ULL);
    h ^= h << 13;
    h ^= h >> 7;
    h ^= h << 17;
    return h ? h : 1;
}
