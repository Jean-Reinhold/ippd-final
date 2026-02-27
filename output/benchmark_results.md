# Benchmark Results

Os resultados abaixo foram obtidos em um Intel Core i5-1235U com 16 GB de RAM, utilizando uma carga de 256×256, 100 ciclos e 20000 agentes. A coluna **Speedup** usa o tempo de referência (NP=1, THREADS=1).

---

| **NP** | **Threads** | **Time (s)** | **Speedup** |
|:------:|:-----------:|:------------:|:-----------:|
| 1      | 1           | 596.966      | 1.000       |
| 1      | 2           | 331.366      | 1.802       |
| 1      | 3           | 339.762      | 1.757       |
| 1      | 4           | 344.926      | 1.731       |
| 2      | 1           | 328.293      | 1.818       |
| 2      | 2           | 181.057      | 3.297       |
| 2      | 3           | 186.125      | 3.207       |
| 2      | 4           | 188.696      | 3.164       |
| 4      | 1           | 322.361      | 1.852       |
| 4      | 2           | 176.908      | 3.374       |
| 4      | 3           | 125.262      | 4.766       |
| 4      | 4           | 119.771      | 4.984       |

---


# Observações de desempenho

OpenMP puro (NP = 1)
A passagem de 1→2 threads quase dobra a taxa de execução (speedup ≈ 1.8). Entretanto, ganhos adicionais somam pouco: 3 e 4 threads ficam abaixo de 2, indicando que a região paralela não escala perfeitamente e/ou há contenção de memória/cache. Com este workload, usar mais de 2 threads isoladas não compensa.

MPI puro (THREADS = 1)
Saltar de 1 para 2 ranks dá ganho semelhante ao de 2‑thread OpenMP. A escala para 4 ranks é marginalmente melhor (speedup ≈ 1.85), sugerindo que o custo de comunicação e a partição dos dados limitam a eficiência. A divisão de trabalho está longe de ideal em seis ou mais processos.

Modo híbrido
O pico ocorre em NP = 4, THREADS = 3 (12 threads totais), com speedup ≈ 4.77, mostrando boa utilização dos 12 threads lógicos do i5‑1235U. Essa combinação equilibra sobrecarga de MPI e paralelismo de nó.

Oversubscription (NP = 4, THREADS = 4)
Tecnicalmente 16 “thread‑ranks” tentavam correr sobre 12 hardware threads. O tempo caiu levemente para 119 s, resultando em speedup 4.98 – não há degradação acentuada, mas o ganho marginal sobre o caso 4×3 (≈‑4 %) é pequeno e provavelmente dentro da variação de medição. Portanto, embora não tenha havido perda visível, usar 16 unidades é desnecessário e pode causar efeitos negativos em cargas maiores ou numa máquina já carregada.