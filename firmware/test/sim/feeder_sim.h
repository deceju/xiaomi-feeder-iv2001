/* Physical simulator of the IV2001 auger, index beam, motor current and kibble.
 *
 * Deterministic and driven by the fake clock. It reads the motor drive pins
 * from the fake GPIO expander, moves a virtual auger, and writes back what the
 * firmware would sense: the index beam (fake expander input), the motor
 * current (fake ADC), and Wi-Fi bus contention (fake WFCI bus).
 *
 * Numbers come from field measurements: rewrite-kit/01-physical-behavior.md.
 */
#ifndef FEEDER_SIM_H
#define FEEDER_SIM_H

#include <stdbool.h>
#include <stdint.h>

#define FEEDER_SIM_NEVER 0xFFFFFFFFu

typedef struct {
    uint32_t hole_period_ms;    /* forward travel between two holes (6400 on mains) */
    uint32_t open_window_ms;    /* travel during which the beam is open (200) */
    uint32_t springback_ms;     /* after a stop inside a hole: rest this far before it */
    uint32_t start_pos_ms;      /* initial position within a period (0 = hole edge, open) */
    uint32_t flicker_ms;        /* start-up jolt: beam reads open this long at each forward start */
    uint16_t run_ma;            /* forward current (70-150 measured) */
    uint16_t run_ma_noise;      /* +/- noise on run_ma */
    uint16_t reverse_ma;
    uint16_t stall_ma;          /* jammed auger (~280 measured) */
    uint32_t jam_at_ms;         /* forward run time at which a jam starts (FEEDER_SIM_NEVER = none) */
    uint8_t jam_clear_reverses; /* reverse phases that clear the jam (0 = never clears) */
    uint16_t dg_per_hole;       /* kibble per hole, decigrams (85 = 8.5 g) */
    uint8_t dg_noise_pct;       /* +/- noise on dg_per_hole */
    uint32_t hopper_dg;         /* kibble left; 0 = empty */
    uint8_t busy_pct;           /* chance that a bus loan is refused at a given tick */
    uint32_t seed;
} feeder_sim_cfg_t;

typedef struct {
    uint32_t holes_dropped;     /* holes whose kibble actually fell */
    uint32_t dropped_dg;
    uint32_t forward_ms;        /* total forward run time */
    uint32_t reverse_phases;
    uint32_t elapsed_ms;
    bool jammed;
} feeder_sim_stats_t;

/* Field defaults (mains supply, full hopper, no faults). */
feeder_sim_cfg_t feeder_sim_default_cfg(void);

/* Reset the fakes this simulator drives and start from cfg. Call after the
 * motor stack is set up (adapter stack: real index adapter + motor driver). */
void feeder_sim_begin(const feeder_sim_cfg_t *cfg);

/* Run the motor task loop under physics until the motor goes idle or
 * max_ms elapses. Returns true when the motor is idle. */
bool feeder_sim_run_until_idle(uint32_t max_ms);

/* Run the motor task loop under physics for ms. */
void feeder_sim_run_ms(uint32_t ms);

/* Step once (one motor loop slice) — for callers that drive other tasks too. */
void feeder_sim_step(void);

feeder_sim_stats_t feeder_sim_stats(void);
bool feeder_sim_beam_open(void);

#endif /* FEEDER_SIM_H */
