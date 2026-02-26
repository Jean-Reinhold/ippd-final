#include "workload.h"

double workload_compute(double resource, int max_iters) {
    /*
     * Synthetic busy-loop whose iteration count scales with the cell's
     * resource level.  This creates heterogeneous per-cell cost, which
     * is the key motivation for dynamic load balancing in the simulation.
     *
     * The volatile qualifier on `result` prevents the compiler from
     * eliminating the loop as dead code.
     */
    int iters = (int)(resource * max_iters);
    volatile double result = 0.0;
    for (int i = 0; i < iters; i++) {
        result += i * 0.0001;
    }
    return result;
}
