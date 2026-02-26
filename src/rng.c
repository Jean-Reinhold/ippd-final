#include "rng.h"

RngState rng_seed(uint64_t seed) {
    /* Ensure we never start with a zero state (xorshift degenerate case). */
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
     * Divide by 2^64 to map the full uint64 range to [0, 1).
     * The constant 5.421…e-20 = 1.0 / (1 << 64).
     */
    return (rng_next(state) >> 11) * (1.0 / (1ULL << 53));
}

uint64_t rng_cell_seed(uint64_t base_seed, int gx, int gy) {
    /*
     * Multiplicative hashing: the constants 2654435761 (Knuth) and 40503
     * spread coordinate bits across the 64-bit space so nearby cells get
     * very different seeds.
     */
    uint64_t h = base_seed ^ ((uint64_t)gx * 2654435761ULL)
                            ^ ((uint64_t)gy * 40503ULL);
    /* One round of xorshift to mix the bits further. */
    h ^= h << 13;
    h ^= h >> 7;
    h ^= h << 17;
    return h ? h : 1;
}
