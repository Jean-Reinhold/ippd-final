#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

/* ── ANSI escape helpers ── */
#define ANSI_RESET   "\033[0m"
#define ANSI_CLEAR   "\033[2J\033[H"
#define ANSI_BOLD    "\033[1m"
#define ANSI_DIM     "\033[2m"

/* Background colors for cell types */
#define BG_MAGENTA   "\033[45m"   /* ALDEIA      */
#define BG_BLUE      "\033[44m"   /* PESCA       */
#define BG_GREEN     "\033[42m"   /* COLETA      */
#define BG_YELLOW    "\033[43m"   /* ROCADO      */
#define BG_RED       "\033[41m"   /* INTERDITADA */
#define BG_DARKGRAY  "\033[100m"  /* inaccessible */

/* Text colors */
#define FG_BRIGHT_YELLOW "\033[93m"
#define FG_WHITE         "\033[97m"
#define FG_GRAY          "\033[90m"
#define FG_BLACK         "\033[30m"

/* Maximum display dimensions before downsampling */
#define MAX_DISPLAY_W 80
#define MAX_DISPLAY_H 40

/* Speed limits for interactive control */
#define SPEED_MIN_MS  10
#define SPEED_MAX_MS  2000
#define SPEED_STEP_MS 25

/* ── Interactive terminal state ── */
static struct termios tui_orig_termios;
static int            tui_raw_mode_active = 0;

void tui_restore_terminal(void) {
    if (tui_raw_mode_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &tui_orig_termios);
        tui_raw_mode_active = 0;
    }
}

void tui_init_interactive(void) {
    if (tui_raw_mode_active) return;

    /* If stdin isn't a real terminal (e.g. mpirun without --stdin),
     * skip raw mode — interactive keys won't work but rendering still will. */
    if (!isatty(STDIN_FILENO)) return;

    tcgetattr(STDIN_FILENO, &tui_orig_termios);
    atexit(tui_restore_terminal);

    struct termios raw = tui_orig_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;   /* non-blocking */
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    tui_raw_mode_active = 1;
}

int tui_poll_input(TuiControl *ctrl) {
    char ch;
    if (read(STDIN_FILENO, &ch, 1) != 1) return 0;

    switch (ch) {
        case ' ':
            ctrl->state = (ctrl->state == TUI_RUNNING)
                          ? TUI_PAUSED : TUI_RUNNING;
            break;
        case 'n': case 'N':
            ctrl->state = TUI_PAUSED;
            return 1;  /* step requested */
        case '+': case '=':
            ctrl->speed_ms += SPEED_STEP_MS;
            if (ctrl->speed_ms > SPEED_MAX_MS)
                ctrl->speed_ms = SPEED_MAX_MS;
            break;
        case '-':
            ctrl->speed_ms -= SPEED_STEP_MS;
            if (ctrl->speed_ms < SPEED_MIN_MS)
                ctrl->speed_ms = SPEED_MIN_MS;
            break;
        case 'q': case 'Q':
            ctrl->state = TUI_QUIT;
            break;
        default:
            break;
    }
    return 0;
}

/* ── Season name helper ── */
static const char *season_name(Season s) {
    return s == DRY ? "DRY" : "WET";
}

/* ── Background color for a cell type ── */
static const char *cell_bg(CellType t) {
    switch (t) {
        case ALDEIA:      return BG_MAGENTA;
        case PESCA:       return BG_BLUE;
        case COLETA:      return BG_GREEN;
        case ROCADO:      return BG_YELLOW;
        case INTERDITADA: return BG_RED;
        default:          return "";
    }
}

/* ── Cell letter ── */
static char cell_char(CellType t) {
    switch (t) {
        case ALDEIA:      return 'A';
        case PESCA:       return 'P';
        case COLETA:      return 'C';
        case ROCADO:      return 'R';
        case INTERDITADA: return 'X';
        default:          return '?';
    }
}

