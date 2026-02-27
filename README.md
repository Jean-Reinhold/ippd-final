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
| `-R THRESHOLD`   | Energia para reprodução           | 2.0     |
| `-r COST`        | Energia do filho                  | 1.0     |
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
- **Criar desbalanceamento de carga**: sem ela, cada agente custaria tempo constante, e qualquer estratégia de escalonamento teria desempenho idêntico. A carga variável é o que torna o uso de escalonamentos avançados, como `schedule(guided)`, necessário e mensurável nos benchmarks.

A carga é limitada por `max_workload` (padrão: 500.000 iterações) e usa `volatile` para impedir que o compilador elimine o loop como código morto.

## Funcionamento

A cada ciclo, a simulação executa 7 fases individualmente cronometradas:

1. **Estação + Acessibilidade** (`season_time`): rank 0 calcula estação, `MPI_Bcast` para todos, loop local de acessibilidade.
2. **Troca de halos** (`halo_time`): `MPI_Isend`/`MPI_Irecv` de 8 direções + `MPI_Waitall`.
3. **Workload sintético** (`workload_time`): busy-loop proporcional ao recurso da célula, OpenMP `schedule(guided, 8)`.
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

### Reprodução

Quando a energia de um agente ultrapassa `reproduce_threshold` (padrão 2.0), ele se reproduz: perde `reproduce_cost` (padrão 1.0) de energia e gera um filho na mesma posição com `energy = reproduce_cost`. Filhos não se reproduzem no mesmo ciclo. A reprodução aumenta a pressão competitiva sobre os recursos, criando dinâmicas de boom/bust ao invés de crescimento perpétuo de energia.

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
| Pesca          | 0.03 | 0.01  | 1.0            |
| Coleta         | 0.01 | 0.03  | 0.8            |
| Roçado         | 0.02 | 0.04  | 0.9            |
| Interditada    | 0.0  | 0.0   | 0.0            |

Fórmula de regeneração:
```
cell.resource += rate * (max_resource - cell.resource)
```

## Modelo de Paralelismo

Modelo híbrido MPI+OpenMP: MPI distribui a grade entre processos e OpenMP paraleliza agentes e atualização da grade dentro de cada processo.

### Decomposição MPI 2D Cartesiana

A grade é dividida em topologia cartesiana 2D (`MPI_Cart_create`), não-periódica. Cada rank recebe um bloco com halo de 1 célula. A decomposição 2D minimiza superfície de halo vs. 1D strips. O `partition_init` escolhe a fatoração de P que minimiza `|px - py|` para manter sub-grades aproximadamente quadradas.

### Troca de halos

8 direções (N, S, E, W, NE, NW, SE, SW) com `MPI_Isend`/`MPI_Irecv` + `MPI_Waitall`. Um `MPI_Datatype` customizado mapeia a struct `Cell` diretamente. Bordas E/W são empacotadas manualmente (`pack_column`/`unpack_column`) por não serem contíguas em row-major.

### Processamento de agentes — OpenMP `guided`

O processamento é dividido em duas funções independentemente cronometradas:

1. **`agents_workload`** — busy-loop sintético proporcional ao recurso da célula. Utilizamos `schedule(guided, 8)` porque a carga varia de 0 a 500k iterações por agente e o escalonamento `static` deixaria as threads severamente desbalanceadas.
2. **`agents_decide_all`** — lógica de decisão com PRNG per-thread (`seed ^ (tid * 2654435761)`). Garante determinismo e independência entre threads.

**Otimização do Escalonamento:** A escolha por `guided, 8` otimiza o balanceamento de carga. A diretiva `guided` inicia entregando blocos (chunks) grandes para as threads e diminui o tamanho exponencialmente até o limite mínimo de 8. Isso reduz significativamente o overhead do escalonador em comparação com o modelo `dynamic`, garantindo ao mesmo tempo que as threads não fiquem ociosas (starvation) na reta final da execução do laço.

### Atualização da grade — `collapse(2) static`

`collapse(2)` transforma o espaço de iteração de `local_h` para `local_h × local_w`, evitando threads ociosas quando `local_h < nthreads`. `static` porque cada célula tem custo idêntico.

### Migração — `MPI_Alltoallv`

Duas fases: (1) `MPI_Alltoall` de contagens, (2) `MPI_Alltoallv` de dados. O array local é compactado in-place após marcar migrantes e cresce com `realloc` amortizado.

### Métricas — `MPI_Allreduce`

