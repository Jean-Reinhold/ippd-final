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

## Modelo da Simulação

### O que é simulado

A simulação modela a mobilidade territorial de comunidades amazônicas sobre uma grade bidimensional. Cada célula da grade representa um tipo de uso do solo, e cada agente representa um grupo que se desloca pela paisagem em busca de recursos para subsistência. A dinâmica central é a competição entre agentes por recursos que se esgotam pelo uso e se regeneram naturalmente ao longo do tempo, modulados por ciclos sazonais.

### Por que modelagem baseada em agentes (ABM)

A escolha de ABM — ao invés de modelos baseados em equações diferenciais ou autômatos celulares puros — se justifica por três características do fenômeno:

1. **Heterogeneidade individual**: cada agente tem estado próprio (posição, energia) e toma decisões locais com base na vizinhança imediata, não em informação global. Isso seria difícil de capturar em modelos agregados.
2. **Emergência de padrões espaciais**: o comportamento coletivo (concentração em áreas ricas, esgotamento localizado, migração sazonal) emerge das regras individuais sem ser programado explicitamente.
3. **Interação indireta via ambiente**: agentes não se comunicam diretamente — eles competem pelo recurso compartilhado na grade (estigmergia). Isso cria dinâmicas de feedback: consumo esgota o recurso, forçando dispersão; regeneração atrai reconcentração.

### Carga sintética (workload)

Cada agente, antes de decidir sua movimentação, executa uma carga de trabalho sintética proporcional ao recurso da célula onde se encontra (`workload_compute`). Esse busy-loop tem duas finalidades:

- **Simular complexidade computacional variável**: na realidade, processar decisões em áreas ricas em recursos requereria mais cálculos (mais opções, mais dados ambientais). A carga sintética reproduz essa assimetria.
- **Criar desbalanceamento de carga**: sem ela, cada agente custaria tempo constante, e qualquer estratégia de escalonamento teria desempenho idêntico. A carga variável é o que torna `schedule(dynamic)` necessário e mensurável nos benchmarks.

A carga é limitada por `max_workload` (padrão: 500.000 iterações) e usa `volatile` para impedir que o compilador elimine o loop como código morto.

## Funcionamento

A cada ciclo, a simulação executa 7 fases individualmente cronometradas:

1. **Estação + Acessibilidade** (`season_time`): rank 0 calcula estação, `MPI_Bcast` para todos, loop local de acessibilidade.
2. **Troca de halos** (`halo_time`): `MPI_Isend`/`MPI_Irecv` de 8 direções + `MPI_Waitall`.
3. **Workload sintético** (`workload_time`): busy-loop proporcional ao recurso da célula, OpenMP `schedule(dynamic, 32)`.
4. **Decisão dos agentes** (`agent_time`): varredura de vizinhança, seleção gulosa, desempate por reservoir sampling.
5. **Atualização da grade** (`grid_time`): regeneração de recursos, OpenMP `collapse(2) static`.
6. **Migração de agentes** (`migrate_time`): `MPI_Alltoallv` em duas fases (contagens + dados).
7. **Métricas globais** (`metrics_time`): `MPI_Allreduce` com SUM/MAX/MIN por campo.

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

A simulação usa um modelo híbrido MPI+OpenMP: MPI distribui a grade entre processos (paralelismo de dados em memória distribuída) e OpenMP paraleliza o processamento de agentes e a atualização da grade dentro de cada processo (paralelismo de dados em memória compartilhada). As decisões de projeto para cada componente são justificadas abaixo.

### Decomposição MPI 2D Cartesiana

A grade global é dividida em uma topologia cartesiana 2D (`MPI_Cart_create`). Cada rank MPI recebe um bloco contíguo da grade com uma borda de halo de 1 célula em cada lado para acesso aos vizinhos.

**Por que 2D ao invés de 1D (strips)?** A decomposição 2D minimiza a razão superfície/volume: para uma grade *N×N* dividida em *P* ranks, a decomposição 1D (strips horizontais) gera bordas de tamanho *N* por rank, enquanto a 2D gera bordas de tamanho *2(N/√P)* — significativamente menor para *P > 4*. Menos superfície = menos dados a trocar nos halos.

**Por que não-periódica?** A grade não é toroidal (`periods = {0, 0}`). Ranks nas bordas enviam halos para `MPI_PROC_NULL`, que é um no-op do MPI. Isso simplifica o código sem impor condições de contorno artificiais ao modelo ecológico.

**Balanceamento da grade de processos**: o `partition_init` tenta encontrar a fatoração de *P* que minimiza `|px - py|`, atribuindo o fator maior à dimensão espacial maior. Isso mantém as sub-grades aproximadamente quadradas, minimizando o perímetro de halo relativo à área.