/* ── Intensity prefix based on resource level ── */
static const char *intensity_code(double resource, double max_resource) {
    if (max_resource <= 0.0) return ANSI_DIM;
    double ratio = resource / max_resource;
    if (ratio > 0.66) return ANSI_BOLD;
    if (ratio > 0.33) return "";  /* normal */
    return ANSI_DIM;
}

/* ── Render ── */
void tui_render(Cell *full_grid, int global_w, int global_h,
                Agent *all_agents, int total_agents,
                int cycle, Season season, SimMetrics *metrics,
                TuiControl *ctrl)
{
    /* Compute downsampling step if grid is too large */
    int step_x = 1, step_y = 1;
    if (global_w > MAX_DISPLAY_W) step_x = (global_w + MAX_DISPLAY_W - 1) / MAX_DISPLAY_W;
    if (global_h > MAX_DISPLAY_H) step_y = (global_h + MAX_DISPLAY_H - 1) / MAX_DISPLAY_H;

    int display_w = (global_w + step_x - 1) / step_x;
    int display_h = (global_h + step_y - 1) / step_y;

    /*
     * Build an agent presence map for O(1) lookup.
     * agent_map[gy * global_w + gx] = 1 if an agent is at (gx, gy).
     */
    int *agent_map = calloc((size_t)global_w * (size_t)global_h, sizeof(int));
    if (agent_map) {
        for (int i = 0; i < total_agents; i++) {
            if (!all_agents[i].alive) continue;
            int gx = all_agents[i].gx;
            int gy = all_agents[i].gy;
            if (gx >= 0 && gx < global_w && gy >= 0 && gy < global_h)
                agent_map[gy * global_w + gx] = 1;
        }
    }

    /* Clear screen */
    printf(ANSI_CLEAR);

    /* Header */
    printf(ANSI_BOLD "Cycle: %d | Season: %s | Agents: %d" ANSI_RESET "\n",
           cycle, season_name(season), total_agents);

    /* Grid */
    for (int dy = 0; dy < display_h; dy++) {
        int gy = dy * step_y;
        for (int dx = 0; dx < display_w; dx++) {
            int gx = dx * step_x;
            Cell *c = &full_grid[gy * global_w + gx];

            /* Check if an agent occupies this (possibly downsampled) cell */
            int has_agent = 0;
            if (agent_map) {
                /* In downsampled mode, check the representative cell */
                has_agent = agent_map[gy * global_w + gx];
            }

            if (!c->accessible) {
                /* Inaccessible: dark gray dot */
                printf(BG_DARKGRAY FG_GRAY "." ANSI_RESET);
            } else if (has_agent) {
                /* Agent present: bright yellow '@' on cell background */
                printf("%s" FG_BRIGHT_YELLOW ANSI_BOLD "@" ANSI_RESET,
                       cell_bg(c->type));
            } else {
                /* Normal cell with resource-intensity coloring */
                const char *intens = intensity_code(c->resource, c->max_resource);
                printf("%s%s" FG_BLACK "%c" ANSI_RESET,
                       cell_bg(c->type), intens, cell_char(c->type));
            }
        }
        printf("\n");
    }

    /* Footer */
    if (metrics) {
        printf(ANSI_BOLD "Resources: %.1f | Avg Energy: %.3f | Alive: %d"
               ANSI_RESET "\n",
               metrics->total_resource, metrics->avg_energy,
               metrics->alive_agents);
    }

    /* Controls bar (interactive mode) */
    if (ctrl) {
        const char *icon  = (ctrl->state == TUI_RUNNING) ? "\xe2\x96\xb6" : "\xe2\x8f\xb8";
        const char *label = (ctrl->state == TUI_RUNNING) ? "RUNNING" : "PAUSED ";
        const char *hint  = (ctrl->state == TUI_RUNNING)
                            ? "SPACE:pause"
                            : "SPACE:resume";
        printf(ANSI_BOLD "%s %s" ANSI_RESET " [Speed: %dms] | "
               "%s  N:step  +/-:speed  Q:quit\n",
               icon, label, ctrl->speed_ms, hint);
    }

    fflush(stdout);
    free(agent_map);
}

