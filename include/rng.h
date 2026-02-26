#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/* Deterministic xorshift64 PRNG — lightweight and reproducible. */
typedef uint64_t RngState;

/* Seed a new PRNG state. */
RngState rng_seed(uint64_t seed);

/* Generate the next pseudo-random 64-bit integer. */
uint64_t rng_next(RngState *state);

/* Generate a uniform double in [0, 1). */
double rng_double(RngState *state);

/*
 * Derive a deterministic per-cell seed from a base seed and grid coordinates.
 * Uses multiplicative hashing to spread entropy across the grid.
 */
uint64_t rng_cell_seed(uint64_t base_seed, int gx, int gy);

#endif /* RNG_H */
