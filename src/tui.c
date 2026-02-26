#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

/* ── ANSI escape helpers ── */
#define ANSI_RESET    "\033[0m"
#define ANSI_HOME     "\033[H"       /* cursor to top-left              */
#define ANSI_CLR_EOS  "\033[J"       /* clear from cursor to end of screen */
#define ANSI_BOLD     "\033[1m"
#define ANSI_DIM      "\033[2m"
#define ANSI_ALT_ON   "\033[?1049h"  /* enter alternate screen buffer   */
#define ANSI_ALT_OFF  "\033[?1049l"  /* leave alternate screen buffer   */
#define ANSI_CUR_HIDE "\033[?25l"    /* hide cursor                     */
#define ANSI_CUR_SHOW "\033[?25h"    /* show cursor                     */

/* Maximum display dimensions before downsampling (in grid cells) */
#define MAX_DISPLAY_W 40   /* 40 cells * 2 cols = 80 terminal columns */
#define MAX_DISPLAY_H 30

/* Speed limits for interactive control */
#define SPEED_MIN_MS  10
#define SPEED_MAX_MS  2000
#define SPEED_STEP_MS 25

/* Right-panel width in terminal columns (including box-drawing borders) */
#define RPANEL_W 36

/* ── Box-drawing UTF-8 constants ── */
#define BOX_TL "\xe2\x94\x8c"   /* ┌ */
#define BOX_TR "\xe2\x94\x90"   /* ┐ */
#define BOX_BL "\xe2\x94\x94"   /* └ */
#define BOX_BR "\xe2\x94\x98"   /* ┘ */
#define BOX_H  "\xe2\x94\x80"   /* ─ */
#define BOX_V  "\xe2\x94\x82"   /* │ */
#define BOX_T  "\xe2\x94\x9c"   /* ├ */

/* ── UTF-8 display characters ── */
#define FULL_BLOCK "\xe2\x96\x88"   /* █ */
#define BULLET     "\xe2\x97\x8f"   /* ● */
#define MIDDLE_DOT "\xc2\xb7"       /* · */
#define ICON_PLAY  "\xe2\x96\xb6"   /* ▶ */
#define ICON_PAUSE "\xe2\x8f\xb8"   /* ⏸ */

/* ── 256-color backgrounds for cell types (dim / normal / bright) ── */
/*    Format: \033[48;5;Nm  where N is a 256-color index               */

/* ALDEIA (magenta):  53 / 127 / 163 */
static const char *bg_aldeia[] = {
    "\033[48;5;53m", "\033[48;5;127m", "\033[48;5;163m"
};
/* PESCA (blue):  17 / 27 / 33 */
static const char *bg_pesca[] = {
    "\033[48;5;17m", "\033[48;5;27m", "\033[48;5;33m"
};
/* COLETA (green):  22 / 28 / 40 */
static const char *bg_coleta[] = {
    "\033[48;5;22m", "\033[48;5;28m", "\033[48;5;40m"
};
/* ROCADO (yellow):  58 / 136 / 178 */
static const char *bg_rocado[] = {
    "\033[48;5;58m", "\033[48;5;136m", "\033[48;5;178m"
};
/* INTERDITADA (red):  52 / 124 / 160 */
static const char *bg_interditada[] = {
    "\033[48;5;52m", "\033[48;5;124m", "\033[48;5;160m"
};

/* Inaccessible: dark gray (236) */
#define BG_INACCESSIBLE "\033[48;5;236m"

/* Agent: bright yellow foreground (color 226) */
#define FG_AGENT "\033[38;5;226m"

/* ── Interactive terminal state ── */
static struct termios tui_orig_termios;
static int            tui_raw_mode_active = 0;
static int            tui_tty_fd = -1;
static int            tui_saved_stdout_fd = -1;

void tui_restore_terminal(void) {
    if (tui_raw_mode_active && tui_tty_fd >= 0) {
        printf(ANSI_CUR_SHOW);
        fflush(stdout);

        /* Restore original stdout (mpirun's pipe) for final summary */
        if (tui_saved_stdout_fd >= 0) {
            fflush(stdout);
            dup2(tui_saved_stdout_fd, STDOUT_FILENO);
            close(tui_saved_stdout_fd);
            tui_saved_stdout_fd = -1;
        }

        tcsetattr(tui_tty_fd, TCSAFLUSH, &tui_orig_termios);
        tui_raw_mode_active = 0;
        close(tui_tty_fd);
        tui_tty_fd = -1;
    }
}

