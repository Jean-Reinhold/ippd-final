#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>
#include <omp.h>

#include "types.h"
#include "config.h"
#include "rng.h"
#include "season.h"
#include "workload.h"
#include "grid.h"
#include "partition.h"
#include "agent.h"
#include "halo.h"
#include "migrate.h"
#include "metrics.h"
#include "tui.h"

/* ── Command-line argument parsing ── */
static void parse_args(int argc, char **argv, SimConfig *cfg) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc)
            cfg->global_w = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
            cfg->global_h = atoi(argv[++i]);
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            cfg->total_cycles = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
            cfg->season_length = atoi(argv[++i]);
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
            cfg->num_agents = atoi(argv[++i]);
        else if (strcmp(argv[i], "-W") == 0 && i + 1 < argc)
            cfg->max_workload = atoi(argv[++i]);
        else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc)
            cfg->seed = (uint64_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--no-tui") == 0)
            cfg->tui_enabled = 0;
        else if (strcmp(argv[i], "--tui-interval") == 0 && i + 1 < argc)
            cfg->tui_interval = atoi(argv[++i]);
    }
}

/* ── Print usage ── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -w WIDTH          Grid width  (default %d)\n"
        "  -h HEIGHT         Grid height (default %d)\n"
        "  -c CYCLES         Total cycles (default %d)\n"
        "  -s SEASON_LEN     Cycles per season (default %d)\n"
        "  -a AGENTS         Number of agents (default %d)\n"
        "  -W WORKLOAD       Max workload iterations (default %d)\n"
        "  -S SEED           Random seed (default %llu)\n"
        "  --no-tui          Disable TUI rendering\n"
        "  --tui-interval N  Render TUI every N cycles (default %d)\n",
        prog,
        DEFAULT_GLOBAL_W, DEFAULT_GLOBAL_H, DEFAULT_TOTAL_CYCLES,
        DEFAULT_SEASON_LENGTH, DEFAULT_NUM_AGENTS, DEFAULT_MAX_WORKLOAD,
        (unsigned long long)DEFAULT_SEED, DEFAULT_TUI_INTERVAL);
}

int main(int argc, char **argv) {
    /* ── 1. MPI initialization ── */
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        fprintf(stderr, "Error: MPI_THREAD_FUNNELED not supported "
                "(requested %d, got %d)\n",
                MPI_THREAD_FUNNELED, provided);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* ── 2. Parse configuration ── */
    SimConfig cfg = SIM_CONFIG_DEFAULTS;
    parse_args(argc, argv, &cfg);

    /* Check for --help on rank 0 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            if (rank == 0) usage(argv[0]);
            MPI_Finalize();
            return 0;
        }
    }

    if (rank == 0) {
        printf("=== IPPD Simulation ===\n");
        printf("Grid: %dx%d | Cycles: %d | Agents: %d | Ranks: %d\n",
               cfg.global_w, cfg.global_h, cfg.total_cycles,
               cfg.num_agents, size);
        printf("Season length: %d | Seed: %llu | Workload: %d\n",
               cfg.season_length, (unsigned long long)cfg.seed,
               cfg.max_workload);
        printf("TUI: %s (interval %d) | OMP threads: %d\n",
               cfg.tui_enabled ? "on" : "off", cfg.tui_interval,
               omp_get_max_threads());
        printf("=======================\n");
    }

    /* ── 3. Initialize partition (Cartesian decomposition) ── */
    Partition partition;
    partition_init(&partition, cfg.global_w, cfg.global_h, MPI_COMM_WORLD);

    /* ── 4. Create and initialize subgrid ── */
    SubGrid sg;
    subgrid_create(&sg, &partition, cfg.global_w, cfg.global_h);
    subgrid_init(&sg, &partition, cfg.seed);

    /* ── 5. Initialize agents ── */
    int agent_count = 0;
    int agent_capacity = cfg.num_agents * 2;  /* room for migration */
    Agent *agents = malloc(sizeof(Agent) * (size_t)agent_capacity);
    agents_init(agents, &agent_count, cfg.num_agents,
                &sg, &partition, cfg.global_w, cfg.global_h,
                cfg.initial_energy, cfg.seed);

    /* ── 6. Allocate TUI buffers (rank 0 only) ── */
    Cell *full_grid = NULL;
    if (rank == 0 && cfg.tui_enabled) {
        full_grid = malloc(sizeof(Cell) *
                           (size_t)cfg.global_w * (size_t)cfg.global_h);
    }

    /* ── 7. Main simulation loop ── */
    double t_start = MPI_Wtime();

    for (int cycle = 0; cycle < cfg.total_cycles; cycle++) {

        /* 5.1 — Season update + broadcast + update cell accessibility */
        Season season = season_for_cycle(cycle, cfg.season_length);
        MPI_Bcast(&season, 1, MPI_INT, 0, partition.cart_comm);

        /* Update accessibility for all owned cells */
        for (int r = 1; r <= sg.local_h; r++) {
            for (int c = 1; c <= sg.local_w; c++) {
                Cell *cell = &sg.cells[CELL_AT(&sg, r, c)];
                cell->accessible = season_accessibility(cell->type, season);
            }
        }

        /* 5.2 — Halo exchange */
        halo_exchange(&sg, &partition);

        /* 5.3 — Agent processing (decision-making + workload) */
        agents_process(agents, agent_count, &sg, season,
                       cfg.max_workload, cfg.seed);

        /* 5.4 — Agent migration between ranks */
        migrate_agents(&agents, &agent_count, &agent_capacity,
                       &partition, &sg, cfg.global_w, cfg.global_h);

        /* 5.5 — Subgrid resource update (regeneration) */
        subgrid_update(&sg, season);

        /* 5.6 — Metrics */
        SimMetrics local_metrics, global_metrics;
        metrics_compute_local(&sg, agents, agent_count, &local_metrics);
        metrics_reduce_global(&local_metrics, &global_metrics,
                              partition.cart_comm);

        /* 5.7 — TUI rendering (rank 0 only, at intervals) */
        if (rank == 0 && cfg.tui_enabled &&
            (cycle % cfg.tui_interval == 0 ||
             cycle == cfg.total_cycles - 1)) {

            tui_gather_grid(&sg, &partition, full_grid,
                            cfg.global_w, cfg.global_h,
                            partition.cart_comm);

            Agent *all_agents = NULL;
            int total_agents = 0;
            tui_gather_agents(agents, agent_count, &all_agents,
                              &total_agents, partition.cart_comm);

            tui_render(full_grid, cfg.global_w, cfg.global_h,
                       all_agents, total_agents,
                       cycle, season, &global_metrics);

            free(all_agents);
        } else {
            /*
             * Non-rendering ranks still participate in the gather
             * if TUI is enabled on this cycle.
             */
            if (cfg.tui_enabled &&
                (cycle % cfg.tui_interval == 0 ||
                 cycle == cfg.total_cycles - 1)) {
                tui_gather_grid(&sg, &partition, NULL,
                                cfg.global_w, cfg.global_h,
                                partition.cart_comm);

                Agent *dummy = NULL;
                int dummy_count = 0;
                tui_gather_agents(agents, agent_count, &dummy,
                                  &dummy_count, partition.cart_comm);
                free(dummy);
            }
        }
    }

    double t_end = MPI_Wtime();

    /* ── 8. Final output ── */
    if (rank == 0) {
        SimMetrics final_local, final_global;
        metrics_compute_local(&sg, agents, agent_count, &final_local);
        metrics_reduce_global(&final_local, &final_global,
                              partition.cart_comm);

        printf("\n=== Simulation Complete ===\n");
        printf("Total time:     %.3f s\n", t_end - t_start);
        printf("Total resource: %.1f\n", final_global.total_resource);
        printf("Alive agents:   %d\n", final_global.alive_agents);
        printf("Avg energy:     %.3f\n", final_global.avg_energy);
        printf("Max energy:     %.3f\n", final_global.max_energy);
        printf("Min energy:     %.3f\n", final_global.min_energy);
        printf("===========================\n");
    } else {
        /* Non-zero ranks still participate in the final reduce */
        SimMetrics final_local, final_global;
        metrics_compute_local(&sg, agents, agent_count, &final_local);
        metrics_reduce_global(&final_local, &final_global,
                              partition.cart_comm);
    }

    /* ── 9. Cleanup ── */
    free(agents);
    free(full_grid);
    subgrid_destroy(&sg);
    partition_destroy(&partition);
    MPI_Finalize();

    return 0;
}
