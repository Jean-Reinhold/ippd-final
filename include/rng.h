#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/* PRNG xorshift64 determinístico — leve e reprodutível. */
typedef uint64_t RngState;

/* Inicializa um novo estado do PRNG. */
RngState rng_seed(uint64_t seed);

/* Gera o próximo inteiro pseudo-aleatório de 64 bits. */
uint64_t rng_next(RngState *state);

/* Gera um double uniforme em [0, 1). */
double rng_double(RngState *state);

/*
 * Deriva uma seed determinística por célula a partir de uma seed base
 * e coordenadas da grade. Usa hash multiplicativo para espalhar entropia.
 */
uint64_t rng_cell_seed(uint64_t base_seed, int gx, int gy);

#endif /* RNG_H */
