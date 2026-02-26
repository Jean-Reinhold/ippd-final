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

**Workload domina compute (~99%)** — a carga sintética (`workload_compute`) é responsável por quase todo o tempo de ciclo no baseline serial. Isso valida o design: o `schedule(dynamic, 32)` é essencial e mensurável.

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