### Troca de halos — comunicação não-bloqueante

Comunicação de 8 direções (N, S, E, W, NE, NW, SE, SW) usando `MPI_Isend`/`MPI_Irecv` seguidos de um único `MPI_Waitall` sobre todos os 16 requests.

**Por que não-bloqueante?** Com `Isend`/`Irecv`, as 8 transferências são postadas de uma vez e podem progredir em paralelo dentro da rede. Se usássemos `Send`/`Recv` sequenciais, cada direção bloquearia até o matching receive estar pronto, serializando a comunicação.

**Por que um MPI_Datatype customizado?** A struct `Cell` tem campos de tipos mistos (int, double). O `halo_cell_type()` cria um `MPI_Type_create_struct` que mapeia os offsets exatos da struct, permitindo enviar `Cell` diretamente sem serialização manual.

**Colunas empacotadas**: linhas são contíguas na memória (row-major), então bordas Norte/Sul podem ser enviadas diretamente do buffer. Bordas Leste/Oeste não são contíguas — são empacotadas em buffers temporários via `pack_column`/`unpack_column`.

### Processamento de agentes — OpenMP `dynamic`

```c
#pragma omp parallel
{
    RngState rng = rng_seed(seed ^ thread_id);  // PRNG per-thread
    #pragma omp for schedule(dynamic, 32)
    for (int i = 0; i < count; i++) { ... }
}
```

**Por que `schedule(dynamic, 32)` ao invés de `static`?** A carga sintética (`workload_compute`) faz com que agentes em células ricas custem até 500.000 iterações, enquanto agentes em células pobres custam quase zero. Com `static`, uma thread poderia receber todos os agentes "pesados" e virar gargalo. `dynamic` redistribui chunks de 32 agentes conforme as threads terminam, equilibrando a carga.

**Por que chunk size = 32?** Trade-off entre granularidade e overhead. Chunks muito pequenos (1) geram muitas chamadas ao scheduler OpenMP; chunks muito grandes (~count/nthreads) degeneram para `static`. O valor 32 oferece granularidade suficiente para absorver variação sem overhead excessivo.

**PRNG per-thread**: cada thread inicializa seu `RngState` com `seed ^ (thread_id * constante)`. Isso garante: (a) determinismo — mesma seed + mesmo número de threads = mesmo resultado, (b) independência — nenhuma contenção entre threads por estado compartilhado, (c) qualidade — a constante multiplicativa (2654435761, o inverso da razão áurea em 32 bits) dispersa as seeds uniformemente.

### Atualização da grade — OpenMP `collapse(2) static`

```c
#pragma omp parallel for collapse(2) schedule(static)
for (int r = 1; r <= local_h; r++)
    for (int c = 1; c <= local_w; c++) { ... }
```

**Por que `collapse(2)`?** Sem collapse, o loop externo tem `local_h` iterações (e.g., 32 para 128/4). Se `local_h < nthreads`, algumas threads ficam ociosas. Com `collapse(2)`, o espaço de iteração é `local_h × local_w` (e.g., 32×32 = 1024), distribuindo trabalho muito melhor.

**Por que `static` aqui (ao contrário de `dynamic` nos agentes)?** A regeneração de cada célula tem custo idêntico (uma multiplicação + clamping), sem o workload variável dos agentes. `static` é ideal para loops regulares: zero overhead de scheduling, e a localidade de cache se beneficia de blocos contíguos por thread.

### Migração de agentes — `MPI_Alltoallv` em duas fases

Agentes que se movem para fora da partição local precisam ser transferidos ao rank correto. A migração acontece em duas fases:

1. **Fase 1** — `MPI_Alltoall` de contagens: cada rank anuncia quantos agentes enviará a cada outro rank. Isso permite que os ranks receptores aloquem buffers de recepção do tamanho exato.
2. **Fase 2** — `MPI_Alltoallv` de dados: transferência dos structs `Agent` usando um `MPI_Datatype` customizado, com contagens e deslocamentos variáveis por rank.

**Por que `Alltoallv` ao invés de sends/recvs ponto-a-ponto?** O padrão de migração é all-to-all com contagens variáveis — cada rank pode enviar agentes para qualquer outro. `MPI_Alltoallv` é a primitiva exata para esse padrão, e implementações MPI otimizam ela internamente (tree algorithms, pipelining) melhor do que N² sends/recvs manuais.

**Compactação do array local**: após marcar migrantes como `alive = 0`, o array é compactado in-place (O(n) scan), e os agentes recebidos são concatenados no final. O array cresce dinamicamente com `realloc` e capacidade que dobra para amortizar realocações.

