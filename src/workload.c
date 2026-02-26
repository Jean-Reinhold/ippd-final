#include "workload.h"

double workload_compute(double resource, int max_iters) {
    /*
     * Busy-loop sintético cuja contagem de iterações escala com o nível
     * de recurso da célula. Isso cria custo heterogêneo por célula, que
     * é a motivação principal para balanceamento dinâmico de carga.
     *
     * O qualificador volatile em `result` impede o compilador de
     * eliminar o loop como código morto.
     */
    int iters = (int)(resource * max_iters);
    volatile double result = 0.0;
    for (int i = 0; i < iters; i++) {
        result += i * 0.0001;
    }
    return result;
}