- `total_resource`, `alive_agents` → `MPI_SUM`
- `max_energy` → `MPI_MAX`, `min_energy` → `MPI_MIN` (sentinela `DBL_MAX`)
- `avg_energy` → soma local / total global de vivos

Os 9 campos de timing do `CyclePerf` são contíguos em memória, permitindo um único `MPI_Reduce` com `MPI_MAX` para obter os tempos do rank gargalo.

## Benchmarks

### Como executar

```bash
# Benchmark padrão (3 tamanhos, 3 execuções por config, 5 ciclos de warmup)
./scripts/benchmark.sh

# Configuração customizada
SIZES="64x64:50:100 128x128:200:100" NP_LIST="1 2 4" THREAD_LIST="1 2 4" RUNS=5 ./scripts/benchmark.sh

# Análise (usa o resultado mais recente)
./scripts/analyze.sh
```

O benchmark varia NP × Threads × tamanho do problema, faz `RUNS` execuções por configuração e exclui os primeiros `WARMUP` ciclos das médias. O `analyze.sh` gera tabelas de speedup por fase, fração serial de Karp-Flatt, comparação Amdahl vs Gustafson e decomposição de overhead de comunicação.

### Saída CSV

O modo `--csv` produz 16 colunas por ciclo:

| Coluna          | Descrição                                        |
|-----------------|--------------------------------------------------|
| `cycle`         | Número do ciclo                                  |
| `season`        | Estação atual (dry/wet)                          |
| `season_ms`     | Broadcast da estação + acessibilidade (ms)       |
| `halo_ms`       | Troca de halos (ms)                              |
| `workload_ms`   | Carga sintética / busy-loop (ms)                 |
| `agent_ms`      | Decisão dos agentes (ms)                         |
| `grid_ms`       | Regeneração da grade (ms)                        |
| `migrate_ms`    | Migração de agentes (ms)                         |
| `metrics_ms`    | Redução de métricas (ms)                         |
| `cycle_ms`      | Tempo total do ciclo (ms)                        |
| `total_agents`  | Agentes vivos                                    |
| `total_resource`| Recurso total na grade                           |
| `avg_energy`    | Energia média dos agentes                        |
| `load_balance`  | min_agents/max_agents entre ranks                |
| `workload_pct`  | % do ciclo gasto em workload                     |
| `comm_pct`      | % do ciclo gasto em comunicação                  |

### Análise dos Resultados

A instrumentação granular (7 fases por ciclo) permite identificar exatamente onde o tempo é gasto. Os principais achados:

**Workload domina compute (~99%)** — a carga sintética `(workload_compute)` é responsável por quase todo o tempo de ciclo no baseline serial. Isso valida o design e as otimizações: a escolha fina da política de escalonamento, comprovada pela troca para `schedule(guided, 8)`, é essencial para extrair desempenho e reduzir o overhead, sendo seu impacto diretamente mensurável.

**Workload escala bem com threads** — por ser embaraçosamente paralelo (sem estado compartilhado), o workload atinge ~1.95× com 2 threads e ~3.6× com 4 threads.

**Migração é o gargalo de comunicação, não halos** — a troca de halos é consistentemente barata (<0.3% do ciclo) graças à comunicação não-bloqueante. Já a migração (`MPI_Alltoallv`) cresce significativamente com o número de ranks: mais fronteiras de partição = mais agentes cruzando bordas. Threads adicionais reduzem esse custo indiretamente (processam agentes mais rápido, reduzindo migrantes pendentes).

**Fração serial de Karp-Flatt permanece baixa** — a fração serial experimentalmente determinada fica <4%, indicando que o overhead não cresce muito com o número de cores. O design de comunicação está bem dimensionado.

**Gustafson é mais apropriado que Amdahl** — a fração serial computacional é minúscula (~0.1%), então o limite de Amdahl é irrelevante. Na prática, o limitador é o overhead de comunicação, que dilui com problemas maiores (Gustafson).

**O ponto ideal é híbrido** — configurações como NP=2×4 ou NP=4×2 equilibram distribuição de dados, paralelismo intra-nó e overhead de comunicação. Para tabelas detalhadas com speedup por fase, Karp-Flatt e decomposição de comunicação, execute `./scripts/analyze.sh`.

## Testes

```bash
make test          # roda testes unitários + MPI
make test-unit     # só unitários (sem MPI)
make test-mpi      # só integração MPI (4 ranks)
```