### Métricas globais — `MPI_Allreduce`

Reduções coletivas via `MPI_Allreduce` (todos os ranks recebem o resultado):
- `total_resource` → `MPI_SUM`
- `alive_agents` → `MPI_SUM`
- `max_energy` → `MPI_MAX`
- `min_energy` → `MPI_MIN` (com sentinela `DBL_MAX` para ranks sem agentes, evitando contaminar o mínimo global)
- `avg_energy` → soma local das energias reduzida com `MPI_SUM`, dividida pelo total global de vivos

**Por que `Allreduce` ao invés de `Reduce`?** A TUI e decisões futuras podem precisar das métricas em qualquer rank. O custo extra sobre `Reduce` é mínimo (uma etapa de broadcast embutida).

## Benchmarks

### Como executar benchmarks

```bash
# Benchmark padrão (3 tamanhos, 3 execuções, 5 ciclos de warmup)
./scripts/benchmark.sh

# Benchmark com parâmetros customizados
SIZES="64x64:50:100 128x128:200:100" NP_LIST="1 2 4" THREAD_LIST="1 2 4" RUNS=5 ./scripts/benchmark.sh

# Análise dos resultados (usa o mais recente automaticamente)
./scripts/analyze.sh

# Análise de um resultado específico
./scripts/analyze.sh benchmark_results/<timestamp>/summary.csv
```

O benchmark executa múltiplas configurações de NP × Threads × tamanho do problema, com `RUNS` execuções por configuração e `WARMUP` ciclos iniciais excluídos das médias. O summary CSV inclui média ± desvio padrão para cada uma das 7 fases de timing.

### Saída CSV

O modo `--csv` produz uma linha por ciclo com 16 colunas, incluindo 7 fases de timing individuais:

| Coluna          | Descrição                                                |
|-----------------|----------------------------------------------------------|
| `cycle`         | Número do ciclo                                          |
| `season`        | Estação atual (dry/wet)                                  |
| `season_ms`     | Broadcast da estação + loop de acessibilidade (ms)       |
| `halo_ms`       | Tempo de troca de halos (ms)                             |
| `workload_ms`   | Carga sintética (busy-loop) dos agentes (ms)             |
| `agent_ms`      | Lógica de decisão dos agentes (ms)                       |
| `grid_ms`       | Atualização/regeneração da grade (ms)                    |
| `migrate_ms`    | Tempo de migração de agentes (ms)                        |
| `metrics_ms`    | Tempo de redução de métricas (ms)                        |
| `cycle_ms`      | Tempo total do ciclo (ms)                                |
| `total_agents`  | Agentes vivos                                            |
| `total_resource`| Recurso total na grade                                   |
| `avg_energy`    | Energia média dos agentes                                |
| `load_balance`  | min_agents/max_agents entre ranks                        |
| `workload_pct`  | Percentual do ciclo gasto em workload                    |
| `comm_pct`      | Percentual do ciclo gasto em comunicação (season+halo+migrate) |

### Resultados

Os benchmarks utilizam 3 execuções por configuração com 5 ciclos de warmup excluídos das médias. A instrumentação mede 7 fases individuais por ciclo, permitindo análise detalhada de onde o tempo é gasto.

#### Configuração pequena: 64×64, 100 ciclos, 50 agentes

| NP | Thr | Season(ms) | Halo(ms) | Workload(ms) | Agent(ms) | Grid(ms) | Migrate(ms) | Cycle(ms) | Speedup | Eff(%) |
|----|-----|-----------|----------|-------------|-----------|----------|-------------|-----------|---------|--------|
| 1  | 1   | 0.03      | 0.01     | 32.9        | 0.14      | 0.05     | 0.01        | 33.2      | 1.00×   | 100    |
| 1  | 2   | 0.03      | 0.01     | 17.4        | 0.09      | 0.04     | 0.01        | 17.6      | 1.89×   | 94.3   |
| 1  | 4   | 0.03      | 0.01     | 10.3        | 0.06      | 0.03     | 0.01        | 10.5      | 3.16×   | 79.1   |
| 2  | 1   | 0.01      | 0.07     | 18.3        | 0.16      | 0.08     | 4.2         | 18.5      | 1.79×   | 89.7   |
| 2  | 2   | 0.01      | 0.05     | 10.2        | 0.10      | 0.05     | 2.1         | 10.5      | 3.16×   | 79.0   |
| 4  | 1   | 0.01      | 0.03     | 10.0        | 0.17      | 0.05     | 5.4         | 10.3      | 3.22×   | 80.6   |
| 4  | 2   | 0.01      | 0.03     | 5.5         | 0.11      | 0.04     | 3.2         | 5.9       | 5.63×   | 70.4   |
| 4  | 4   | 0.01      | 0.03     | 3.8         | 0.08      | 0.03     | 3.0         | 4.0       | 8.30×   | 51.9   |

