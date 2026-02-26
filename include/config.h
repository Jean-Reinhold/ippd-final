#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

#define DEFAULT_GLOBAL_W        64
#define DEFAULT_GLOBAL_H        64
#define DEFAULT_TOTAL_CYCLES    100
#define DEFAULT_SEASON_LENGTH   100
#define DEFAULT_NUM_AGENTS      50
#define DEFAULT_MAX_WORKLOAD    500000
#define DEFAULT_CONSUMPTION     0.2
#define DEFAULT_ENERGY_GAIN     0.3
#define DEFAULT_ENERGY_LOSS     0.4
#define DEFAULT_INITIAL_ENERGY  0.8
#define DEFAULT_REPRODUCE_THRESHOLD 12.0
#define DEFAULT_REPRODUCE_COST      3.0
#define DEFAULT_SEED            42ULL
#define DEFAULT_TUI_ENABLED     1
#define DEFAULT_TUI_INTERVAL    1

#define SIM_CONFIG_DEFAULTS {           \
    .global_w        = DEFAULT_GLOBAL_W,        \
    .global_h        = DEFAULT_GLOBAL_H,        \
    .total_cycles    = DEFAULT_TOTAL_CYCLES,    \
    .season_length   = DEFAULT_SEASON_LENGTH,   \
    .num_agents      = DEFAULT_NUM_AGENTS,      \
    .max_workload    = DEFAULT_MAX_WORKLOAD,     \
    .consumption_rate = DEFAULT_CONSUMPTION,     \
    .energy_gain     = DEFAULT_ENERGY_GAIN,     \
    .energy_loss     = DEFAULT_ENERGY_LOSS,     \
    .initial_energy  = DEFAULT_INITIAL_ENERGY,  \
    .reproduce_threshold = DEFAULT_REPRODUCE_THRESHOLD, \
    .reproduce_cost  = DEFAULT_REPRODUCE_COST, \
    .seed            = DEFAULT_SEED,            \
    .tui_enabled     = DEFAULT_TUI_ENABLED,     \
    .tui_interval    = DEFAULT_TUI_INTERVAL,    \
    .csv_output      = 0                        \
}

#endif /* CONFIG_H */
