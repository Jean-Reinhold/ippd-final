# Otimização do Balanceamento de Carga (OpenMP)

## 1. Contexto e Problema
A fase de carga sintética (`agents_workload`) consumia quase todo o tempo do ciclo da simulação (entre 98% e 99%). Como o custo computacional por agente é proporcional à quantidade de recurso da célula que ele ocupa, algumas threads terminavam o trabalho muito rápido enquanto outras ficavam sobrecarregadas, gerando um forte desbalanceamento de carga.

## 2. A Solução Implementada
A implementação base utilizava a diretiva OpenMP `#pragma omp parallel for schedule(dynamic, 32)`. 

Nós alteramos a política de escalonamento para `schedule(guided, 8)`. O modelo `guided` começa repassando blocos grandes de iterações para as threads e diminui o tamanho do bloco exponencialmente (até o limite mínimo de 8) conforme o loop se aproxima do final. Isso reduz a quantidade de chamadas ao escalonador (diminuindo o *overhead* gerado pelo modelo `dynamic`) e garante que as threads não fiquem ociosas nos momentos finais do processamento.

## 3. Resultados Obtidos
Os testes foram executados com uma grade de 256x256, 20.000 agentes e 100 ciclos utilizando um processador Intel Core i5 de 12 núcleos lógicos. A tabela abaixo compara o tempo médio gasto exclusivamente na fase de *workload* (medido via `MPI_Wtime()`).

| Configuração (MPI x OpenMP) | Baseline (`dynamic, 32`) | Otimizado (`guided, 8`) | Diferença de Desempenho |
| :--- | :--- | :--- | :--- |
| **1 Rank x 2 Threads** | 241.8 ms | 219.5 ms | **+ 9.2% de ganho** |
| **2 Ranks x 2 Threads** | 122.1 ms | 119.5 ms | **+ 2.1% de ganho** |
| **4 Ranks x 3 Threads** | 84.7 ms | 84.4 ms | **Empate** |
| **4 Ranks x 4 Threads** | 83.0 ms | 84.2 ms | **Leve perda (-1.4%)** |

*(Nota: Os tempos representam a média por ciclo na fase de workload, ignorando o tempo de comunicação MPI e outras fases da simulação).*

## 4. Conclusão
A mudança para a política `guided, 8` resultou em ganhos claros de desempenho nos cenários de paralelismo moderado, melhorando a velocidade em até 9% sem adicionar complexidade ao código. Em cenários de alta concorrência (*oversubscription*), o desempenho se estabilizou, mantendo os tempos na mesma faixa do baseline. A alteração é vantajosa pois otimiza a distribuição da carga e reduz o overhead do sistema.