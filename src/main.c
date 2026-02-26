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
        else if (strcmp(argv[i], "--tui-file") == 0 && i + 1 < argc)
            strncpy(cfg->tui_file, argv[++i], sizeof(cfg->tui_file) - 1);
    }
}

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
        "  --tui-interval N  Render TUI every N cycles (default %d)\n"
        "  --tui-file PATH   Write TUI frames to file (for MPI compatibility)\n",
        prog,
        DEFAULT_GLOBAL_W, DEFAULT_GLOBAL_H, DEFAULT_TOTAL_CYCLES,
        DEFAULT_SEASON_LENGTH, DEFAULT_NUM_AGENTS, DEFAULT_MAX_WORKLOAD,
        (unsigned long long)DEFAULT_SEED, DEFAULT_TUI_INTERVAL);
}

int main(int argc, char **argv) {
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

    SimConfig cfg = SIM_CONFIG_DEFAULTS;
    parse_args(argc, argv, &cfg);

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

    Partition partition;
    partition_init(&partition, cfg.global_w, cfg.global_h, MPI_COMM_WORLD);

    SubGrid sg;
    subgrid_create(&sg, &partition, cfg.global_w, cfg.global_h);
    subgrid_init(&sg, &partition, cfg.seed);

    int agent_count = 0;
    int agent_capacity = cfg.num_agents * 2;
    Agent *agents = malloc(sizeof(Agent) * (size_t)agent_capacity);
    agents_init(agents, &agent_count, cfg.num_agents,
                &sg, &partition, cfg.global_w, cfg.global_h,
                cfg.initial_energy, cfg.seed);

    Cell *full_grid = NULL;
    if (rank == 0 && cfg.tui_enabled) {
        full_grid = malloc(sizeof(Cell) *
                           (size_t)cfg.global_w * (size_t)cfg.global_h);
    }

    TuiControl ctrl = { .state = TUI_RUNNING, .speed_ms = 100 };

    if (cfg.tui_enabled && rank == 0)
        tui_init_interactive();

    if (cfg.tui_file[0] && rank == 0)
        tui_set_output_file(cfg.tui_file);

    double t_start = MPI_Wtime();
    int cycle = 0;
    CyclePerf last_perf = {0};
    int have_last_perf = 0;

    while (cycle < cfg.total_cycles && ctrl.state != TUI_QUIT) {
        int step_requested = 0;

        if (rank == 0 && cfg.tui_enabled)
            step_requested = tui_poll_input(&ctrl);

        MPI_Bcast(&ctrl, sizeof(ctrl), MPI_BYTE, 0, partition.cart_comm);
        MPI_Bcast(&step_requested, 1, MPI_INT, 0, partition.cart_comm);

        if (ctrl.state == TUI_QUIT) break;

        if (ctrl.state == TUI_PAUSED && !step_requested) {
            if (rank == 0 && cfg.tui_enabled) {
                tui_gather_grid(&sg, &partition, full_grid,
                                cfg.global_w, cfg.global_h,
                                partition.cart_comm);

                Agent *all_agents = NULL;
                int total_agents = 0;
                tui_gather_agents(agents, agent_count, &all_agents,
                                  &total_agents, partition.cart_comm);

                SimMetrics local_m, global_m;
                metrics_compute_local(&sg, agents, agent_count, &local_m);
                metrics_reduce_global(&local_m, &global_m,
                                      partition.cart_comm);

                tui_render(full_grid, cfg.global_w, cfg.global_h,
                           all_agents, total_agents,
                           cycle, cfg.total_cycles,
                           season_for_cycle(cycle, cfg.season_length),
                           &global_m,
                           have_last_perf ? &last_perf : NULL,
                           &ctrl);
                free(all_agents);
                usleep(50000); /* 50ms poll interval to avoid busy-wait */
            } else {
                /* Ranks não-zero participam das chamadas coletivas mesmo em pausa. */
                tui_gather_grid(&sg, &partition, NULL,
                                cfg.global_w, cfg.global_h,
                                partition.cart_comm);
                Agent *dummy = NULL;
                int dummy_count = 0;
                tui_gather_agents(agents, agent_count, &dummy,
                                  &dummy_count, partition.cart_comm);
                free(dummy);

                SimMetrics local_m, global_m;
                metrics_compute_local(&sg, agents, agent_count, &local_m);
                metrics_reduce_global(&local_m, &global_m,
                                      partition.cart_comm);
            }
            MPI_Barrier(partition.cart_comm);
            continue;
        }

        double t_cycle_start = MPI_Wtime();
        CyclePerf local_perf = {0};

        Season season = season_for_cycle(cycle, cfg.season_length);
        MPI_Bcast(&season, 1, MPI_INT, 0, partition.cart_comm);

        for (int r = 1; r <= sg.local_h; r++) {
            for (int c = 1; c <= sg.local_w; c++) {
                Cell *cell = &sg.cells[CELL_AT(&sg, r, c)];
                cell->accessible = season_accessibility(cell->type, season);
            }
        }

        double t0 = MPI_Wtime();
        halo_exchange(&sg, &partition);
        local_perf.halo_time = MPI_Wtime() - t0;

        t0 = MPI_Wtime();
        agents_process(agents, agent_count, &sg, season,
                       cfg.max_workload, cfg.seed,
                       cfg.energy_gain, cfg.energy_loss);

        subgrid_update(&sg, season);
        local_perf.compute_time = MPI_Wtime() - t0;

        t0 = MPI_Wtime();
        migrate_agents(&agents, &agent_count, &agent_capacity,
                       &partition, &sg, cfg.global_w, cfg.global_h);
        local_perf.migrate_time = MPI_Wtime() - t0;

        t0 = MPI_Wtime();
        SimMetrics local_metrics, global_metrics;
        metrics_compute_local(&sg, agents, agent_count, &local_metrics);
        metrics_reduce_global(&local_metrics, &global_metrics,
                              partition.cart_comm);
        local_perf.metrics_time = MPI_Wtime() - t0;

        int do_render = cfg.tui_enabled &&
            (cycle % cfg.tui_interval == 0 ||
             cycle == cfg.total_cycles - 1);

        t0 = MPI_Wtime();
        if (do_render) {
            if (rank == 0) {
                tui_gather_grid(&sg, &partition, full_grid,
                                cfg.global_w, cfg.global_h,
                                partition.cart_comm);

                Agent *all_agents = NULL;
                int total_agents = 0;
                tui_gather_agents(agents, agent_count, &all_agents,
                                  &total_agents, partition.cart_comm);

                local_perf.render_time = MPI_Wtime() - t0;
                local_perf.cycle_time = MPI_Wtime() - t_cycle_start;

                /* MPI_MAX nos tempos: o rank gargalo define o tempo real do ciclo. */
                CyclePerf global_perf = {0};
                MPI_Reduce(&local_perf.cycle_time,   &global_perf.cycle_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.compute_time,  &global_perf.compute_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.halo_time,     &global_perf.halo_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.migrate_time,  &global_perf.migrate_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.metrics_time,  &global_perf.metrics_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.render_time,   &global_perf.render_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);

                int min_agents, max_agents;
                MPI_Reduce(&agent_count, &min_agents, 1, MPI_INT,
                           MPI_MIN, 0, partition.cart_comm);
                MPI_Reduce(&agent_count, &max_agents, 1, MPI_INT,
                           MPI_MAX, 0, partition.cart_comm);

                global_perf.load_balance = (max_agents > 0)
                    ? (double)min_agents / (double)max_agents : 1.0;
                global_perf.comm_compute = (global_perf.compute_time > 0.0)
                    ? (global_perf.halo_time + global_perf.migrate_time
                       + global_perf.metrics_time) / global_perf.compute_time
                    : 0.0;
                global_perf.mpi_size    = size;
                global_perf.omp_threads = omp_get_max_threads();

                tui_render(full_grid, cfg.global_w, cfg.global_h,
                           all_agents, total_agents,
                           cycle, cfg.total_cycles,
                           season, &global_metrics,
                           &global_perf, &ctrl);

                last_perf = global_perf;
                have_last_perf = 1;

                free(all_agents);
                usleep((unsigned int)(ctrl.speed_ms * 1000));
            } else {
                /* Ranks não-zero participam dos gathers e reduções de perf. */
                tui_gather_grid(&sg, &partition, NULL,
                                cfg.global_w, cfg.global_h,
                                partition.cart_comm);
                Agent *dummy = NULL;
                int dummy_count = 0;
                tui_gather_agents(agents, agent_count, &dummy,
                                  &dummy_count, partition.cart_comm);
                free(dummy);

                local_perf.render_time = MPI_Wtime() - t0;
                local_perf.cycle_time = MPI_Wtime() - t_cycle_start;

                CyclePerf global_perf = {0};
                MPI_Reduce(&local_perf.cycle_time,   &global_perf.cycle_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.compute_time,  &global_perf.compute_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.halo_time,     &global_perf.halo_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.migrate_time,  &global_perf.migrate_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.metrics_time,  &global_perf.metrics_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);
                MPI_Reduce(&local_perf.render_time,   &global_perf.render_time,
                           1, MPI_DOUBLE, MPI_MAX, 0, partition.cart_comm);

                int min_agents, max_agents;
                MPI_Reduce(&agent_count, &min_agents, 1, MPI_INT,
                           MPI_MIN, 0, partition.cart_comm);
                MPI_Reduce(&agent_count, &max_agents, 1, MPI_INT,
                           MPI_MAX, 0, partition.cart_comm);
            }
        }

        cycle++;
    }

    double t_end = MPI_Wtime();

    if (rank == 0 && cfg.tui_enabled)
        tui_restore_terminal();

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
        /* Ranks não-zero participam da redução final. */
        SimMetrics final_local, final_global;
        metrics_compute_local(&sg, agents, agent_count, &final_local);
        metrics_reduce_global(&final_local, &final_global,
                              partition.cart_comm);
    }

    free(agents);
    free(full_grid);
    subgrid_destroy(&sg);
    partition_destroy(&partition);
    MPI_Finalize();

    return 0;
}
