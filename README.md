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

## Testes

```bash
make test          # roda testes unitários + MPI
make test-unit     # só unitários (sem MPI)
make test-mpi      # só integração MPI (4 ranks)
```
