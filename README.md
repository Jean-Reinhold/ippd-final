# Simulação de uso da terra na Amazônia

Simulação baseada em agentes que modela o uso da terra em categorias amazônicas (aldeia, pesca, coleta, roçado, interditada). Os agentes se movem pela grade buscando recursos, enquanto estações secas e chuvosas alternam a acessibilidade e regeneração de cada tipo de célula. A simulação é paralelizada com MPI (distribuição da grade entre processos) e OpenMP (processamento de agentes por threads), e conta com uma TUI interativa para visualização em tempo real. Trabalho final da disciplina de IPPD.

## Como compilar

```bash
make
```

Requer `mpicc` com suporte a OpenMP. No macOS com Homebrew, o Makefile já exporta `OMPI_CC=gcc-15` para usar o GCC ao invés do clang (que não tem OpenMP nativo).

## Como executar

```bash
# Um processo, TUI interativa
./sim

# 4 processos MPI, 10 ciclos, sem TUI
mpirun --oversubscribe -np 4 ./sim -c 10 --no-tui

# Script auxiliar com variáveis de ambiente
NP=2 THREADS=4 WIDTH=64 CYCLES=100 ./scripts/run.sh
```

## Parâmetros

| Flag             | Descrição                         | Padrão  |
|------------------|-----------------------------------|---------|
| `-w WIDTH`       | Largura da grade                  | 64      |
| `-h HEIGHT`      | Altura da grade                   | 64      |
| `-c CYCLES`      | Total de ciclos                   | 100     |
| `-s SEASON_LEN`  | Ciclos por estação                | 10      |
| `-a AGENTS`      | Número de agentes                 | 50      |
| `-W WORKLOAD`    | Iterações máximas de workload     | 500000  |
| `-S SEED`        | Seed do RNG                       | 42      |
| `--no-tui`       | Desabilita a TUI                  | —       |
| `--tui-interval N`| Renderiza a cada N ciclos        | 1       |
| `--csv`          | Saída CSV de timing por ciclo     | —       |

## Estrutura do projeto

```
src/
  main.c        — loop principal, parsing de args, orquestração MPI
  agent.c       — decisão e movimentação dos agentes
  grid.c        — criação, inicialização e atualização da sub-grade
  halo.c        — troca de halos (ghost cells) entre ranks vizinhos
  migrate.c     — migração de agentes entre ranks via MPI_Alltoallv
  partition.c   — decomposição cartesiana 2D e cálculo de vizinhos
  metrics.c     — métricas locais e redução global (MPI_Allreduce)
  season.c      — lógica de estações, acessibilidade e regeneração
  rng.c         — PRNG xorshift64 determinístico
  workload.c    — carga de trabalho sintética para balanceamento
  tui.c         — interface terminal com ANSI 256 cores

include/
  types.h       — structs e enums compartilhados (Cell, Agent, SubGrid, Partition)
  config.h      — valores padrão da configuração
  *.h           — headers de cada módulo
```

## Funcionamento

A cada ciclo, a simulação determina a estação (seca ou chuva), que define quais tipos de célula estão acessíveis e suas taxas de regeneração. Os agentes examinam as 8 células vizinhas mais a atual, escolhem a de maior recurso (com desempate por amostragem de reservatório) e consomem parte do recurso para ganhar energia. Agentes sem energia morrem.

A grade global é dividida entre processos MPI em uma topologia cartesiana 2D. Cada rank mantém sua sub-grade com uma borda de halo de 1 célula em cada lado. Antes do processamento de agentes, os halos são trocados com os vizinhos via Isend/Irecv não-bloqueantes. Depois da movimentação, agentes que saíram da partição local são migrados para o rank correto usando MPI_Alltoallv em duas fases.

No rank 0, a TUI coleta a grade e os agentes de todos os ranks via MPI_Gather e renderiza um mapa colorido no terminal, com um painel lateral mostrando métricas de desempenho (tempo por fase, balanceamento de carga, razão comunicação/computação). A TUI suporta pausa, passo a passo, e controle de velocidade pelo teclado.

## Lógica de Decisão dos Agentes

A cada ciclo, cada agente executa o seguinte processo de decisão:

1. **Varredura de vizinhança**: examina as 8 células vizinhas (Moore neighborhood) mais a célula atual (9 candidatas no total)
2. **Filtragem**: descarta células inacessíveis (fora dos limites ou bloqueadas pela estação)
3. **Seleção gulosa**: escolhe a célula com maior recurso disponível
4. **Desempate**: quando múltiplas células têm o mesmo recurso máximo, usa amostragem de reservatório (*reservoir sampling*) com o PRNG per-thread — garante aleatoriedade uniforme sem precisar armazenar todos os empates
5. **Movimentação**: desloca-se para a célula escolhida
6. **Consumo**: se a célula é acessível e tem recurso, consome `min(energy_gain, cell.resource)`
7. **Penalidade**: se não consegue consumir, perde `energy_loss` de energia
8. **Morte**: se `energy <= 0`, o agente morre (`alive = 0`)

## Mecânica de Energia

| Parâmetro        | Valor padrão | Descrição                          |
|------------------|--------------|------------------------------------|
| `initial_energy` | 1.0          | Energia inicial de cada agente     |
| `energy_gain`    | 0.3          | Ganho máximo por consumo           |
| `energy_loss`    | 0.05         | Perda quando não consome           |

Fórmula de consumo:
```
consumed = min(energy_gain, cell.resource)
cell.resource -= consumed
agent.energy  += consumed
```

## Sistema de Estações

As estações alternam a cada `season_length` ciclos (padrão: 10). A divisão inteira do ciclo pelo comprimento da estação determina o índice: par = seca, ímpar = chuva.

### Acessibilidade por estação

| Tipo de célula | Seca | Chuva |
|----------------|------|-------|
| Aldeia         | Sim  | Sim   |
| Pesca          | Sim  | Não   |
| Coleta         | Sim  | Sim   |
| Roçado         | Não  | Sim   |
| Interditada    | Não  | Não   |

### Taxas de regeneração

| Tipo de célula | Seca | Chuva | Recurso máximo |
|----------------|------|-------|----------------|
| Aldeia         | 0.0  | 0.0   | 0.5            |
| Pesca          | 0.3  | 0.1   | 1.0            |
| Coleta         | 0.1  | 0.3   | 0.8            |
| Roçado         | 0.2  | 0.4   | 0.9            |
| Interditada    | 0.0  | 0.0   | 0.0            |

Fórmula de regeneração:
```
cell.resource += rate * (max_resource - cell.resource)
```

## Modelo de Paralelismo

### Decomposição MPI

A grade global é dividida em uma topologia cartesiana 2D (`MPI_Cart_create`). Cada rank MPI recebe um bloco contíguo da grade com uma borda de halo de 1 célula em cada lado para acesso aos vizinhos.

### Troca de halos

Comunicação não-bloqueante de 8 direções (N, S, E, W, NE, NW, SE, SW) usando `MPI_Isend`/`MPI_Irecv` seguidos de `MPI_Waitall`. Isto permite que a troca das 8 bordas se sobreponha.

### Processamento de agentes (OpenMP)

```c
#pragma omp parallel
{
    RngState rng = rng_seed(seed ^ thread_id);  // PRNG per-thread
    #pragma omp for schedule(dynamic, 32)
    for (int i = 0; i < count; i++) { ... }
}
```

- `schedule(dynamic, 32)`: balanceamento dinâmico para compensar variação de workload entre agentes
- PRNG per-thread: cada thread tem seu próprio estado RNG, evitando contenção

### Atualização da grade (OpenMP)

```c
#pragma omp parallel for collapse(2) schedule(static)
for (int r = 1; r <= local_h; r++)
    for (int c = 1; c <= local_w; c++) { ... }
```

- `collapse(2)`: achata os dois loops em um, melhorando a distribuição de trabalho
- `schedule(static)`: carga uniforme por célula, ideal para iteração regular

### Migração de agentes

Agentes que saem da partição local são migrados em duas fases:
1. `MPI_Alltoall` — cada rank informa quantos agentes enviará a cada outro rank
2. `MPI_Alltoallv` — transferência dos dados dos agentes com deslocamentos variáveis

### Métricas globais

Reduções coletivas via `MPI_Allreduce`:
- `total_resource` → `MPI_SUM`
- `alive_agents` → `MPI_SUM`
- `max_energy` → `MPI_MAX`
- `min_energy` → `MPI_MIN`
- `avg_energy` → soma das energias / total de vivos

## Benchmarks

### Como executar benchmarks