void tui_init_interactive(void) {
    if (tui_raw_mode_active) return;

    /* Open the controlling terminal directly — this bypasses
     * mpirun's stdin pipe so we always get a real tty fd. */
    tui_tty_fd = open("/dev/tty", O_RDONLY | O_NONBLOCK);
    if (tui_tty_fd < 0) return;  /* headless — skip silently */

    tcgetattr(tui_tty_fd, &tui_orig_termios);
    atexit(tui_restore_terminal);

    struct termios raw = tui_orig_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;   /* non-blocking */
    raw.c_cc[VTIME] = 0;
    tcsetattr(tui_tty_fd, TCSAFLUSH, &raw);
    tui_raw_mode_active = 1;

    /* Redirect stdout → stderr for TUI output.
     * mpirun buffers child stdout through a pipe that doesn't flush
     * in real-time (output stays stuck in the pipe buffer).
     * stderr is forwarded with far less buffering, so TUI frames
     * actually reach the terminal as they're rendered. */
    fflush(stdout);
    tui_saved_stdout_fd = dup(STDOUT_FILENO);
    dup2(STDERR_FILENO, STDOUT_FILENO);

    printf(ANSI_CUR_HIDE);
    fflush(stdout);
}

int tui_poll_input(TuiControl *ctrl) {
    if (tui_tty_fd < 0) return 0;

    char ch;
    if (read(tui_tty_fd, &ch, 1) != 1) return 0;

    switch (ch) {
        case ' ':
            ctrl->state = (ctrl->state == TUI_RUNNING)
                          ? TUI_PAUSED : TUI_RUNNING;
            break;
        case 'n': case 'N':
            ctrl->state = TUI_PAUSED;
            return 1;  /* step requested */
        case '+': case '=':
            ctrl->speed_ms -= SPEED_STEP_MS;
            if (ctrl->speed_ms < SPEED_MIN_MS)
                ctrl->speed_ms = SPEED_MIN_MS;
            break;
        case '-':
            ctrl->speed_ms += SPEED_STEP_MS;
            if (ctrl->speed_ms > SPEED_MAX_MS)
                ctrl->speed_ms = SPEED_MAX_MS;
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

/* ── 256-color background for a cell type, with resource intensity ── */
static const char *cell_bg256(CellType t, double resource, double max_resource) {
    int shade = 1;  /* default: normal */
    if (max_resource > 0.0) {
        double ratio = resource / max_resource;
        if (ratio > 0.66)      shade = 2;  /* bright */
        else if (ratio > 0.33) shade = 1;  /* normal */
        else                   shade = 0;  /* dim */
    }
    switch (t) {
        case ALDEIA:      return bg_aldeia[shade];
        case PESCA:       return bg_pesca[shade];
        case COLETA:      return bg_coleta[shade];
        case ROCADO:      return bg_rocado[shade];
        case INTERDITADA: return bg_interditada[shade];
        default:          return "";
    }
}

/* ── Format a horizontal bar ── */
static void format_bar(char *buf, size_t bufsz, double fraction, int bar_w) {
    int filled = (int)(fraction * bar_w + 0.5);
    if (filled > bar_w) filled = bar_w;
    if (filled < 0)     filled = 0;

    int pos = 0;
    for (int i = 0; i < bar_w && pos + 4 < (int)bufsz; i++) {
        if (i < filled) {
            /* Full block: █ (UTF-8: 3 bytes) */
            buf[pos++] = '\xe2';
            buf[pos++] = '\x96';
            buf[pos++] = '\x88';
        } else {
            /* Light shade: ░ (UTF-8: 3 bytes) */
            buf[pos++] = '\xe2';
            buf[pos++] = '\x96';
            buf[pos++] = '\x91';
        }
    }
    buf[pos] = '\0';
}

/* ── Format a right-panel top/bottom border line ── */
static void format_box_top(char *buf, size_t bufsz, const char *title, int inner_w) {
    /* ┌─ Title ─...─┐ */
    int title_len = (int)strlen(title);
    int dashes_after = inner_w - 2 - title_len;
    if (dashes_after < 1) dashes_after = 1;

    int pos = 0;
    pos += snprintf(buf + pos, bufsz - (size_t)pos, "%s%s ", BOX_TL, BOX_H);
    pos += snprintf(buf + pos, bufsz - (size_t)pos, "%s", title);
    pos += snprintf(buf + pos, bufsz - (size_t)pos, " ");
    for (int i = 0; i < dashes_after && pos + 4 < (int)bufsz; i++)
        pos += snprintf(buf + pos, bufsz - (size_t)pos, "%s", BOX_H);
    snprintf(buf + pos, bufsz - (size_t)pos, "%s", BOX_TR);
}

static void format_box_bottom(char *buf, size_t bufsz, int inner_w) {
    int pos = 0;
    pos += snprintf(buf + pos, bufsz - (size_t)pos, "%s", BOX_BL);
    for (int i = 0; i < inner_w && pos + 4 < (int)bufsz; i++)
        pos += snprintf(buf + pos, bufsz - (size_t)pos, "%s", BOX_H);
    snprintf(buf + pos, bufsz - (size_t)pos, "%s", BOX_BR);
}

/* ── Count display columns for a UTF-8 string (no ANSI escapes) ── */
/* Each ASCII byte (0x00–0x7F) = 1 column.  Each multi-byte sequence
 * (leading byte 0xC0+ followed by continuation bytes 0x80–0xBF) = 1 column. */
static int utf8_display_width(const char *s) {
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ) {
        if (*p < 0x80) {
            width++;
            p++;
        } else {
            /* Skip leading byte + all continuation bytes → 1 column */
            width++;
            p++;
            while (*p >= 0x80 && *p < 0xC0) p++;
        }
    }
    return width;
}

/* ── Pad or truncate a line to exactly `width` visible characters ── */
static void format_box_line(char *buf, size_t bufsz, const char *content, int inner_w) {
    int clen = utf8_display_width(content);
    int pad = inner_w - clen;
    if (pad < 0) pad = 0;
    int pos = 0;
    pos += snprintf(buf + pos, bufsz - (size_t)pos, "%s", BOX_V);
    pos += snprintf(buf + pos, bufsz - (size_t)pos, "%s", content);
    for (int i = 0; i < pad && pos + 1 < (int)bufsz; i++)
        buf[pos++] = ' ';
    buf[pos] = '\0';
    snprintf(buf + pos, bufsz - (size_t)pos, "%s", BOX_V);
}

/* ── Render ── */
void tui_render(Cell *full_grid, int global_w, int global_h,
                Agent *all_agents, int total_agents,
                int cycle, int total_cycles,
                Season season, SimMetrics *metrics,
                CyclePerf *perf, TuiControl *ctrl)
{
    /* Compute downsampling step if grid is too large */
    int step_x = 1, step_y = 1;
    if (global_w > MAX_DISPLAY_W) step_x = (global_w + MAX_DISPLAY_W - 1) / MAX_DISPLAY_W;
    if (global_h > MAX_DISPLAY_H) step_y = (global_h + MAX_DISPLAY_H - 1) / MAX_DISPLAY_H;

    int display_w = (global_w + step_x - 1) / step_x;
    int display_h = (global_h + step_y - 1) / step_y;

    /* Grid occupies display_w * 2 terminal columns (2-char cells) */
    int grid_tcols = display_w * 2;

    /*
     * Build an agent presence map for O(1) lookup.
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

    /* ── Build right-panel lines ── */
    /* We'll have up to 20 right-panel lines for the Performance panel
     * and a few more for Simulation + Controls.
     * Each line is a pre-formatted string (plain content, no ANSI). */
    #define MAX_RPANEL_LINES 24
    char rpanel[MAX_RPANEL_LINES][256];
    int rcount = 0;  /* number of rpanel lines */

    int inner_w = RPANEL_W - 2;  /* minus left/right box chars */

    /* ── Performance panel ── */
    {
        const char *perf_title = (ctrl && ctrl->state == TUI_PAUSED)
                                 ? "Performance (paused)" : "Performance";
        format_box_top(rpanel[rcount++], 256, perf_title, inner_w);
    }

    if (perf) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), " Cycle Time: %7.1fms         ", perf->cycle_time * 1000.0);
        format_box_line(rpanel[rcount++], 256, tmp, inner_w);

        double ct_ms = perf->compute_time  * 1000.0;
        double ht_ms = perf->halo_time     * 1000.0;
        double mt_ms = perf->migrate_time  * 1000.0;
        double me_ms = perf->metrics_time  * 1000.0;
        double rt_ms = perf->render_time   * 1000.0;
        double total_ms = perf->cycle_time * 1000.0;
        if (total_ms < 0.001) total_ms = 0.001;

        #define PHASE_LINE(prefix, val) do {                            \
            snprintf(tmp, sizeof(tmp), " %s %5.1fms (%4.1f%%)",        \
                     prefix, val, val / total_ms * 100.0);             \
            format_box_line(rpanel[rcount++], 256, tmp, inner_w);      \
        } while(0)

        PHASE_LINE("\xe2\x94\x9c\xe2\x94\x80 Compute: ", ct_ms);
        PHASE_LINE("\xe2\x94\x9c\xe2\x94\x80 Halo Exch:", ht_ms);
        PHASE_LINE("\xe2\x94\x9c\xe2\x94\x80 Migration:", mt_ms);
        PHASE_LINE("\xe2\x94\x9c\xe2\x94\x80 Metrics:  ", me_ms);
        PHASE_LINE("\xe2\x94\x94\xe2\x94\x80 Render:   ", rt_ms);
        #undef PHASE_LINE

        format_box_line(rpanel[rcount++], 256, "", inner_w);

        snprintf(tmp, sizeof(tmp), " MPI Ranks:    %3d", perf->mpi_size);
        format_box_line(rpanel[rcount++], 256, tmp, inner_w);

        snprintf(tmp, sizeof(tmp), " OMP Threads:  %3d per rank", perf->omp_threads);
        format_box_line(rpanel[rcount++], 256, tmp, inner_w);

        /* Load balance bar */
        char bar[64];
        format_bar(bar, sizeof(bar), perf->load_balance, 10);
        snprintf(tmp, sizeof(tmp), " Load Balance: %s %3.0f%%",
                 bar, perf->load_balance * 100.0);
        format_box_line(rpanel[rcount++], 256, tmp, inner_w);

        snprintf(tmp, sizeof(tmp), " Comm/Compute: %5.1f%%", perf->comm_compute * 100.0);
        format_box_line(rpanel[rcount++], 256, tmp, inner_w);
    } else {
        format_box_line(rpanel[rcount++], 256, " (no data yet)", inner_w);
    }
    format_box_bottom(rpanel[rcount++], 256, inner_w);

    /* ── Simulation panel ── */
    format_box_top(rpanel[rcount++], 256, "Simulation", inner_w);
    {
        char tmp[128];
        double progress = (total_cycles > 0)
                          ? (double)cycle / (double)total_cycles
                          : 0.0;
        char bar[64];
        format_bar(bar, sizeof(bar), progress, 10);
        snprintf(tmp, sizeof(tmp), " Cycle: %d/%d %s %3.0f%%",
                 cycle, total_cycles, bar, progress * 100.0);
        format_box_line(rpanel[rcount++], 256, tmp, inner_w);

        int alive = metrics ? metrics->alive_agents : total_agents;
        snprintf(tmp, sizeof(tmp), " Season: %-3s  Agents: %d",
                 season_name(season), alive);
        format_box_line(rpanel[rcount++], 256, tmp, inner_w);

        if (metrics) {
            snprintf(tmp, sizeof(tmp), " Resources: %-8.1f Avg E: %.2f",
                     metrics->total_resource, metrics->avg_energy);
            format_box_line(rpanel[rcount++], 256, tmp, inner_w);

            snprintf(tmp, sizeof(tmp), " Energy: %.2f - %.2f",
                     metrics->min_energy, metrics->max_energy);
            format_box_line(rpanel[rcount++], 256, tmp, inner_w);
        }
    }
    format_box_bottom(rpanel[rcount++], 256, inner_w);

    /* ── Controls panel ── */
    if (ctrl) {
        format_box_top(rpanel[rcount++], 256, "Controls", inner_w);
        {
            char tmp[128];
            const char *state_label = (ctrl->state == TUI_RUNNING) ? "RUNNING" : "PAUSED ";
            const char *speed_label;
            if      (ctrl->speed_ms <= 25)  speed_label = "Fastest";
            else if (ctrl->speed_ms <= 75)  speed_label = "Fast";
            else if (ctrl->speed_ms <= 150) speed_label = "Normal";
            else if (ctrl->speed_ms <= 500) speed_label = "Slow";
            else                            speed_label = "Slowest";
            snprintf(tmp, sizeof(tmp), " %s %s [%dms %s]",
                     (ctrl->state == TUI_RUNNING) ? ICON_PLAY : ICON_PAUSE,
                     state_label, ctrl->speed_ms, speed_label);
            format_box_line(rpanel[rcount++], 256, tmp, inner_w);

            snprintf(tmp, sizeof(tmp), " SPC:%s N:step +/-:spd Q:quit",
                     (ctrl->state == TUI_RUNNING) ? "pause " : "resume");
            format_box_line(rpanel[rcount++], 256, tmp, inner_w);
        }
        format_box_bottom(rpanel[rcount++], 256, inner_w);
    }

    /* ── Move cursor home (alt screen buffer handles the canvas) ── */
    printf(ANSI_HOME);

    /* Grid top border + first rpanel line on the same row */
    /* Grid box top: ┌─ Grid ─...─┐ */
    {
        char grid_top[256];
        char grid_title[64];
        snprintf(grid_title, sizeof(grid_title), "Grid [Cycle %d/%d %s]",
                 cycle, total_cycles, season_name(season));
        format_box_top(grid_top, sizeof(grid_top), grid_title, grid_tcols);
        printf("%s", grid_top);
        /* Space between panels */
        if (rcount > 0)
            printf(" %s", rpanel[0]);
        printf("\n");
    }

    /* Grid rows side-by-side with rpanel lines */
    for (int dy = 0; dy < display_h; dy++) {
        int gy = dy * step_y;

        /* Left border */
        printf("%s", BOX_V);

        for (int dx = 0; dx < display_w; dx++) {
            int gx = dx * step_x;
            Cell *c = &full_grid[gy * global_w + gx];

            int has_agent = 0;
            if (agent_map)
                has_agent = agent_map[gy * global_w + gx];

            if (!c->accessible) {
                /* Inaccessible: dim middle dots on dark gray */
                printf(BG_INACCESSIBLE "\033[38;5;242m" MIDDLE_DOT MIDDLE_DOT ANSI_RESET);
            } else if (has_agent) {
                /* Agent: bright yellow bullet on cell background */
                const char *bg = cell_bg256(c->type, c->resource, c->max_resource);
                printf("%s" FG_AGENT ANSI_BOLD BULLET " " ANSI_RESET, bg);
            } else {
                /* Normal cell: 2 full-block chars with 256-color bg */
                const char *bg = cell_bg256(c->type, c->resource, c->max_resource);
                printf("%s" FULL_BLOCK FULL_BLOCK ANSI_RESET, bg);
            }
        }

        /* Right border of grid box */
        printf("%s", BOX_V);

        /* Adjacent rpanel line (dy+1 because row 0 was the top border) */
        int rline = dy + 1;
        if (rline < rcount)
            printf(" %s", rpanel[rline]);

        printf("\n");
    }

    /* Grid bottom border */
    {
        char grid_bot[256];
        format_box_bottom(grid_bot, sizeof(grid_bot), grid_tcols);
        printf("%s", grid_bot);

        int rline = display_h + 1;
        if (rline < rcount)
            printf(" %s", rpanel[rline]);
        printf("\n");
    }

    /* Color legend below the grid */
    printf(" ");
    const char *legend_bg[] = {
        "\033[48;5;127m", "\033[48;5;27m", "\033[48;5;28m",
        "\033[48;5;136m", "\033[48;5;124m"
    };
    const char *legend_lbl[] = { "A", "P", "C", "R", "X" };
    for (int i = 0; i < 5; i++)
        printf(" %s %s " ANSI_RESET, legend_bg[i], legend_lbl[i]);
    printf("  " BG_INACCESSIBLE "\033[38;5;242m" MIDDLE_DOT MIDDLE_DOT ANSI_RESET ":closed");
    printf("  " FG_AGENT ANSI_BOLD BULLET ANSI_RESET ":agent\n");

    /* Any remaining rpanel lines that didn't fit beside the grid */
    for (int rline = display_h + 2; rline < rcount; rline++) {
        /* Indent to align with where the rpanel column starts */
        /* grid_tcols + 2 (left/right box chars) + 1 space */
        for (int s = 0; s < grid_tcols + 3; s++)
            putchar(' ');
        printf("%s\n", rpanel[rline]);
    }

    printf(ANSI_CLR_EOS);   /* wipe any leftover lines from previous frame */
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
