#ifndef TUI_H
#define TUI_H

#include "types.h"
#include "metrics.h"

/* ── Interactive TUI control ── */

typedef enum {
    TUI_RUNNING = 0,
    TUI_PAUSED  = 1,
    TUI_QUIT    = 2
} TuiState;

typedef struct {
    TuiState state;
    int      speed_ms;   /* delay between frames in milliseconds */
} TuiControl;

/*
 * Set terminal to raw/non-blocking mode for interactive input.
 * Must be called only on rank 0.  Registers atexit handler
 * so the terminal is always restored on exit.
 */
void tui_init_interactive(void);

/*
 * Restore the original terminal settings.
 * Safe to call multiple times.
 */
void tui_restore_terminal(void);

/*
 * Non-blocking poll for keyboard input on rank 0.
 * Updates ctrl->state and ctrl->speed_ms based on keypresses.
 * Returns 1 if a single-step was requested (N key), 0 otherwise.
 */
int tui_poll_input(TuiControl *ctrl);

#ifdef USE_MPI
#include <mpi.h>

/*
 * Gather all subgrids to rank 0 and reorder from rank-order into
 * the spatial (row-major) global grid layout.
 *
 * Only rank 0 writes into full_grid (must be pre-allocated to
 * global_w * global_h cells).  Other ranks send their interior cells.
 */
void tui_gather_grid(SubGrid *sg, Partition *p,
                     Cell *full_grid, int global_w, int global_h,
                     MPI_Comm comm);

/*
 * Gather all agents to rank 0.
 * On rank 0: *all_agents is malloc'd and must be freed by the caller.
 * On other ranks: *all_agents is set to NULL.
 */
void tui_gather_agents(Agent *local_agents, int local_count,
                       Agent **all_agents, int *total_count,
                       MPI_Comm comm);

#endif /* USE_MPI */

/*
 * Render the global grid to stdout using ANSI escape codes.
 * Should only be called on rank 0.
 *
 * Color scheme:
 *   ALDEIA      → magenta background, 'A'
 *   PESCA       → blue background,    'P'
 *   COLETA      → green background,   'C'
 *   ROCADO      → yellow background,  'R'
 *   INTERDITADA → red background,     'X'
 *   Inaccessible → dark gray '.'
 *   Agent present → bright yellow '@'
 *
 * Resource intensity controls text brightness:
 *   > 0.66 * max → bright, > 0.33 * max → normal, else → dim
 *
 * If grid exceeds 80×40, downsampling is applied.
 */
void tui_render(Cell *full_grid, int global_w, int global_h,
                Agent *all_agents, int total_agents,
                int cycle, int total_cycles,
                Season season, SimMetrics *metrics,
                CyclePerf *perf, TuiControl *ctrl);

#endif /* TUI_H */