```bash
# Benchmark padrão (64x64, 100 ciclos, 50 agentes)
./scripts/benchmark.sh

# Benchmark com configuração maior
WIDTH=128 HEIGHT=128 AGENTS=200 CYCLES=50 ./scripts/benchmark.sh

# Análise dos resultados (usa o mais recente automaticamente)
./scripts/analyze.sh

# Análise de um resultado específico
./scripts/analyze.sh benchmark_results/<timestamp>/summary.csv
```

### Saída CSV

O modo `--csv` produz uma linha por ciclo com as seguintes colunas:

| Coluna          | Descrição                                        |
|-----------------|--------------------------------------------------|
| `cycle`         | Número do ciclo                                  |
| `season`        | Estação atual (dry/wet)                          |
| `compute_ms`    | Tempo de processamento de agentes + grid (ms)    |
| `halo_ms`       | Tempo de troca de halos (ms)                     |
| `migrate_ms`    | Tempo de migração de agentes (ms)                |
| `metrics_ms`    | Tempo de redução de métricas (ms)                |
| `cycle_ms`      | Tempo total do ciclo (ms)                        |
| `alive_agents`  | Agentes vivos                                    |
| `total_resource`| Recurso total na grade                           |
| `avg_energy`    | Energia média dos agentes                        |
| `load_balance`  | min_agents/max_agents entre ranks                |
| `comm_compute`  | Razão comunicação/computação                     |

### Resultados

#### Configuração pequena: 64×64, 100 ciclos, 50 agentes

| NP | Threads | Compute(ms) | Halo(ms) | Migrate(ms) | Cycle(ms) | Speedup | Eficiência |
|----|---------|-------------|----------|-------------|-----------|---------|------------|
| 1  | 1       | 41.95       | 0.004    | 0.008       | 41.99     | 1.00×   | 100%       |
| 1  | 2       | 26.83       | 0.004    | 0.009       | 26.88     | 1.56×   | 78.1%      |
| 1  | 4       | 28.32       | 0.004    | 0.009       | 28.37     | 1.48×   | 37.0%      |
| 2  | 1       | 23.22       | 0.018    | 2.505       | 23.27     | 1.80×   | 90.2%      |
| 2  | 2       | 22.81       | 0.011    | 2.472       | 22.86     | 1.84×   | 45.9%      |
| 4  | 1       | 13.67       | 0.010    | 5.374       | 13.71     | 3.06×   | 76.6%      |
| 4  | 2       | 13.02       | 0.012    | 3.194       | 13.06     | 3.22×   | 40.2%      |
| 4  | 4       | 13.04       | 0.013    | 3.001       | 13.08     | 3.21×   | 20.1%      |

#### Configuração grande: 128×128, 50 ciclos, 200 agentes

| NP | Threads | Compute(ms) | Halo(ms) | Migrate(ms) | Cycle(ms) | Speedup | Eficiência |
|----|---------|-------------|----------|-------------|-----------|---------|------------|
| 1  | 1       | 168.57      | 0.004    | 0.010       | 168.70    | 1.00×   | 100%       |
| 1  | 2       | 86.72       | 0.005    | 0.010       | 86.85     | 1.94×   | 97.1%      |
| 1  | 4       | 54.61       | 0.005    | 0.011       | 54.74     | 3.08×   | 77.0%      |
| 2  | 1       | 87.80       | 0.014    | 9.850       | 87.90     | 1.92×   | 96.0%      |
| 2  | 2       | 54.27       | 0.013    | 2.429       | 54.36     | 3.10×   | 77.6%      |
| 2  | 4       | 30.00       | 0.015    | 1.093       | 30.09     | 5.61×   | 70.1%      |
| 4  | 1       | 55.26       | 0.020    | 19.657      | 55.35     | 3.05×   | 76.2%      |
| 4  | 2       | 29.97       | 0.018    | 2.323       | 30.04     | 5.62×   | 70.2%      |
| 4  | 4       | 30.59       | 0.018    | 2.647       | 30.66     | 5.50×   | 34.4%      |

**Observações**: Com a configuração maior (mais carga computacional por agente), tanto MPI quanto OpenMP escalam melhor. A melhor configuração híbrida (NP=4, Threads=2) atinge speedup de 5.62× com eficiência de 70.2%. A migração de agentes (`MPI_Alltoallv`) é o principal custo de comunicação, especialmente com 4 ranks.

## Testes

```bash
make test          # roda testes unitários + MPI
make test-unit     # só unitários (sem MPI)
make test-mpi      # só integração MPI (4 ranks)
```
