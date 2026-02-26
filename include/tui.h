#ifndef TUI_H
#define TUI_H

#include "types.h"
#include "metrics.h"


typedef enum {
    TUI_RUNNING = 0,
    TUI_PAUSED  = 1,
    TUI_QUIT    = 2
} TuiState;

typedef struct {
    TuiState state;
    int      speed_ms;   /* atraso entre frames em milissegundos */
} TuiControl;

/*
 * Configura o terminal em modo raw/não-bloqueante para input interativo.
 * Deve ser chamado apenas no rank 0. Registra handler via atexit
 * para restaurar o terminal na saída.
 */
void tui_init_interactive(void);

/*
 * Restaura as configurações originais do terminal.
 * Seguro chamar múltiplas vezes.
 */
void tui_restore_terminal(void);

/*
 * Poll não-bloqueante de teclado no rank 0.
 * Atualiza ctrl->state e ctrl->speed_ms conforme teclas pressionadas.
 * Retorna 1 se foi solicitado passo único (tecla N), 0 caso contrário.
 */
int tui_poll_input(TuiControl *ctrl);

#ifdef USE_MPI
#include <mpi.h>

/*
 * Coleta todas as sub-grades no rank 0 e reordena de ordem-de-rank
 * para layout espacial (row-major) da grade global.
 *
 * Apenas rank 0 escreve em full_grid (deve ser pré-alocado com
 * global_w * global_h células). Demais ranks enviam células interiores.
 */
void tui_gather_grid(SubGrid *sg, Partition *p,
                     Cell *full_grid, int global_w, int global_h,
                     MPI_Comm comm);

/*
 * Coleta todos os agentes no rank 0.
 * No rank 0: *all_agents é alocado com malloc e deve ser liberado pelo caller.
 * Nos demais ranks: *all_agents é definido como NULL.
 */
void tui_gather_agents(Agent *local_agents, int local_count,
                       Agent **all_agents, int *total_count,
                       MPI_Comm comm);

#endif /* USE_MPI */

/*
 * Renderiza a grade global em stdout usando códigos ANSI.
 * Deve ser chamado apenas no rank 0.
 *
 * Esquema de cores:
 *   ALDEIA      → fundo magenta, 'A'
 *   PESCA       → fundo azul,    'P'
 *   COLETA      → fundo verde,   'C'
 *   ROCADO      → fundo amarelo, 'R'
 *   INTERDITADA → fundo vermelho,'X'
 *   Inacessível → cinza escuro '.'
 *   Agente      → amarelo brilhante '@'
 *
 * Intensidade do recurso controla brilho:
 *   > 0.66 * max → brilhante, > 0.33 * max → normal, senão → escuro
 *
 * Grades maiores que 80x40 são reduzidas por downsampling.
 */
void tui_render(Cell *full_grid, int global_w, int global_h,
                Agent *all_agents, int total_agents,
                int cycle, int total_cycles,
                Season season, SimMetrics *metrics,
                CyclePerf *perf, TuiControl *ctrl);

#endif /* TUI_H */