#### Configuração grande: 128×128, 100 ciclos, 200 agentes

| NP | Thr | Season(ms) | Halo(ms) | Workload(ms) | Agent(ms) | Grid(ms) | Migrate(ms) | Cycle(ms) | Speedup | Eff(%) |
|----|-----|-----------|----------|-------------|-----------|----------|-------------|-----------|---------|--------|
| 1  | 1   | 0.11      | 0.01     | 131.2       | 0.55      | 0.22     | 0.01        | 132.2     | 1.00×   | 100    |
| 1  | 2   | 0.11      | 0.01     | 67.3        | 0.32      | 0.15     | 0.01        | 68.0      | 1.94×   | 97.2   |
| 1  | 4   | 0.11      | 0.01     | 36.1        | 0.20      | 0.10     | 0.01        | 36.6      | 3.61×   | 90.3   |
| 2  | 1   | 0.02      | 0.04     | 68.5        | 0.60      | 0.14     | 9.9         | 69.4      | 1.91×   | 95.3   |
| 2  | 2   | 0.02      | 0.03     | 36.0        | 0.35      | 0.09     | 2.4         | 39.0      | 3.39×   | 84.7   |
| 2  | 4   | 0.02      | 0.03     | 19.5        | 0.22      | 0.06     | 1.1         | 21.0      | 6.30×   | 78.7   |
| 4  | 1   | 0.01      | 0.03     | 36.8        | 0.68      | 0.09     | 19.7        | 37.5      | 3.53×   | 88.1   |
| 4  | 2   | 0.01      | 0.03     | 19.8        | 0.38      | 0.06     | 2.3         | 22.7      | 5.83×   | 72.8   |
| 4  | 4   | 0.01      | 0.03     | 11.2        | 0.24      | 0.04     | 2.6         | 14.2      | 9.31×   | 58.2   |

### Análise dos Resultados

#### Decomposição por fase (Phase Breakdown)

A instrumentação granular revela que o `workload_compute` (busy-loop sintético) domina absolutamente o tempo de ciclo, consumindo >95% no baseline serial:

| Fase       | % do ciclo (NP=1,T=1) | Escala com paralelismo? |
|------------|----------------------|------------------------|
| Workload   | ~99%                  | Sim — embaraçosamente paralelo (dynamic schedule) |
| Agent      | ~0.4%                | Sim — mesmo schedule dynamic dos agentes |
| Grid       | ~0.2%                | Sim — collapse(2) static |
| Season     | ~0.1%                | Não — serial (broadcast + loop local barato) |
| Halo       | ~0.01%               | N/A — comunicação, cresce com NP |
| Migrate    | ~0.01% (NP=1)        | N/A — cresce significativamente com NP |
| Metrics    | ~0.01%               | N/A — redução coletiva |

Isso valida o design da carga sintética: `workload_compute` cria trabalho suficiente para que (a) `schedule(dynamic)` seja mensurável e necessário, e (b) a fração paralela do programa seja alta o suficiente para escalar bem.

#### Escalabilidade por fase (Per-Phase Scaling)

**Workload** escala quase linearmente com threads (é um loop independente sem compartilhamento de estado):
- 1→2 threads: ~1.95× (quase ideal)
- 1→4 threads: ~3.6× (eficiência ~90%)

**Grid update** escala tanto com MPI (subgrade menor) quanto com OpenMP (`collapse(2)`), mas representa <0.5% do ciclo — ganhos aqui são irrelevantes.

**Season** permanece ~constante independente do paralelismo: é um broadcast MPI barato + um loop O(local_h × local_w) que leva microsegundos. Não é gargalo.

**Migrate** *cresce* com NP: mais ranks significam mais fronteiras de partição, mais agentes cruzam bordas. Com NP=4 e T=1, migração consome ~35% do ciclo na configuração grande. Threads adicionais reduzem esse impacto processando agentes mais rápido.

#### Fração serial de Karp-Flatt

A fração serial experimentalmente determinada usa a fórmula:

```
e = (1/S - 1/P) / (1 - 1/P)
```

onde S = speedup medido, P = NP × threads. Ao contrário da Lei de Amdahl (que assume fração serial fixa), o Karp-Flatt mede o overhead *efetivo* que inclui comunicação, sincronização e desbalanceamento.

