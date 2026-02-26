#ifndef WORKLOAD_H
#define WORKLOAD_H

/*
 * Perform a synthetic computational workload proportional to the cell's
 * resource level.  This simulates varying per-cell computation cost
 * to exercise load-balancing strategies.
 *
 * Returns a volatile result to prevent the compiler from optimizing
 * the loop away.
 */
double workload_compute(double resource, int max_iters);

#endif /* WORKLOAD_H */