/* ── MPI gather functions ── */
#ifdef USE_MPI

void tui_gather_grid(SubGrid *sg, Partition *p,
                     Cell *full_grid, int global_w, int global_h,
                     MPI_Comm comm)
{
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    /*
     * Each rank packs its interior cells (no halos) into a contiguous
     * send buffer.  Interior cells are at rows [1..local_h], cols [1..local_w]
     * in the halo-padded array.
     */
    int owned = sg->local_w * sg->local_h;
    Cell *send_buf = malloc(sizeof(Cell) * (size_t)owned);
    for (int r = 0; r < sg->local_h; r++) {
        for (int c = 0; c < sg->local_w; c++) {
            send_buf[r * sg->local_w + c] =
                sg->cells[CELL_AT(sg, r + 1, c + 1)];
        }
    }

    /*
     * Rank 0 receives all chunks.  We use MPI_Gather with a byte-level
     * count since Cell may have padding and we want exact copies.
     *
     * All subgrids have the same dimensions (assuming evenly divisible
     * grid), so MPI_Gather with uniform counts works.
     */
    int cell_bytes = (int)sizeof(Cell);
    Cell *recv_buf = NULL;
    if (rank == 0) {
        recv_buf = malloc(sizeof(Cell) * (size_t)global_w * (size_t)global_h);
    }

    MPI_Gather(send_buf, owned * cell_bytes, MPI_BYTE,
               recv_buf, owned * cell_bytes, MPI_BYTE,
               0, comm);

    /*
     * On rank 0: reorder from rank-order to spatial (row-major) layout.
     * Rank r owns the block at grid position (r % px, r / px) in the
     * partition's Cartesian decomposition.
     */
    if (rank == 0) {
        int local_w = sg->local_w;
        int local_h = sg->local_h;

        for (int r = 0; r < size; r++) {
            /* Determine the spatial origin of rank r's block */
            int col_in_grid = r % p->px;
            int row_in_grid = r / p->px;
            int origin_x = col_in_grid * local_w;
            int origin_y = row_in_grid * local_h;

            Cell *chunk = &recv_buf[r * owned];

            for (int lr = 0; lr < local_h; lr++) {
                for (int lc = 0; lc < local_w; lc++) {
                    int gy = origin_y + lr;
                    int gx = origin_x + lc;
                    if (gy < global_h && gx < global_w) {
                        full_grid[gy * global_w + gx] =
                            chunk[lr * local_w + lc];
                    }
                }
            }
        }
        free(recv_buf);
    }

    free(send_buf);
}

void tui_gather_agents(Agent *local_agents, int local_count,
                       Agent **all_agents, int *total_count,
                       MPI_Comm comm)
{
    int rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    /* Gather all local counts to rank 0 */
    int *counts = NULL;
    if (rank == 0) {
        counts = malloc(sizeof(int) * (size_t)size);
    }
    MPI_Gather(&local_count, 1, MPI_INT, counts, 1, MPI_INT, 0, comm);

    /* Build displacement array and total count on rank 0 */
    int *displs = NULL;
    int total = 0;
    int agent_bytes = (int)sizeof(Agent);

    if (rank == 0) {
        displs = malloc(sizeof(int) * (size_t)size);
        for (int i = 0; i < size; i++) {
            displs[i] = total;
            total += counts[i];
        }
        *total_count = total;
        *all_agents = malloc(sizeof(Agent) * (size_t)(total > 0 ? total : 1));

        /* Convert counts and displs to byte units for MPI_Gatherv */
        for (int i = 0; i < size; i++) {
            counts[i] *= agent_bytes;
            displs[i] *= agent_bytes;
        }
    }

    MPI_Gatherv(local_agents, local_count * agent_bytes, MPI_BYTE,
                rank == 0 ? *all_agents : NULL,
                counts, displs, MPI_BYTE,
                0, comm);

    if (rank == 0) {
        free(counts);
        free(displs);
    }
}

#endif /* USE_MPI */