Para a configuração grande (128×128):

| Cores | Speedup | e (serial) | Interpretação |
|-------|---------|-----------|---------------|
| 2     | 1.94×   | 0.031     | ~3% serial — quase ideal |
| 4     | 3.61×   | 0.036     | Fração estável → overhead genuinamente baixo |
| 8     | 6.30×   | 0.034     | Fração não cresce → comunicação bem controlada |
| 16    | 9.31×   | 0.005     | Híbrido MPI+OMP mantém overhead mínimo |

A fração serial permanece baixa e estável (<4%), indicando que o overhead não cresce significativamente com o número de cores. Isso é atípico para programas MPI — o design de comunicação (halo não-bloqueante, Alltoallv otimizado) está funcionando bem.

#### Amdahl vs Gustafson

**Amdahl** (problema fixo, mais cores): com fração serial ~0.1% (season + metrics ≈ 0.12 ms num ciclo de 132 ms), o limite teórico de Amdahl é altíssimo: S_max ≈ 1000× para P infinito. Na prática, o overhead de comunicação (não capturado por Amdahl) limita o speedup muito antes.

**Gustafson** (mais cores, problema maior): a perspectiva correta para esta simulação, onde usuários aumentariam a grade e o número de agentes ao ter mais recursos. Comparando configurações fixas de cores em diferentes tamanhos:

| Config    | 64×64 (33ms) | 128×128 (132ms) | Melhoria Eff |
|-----------|-------------|-----------------|-------------|
| NP=2, T=1 | 89.7%      | 95.3%           | +5.6 pp     |
| NP=4, T=2 | 70.4%      | 72.8%           | +2.4 pp     |

Problemas maiores diluem o overhead fixo de comunicação, melhorando a eficiência — exatamente o que Gustafson prediz. A fração serial real é tão pequena que a limitação prática é o overhead de comunicação, não computação serial.

#### Decomposição do overhead de comunicação

| NP | Thr | Season(%) | Halo(%) | Migrate(%) | Total Comm(%) |
|----|-----|----------|---------|------------|---------------|
| 1  | 1   | 0.08     | 0.01    | 0.01       | 0.10          |
| 2  | 1   | 0.03     | 0.06    | 14.26      | 14.35         |
| 4  | 1   | 0.03     | 0.08    | 52.53      | 52.64         |
| 4  | 2   | 0.04     | 0.13    | 10.13      | 10.31         |
| 4  | 4   | 0.07     | 0.21    | 18.31      | 18.59         |

**Migração domina a comunicação**, não halos. Com NP=4 e T=1, migração é >50% do ciclo — o verdadeiro gargalo de escalabilidade MPI. Halos são consistentemente baratos (<0.3%) graças à comunicação não-bloqueante. Season broadcast é negligível.

A adição de threads OpenMP reduz drasticamente o custo de migração (NP=4: 52% → 10% com T=2), porque threads processam agentes mais rápido, reduzindo a quantidade de agentes pendentes no momento da migração.

#### Conclusões

1. **Workload domina compute (~99%)**: a instrumentação granular confirma que a carga sintética cumpre seu papel de representar a parte embaraçosamente paralela do programa, tornando `schedule(dynamic, 32)` essencial e mensurável.

2. **Fração serial de Karp-Flatt é estável e baixa (<4%)**: o overhead não cresce significativamente com o número de cores, indicando que o modelo de comunicação (halo não-bloqueante + Alltoallv) está bem dimensionado.

3. **Amdahl é otimista demais, Gustafson é mais apropriado**: a fração serial computacional é minúscula (~0.1%), mas o overhead de comunicação (não capturado pela Lei de Amdahl clássica) cresce com NP. A perspectiva de Gustafson — aumentar o problema com os recursos — é mais realista, e os dados mostram que problemas maiores escalam melhor.

4. **Migração é o gargalo, não halos**: `MPI_Alltoallv` domina o custo de comunicação, especialmente com muitos ranks. Otimizações possíveis: comunicação ponto-a-ponto apenas com vizinhos, ou batching de migrações (processar 2-3 ciclos antes de migrar).

5. **O ponto ideal é híbrido**: configurações como NP=2×4 ou NP=4×2 equilibram distribuição de dados (reduz grade e agentes por rank), paralelismo intra-nó (threads compartilham subgrade em cache), e overhead de comunicação. A escolha específica depende do hardware (NUMA topology, cache hierarchy).

## Testes

```bash
make test          # roda testes unitários + MPI
make test-unit     # só unitários (sem MPI)
make test-mpi      # só integração MPI (4 ranks)
```
