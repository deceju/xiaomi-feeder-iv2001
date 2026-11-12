/* Physical simulator — see feeder_sim.h. */
#include "feeder_sim.h"

#include <string.h>

#include "board_gpio_iv2001.h"
#include "fake_adc_port.h"
#include "fake_gpio_expander_port.h"
#include "fake_time.h"
#include "fake_wfci_bus_port.h"
#include "motor_ctrl.h"
#include "motor_ctrl_test.h"
#include "motor_jam.h"

typedef enum { SIM_STOP, SIM_FWD, SIM_REV } sim_drive_t;

static feeder_sim_cfg_t s_cfg;
static feeder_sim_stats_t s_stats;
static int64_t s_pos;           /* absolute forward travel, ms */
static int64_t s_last_hole;     /* highest hole index whose kibble fell */
static sim_drive_t s_drive;
static uint32_t s_now;
static uint32_t s_flicker_until;
static uint32_t s_rng;
static uint32_t s_reverses_in_jam;

feeder_sim_cfg_t feeder_sim_default_cfg(void)
{
    feeder_sim_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.hole_period_ms = 6400u;
    cfg.open_window_ms = 200u;
    cfg.springback_ms = 120u;
    cfg.start_pos_ms = 50u;           /* parked inside the hole: beam open */
    cfg.flicker_ms = 0u;
    cfg.run_ma = 110u;
    cfg.run_ma_noise = 40u;
    cfg.reverse_ma = 130u;
    cfg.stall_ma = 280u;
    cfg.jam_at_ms = FEEDER_SIM_NEVER;
    cfg.jam_clear_reverses = 1u;
    cfg.dg_per_hole = 85u;
    cfg.dg_noise_pct = 0u;
    cfg.hopper_dg = 100000u;
    cfg.busy_pct = 0u;
    cfg.seed = 1u;
    return cfg;
}

static uint32_t sim_rand(void)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return (s_rng >> 16) & 0x7FFFu;
}

static int64_t sim_period(void)
{
    return (int64_t)s_cfg.hole_period_ms;
}

static int64_t sim_hole_index(int64_t pos)
{
    /* floor division for negative positions */
    return (pos >= 0) ? pos / sim_period() : -((-pos + sim_period() - 1) / sim_period());
}

static bool sim_in_hole(void)
{
    int64_t phase = s_pos - sim_hole_index(s_pos) * sim_period();

    return phase < (int64_t)s_cfg.open_window_ms;
}

bool feeder_sim_beam_open(void)
{
    if (s_drive == SIM_FWD && s_now < s_flicker_until) {
        return true;
    }
    return sim_in_hole();
}

static void sim_drop_kibble(int64_t hole)
{
    uint32_t dg = s_cfg.dg_per_hole;

    if (s_cfg.dg_noise_pct > 0u && dg > 0u) {
        uint32_t span = (dg * s_cfg.dg_noise_pct) / 100u;

        if (span > 0u) {
            dg = dg - span + (sim_rand() % (2u * span + 1u));
        }
    }
    if (dg > s_cfg.hopper_dg) {
        dg = s_cfg.hopper_dg;
    }
    s_cfg.hopper_dg -= dg;
    s_last_hole = hole;
    if (dg > 0u) {
        s_stats.holes_dropped++;
        s_stats.dropped_dg += dg;
    }
}

static void sim_move(uint32_t dt)
{
    uint32_t i;

    for (i = 0u; i < dt; i++) {
        if (s_drive == SIM_FWD) {
            if (!s_stats.jammed && s_cfg.jam_at_ms != FEEDER_SIM_NEVER &&
                s_stats.forward_ms >= s_cfg.jam_at_ms) {
                s_stats.jammed = true;
                s_cfg.jam_at_ms = FEEDER_SIM_NEVER;
                s_reverses_in_jam = 0u;
            }
            s_stats.forward_ms++;
            if (!s_stats.jammed) {
                s_pos++;
                /* Kibble falls when a new hole reaches the chute. */
                if (sim_hole_index(s_pos) > s_last_hole &&
                    s_pos - sim_hole_index(s_pos) * sim_period() == 0) {
                    sim_drop_kibble(sim_hole_index(s_pos));
                }
            }
        } else if (s_drive == SIM_REV) {
            if (!s_stats.jammed) {
                s_pos--;
            }
        }
    }
    s_stats.elapsed_ms += dt;
}

static sim_drive_t sim_read_drive(void)
{
    bool en = fake_gpio_expander_pin(BOARD_GPIO_MOTOR_EN_PORT, BOARD_GPIO_MOTOR_EN_PIN);
    bool ph = fake_gpio_expander_pin(BOARD_GPIO_MOTOR_PH_PORT, BOARD_GPIO_MOTOR_PH_PIN);

    if (!en) {
        return SIM_STOP;
    }
    return ph ? SIM_FWD : SIM_REV;
}

static void sim_apply_drive(sim_drive_t next)
{
    if (next == s_drive) {
        return;
    }
    if (s_drive == SIM_FWD && next != SIM_FWD && !s_stats.jammed && sim_in_hole()) {
        /* Spring-back: the auger relaxes to just before the hole it stopped in. */
        s_pos = sim_hole_index(s_pos) * sim_period() - (int64_t)s_cfg.springback_ms;
    }
    if (next == SIM_REV) {
        s_stats.reverse_phases++;
        if (s_stats.jammed && s_cfg.jam_clear_reverses > 0u &&
            ++s_reverses_in_jam >= s_cfg.jam_clear_reverses) {
            s_stats.jammed = false;
        }
    }
    if (next == SIM_FWD) {
        s_flicker_until = s_now + s_cfg.flicker_ms;
    }
    s_drive = next;
}

static void sim_publish_sensors(void)
{
    uint16_t ma = 0u;

    fake_gpio_expander_set_index_beam_open(feeder_sim_beam_open());

    if (s_drive == SIM_FWD) {
        if (s_stats.jammed) {
            ma = s_cfg.stall_ma;
        } else {
            ma = s_cfg.run_ma;
            if (s_cfg.run_ma_noise > 0u) {
                ma = (uint16_t)(ma - s_cfg.run_ma_noise +
                                sim_rand() % (2u * s_cfg.run_ma_noise + 1u));
            }
        }
    } else if (s_drive == SIM_REV) {
        ma = s_cfg.reverse_ma;
    }
    fake_adc_port_set_motor_ma(ma);

    fake_wfci_bus_set_contended(s_cfg.busy_pct > 0u &&
                                (sim_rand() % 100u) < s_cfg.busy_pct);
}

static void sim_sync_clock(void)
{
    uint32_t now = (uint32_t)fake_time_ticks();

    if (now > s_now) {
        sim_move(now - s_now);
    }
    s_now = now;
}

void feeder_sim_begin(const feeder_sim_cfg_t *cfg)
{
    s_cfg = (cfg != NULL) ? *cfg : feeder_sim_default_cfg();
    memset(&s_stats, 0, sizeof(s_stats));
    s_pos = (int64_t)(s_cfg.start_pos_ms % s_cfg.hole_period_ms);
    /* The hole the auger rests in (or just left) has already been emptied. */
    s_last_hole = sim_hole_index(s_pos);
    s_drive = SIM_STOP;
    s_now = (uint32_t)fake_time_ticks();
    s_flicker_until = 0u;
    s_rng = s_cfg.seed;
    s_reverses_in_jam = 0u;
    sim_publish_sensors();
}

void feeder_sim_step(void)
{
    sim_sync_clock();
    sim_publish_sensors();
    motor_ctrl_test_poll();
    /* Delays inside the poll (LED settle, PH settle) moved the auger with the
     * drive that was active during them. */
    sim_sync_clock();
    sim_apply_drive(sim_read_drive());
    sim_publish_sensors();
    fake_time_advance_ms(MOTOR_CTRL_LOOP_SLICE_MS);
}

bool feeder_sim_run_until_idle(uint32_t max_ms)
{
    uint32_t start = (uint32_t)fake_time_ticks();

    /* First step picks up the queued command. */
    feeder_sim_step();
    while (motor_ctrl_is_active() || s_drive != SIM_STOP) {
        if ((uint32_t)fake_time_ticks() - start >= max_ms) {
            return false;
        }
        feeder_sim_step();
    }
    sim_sync_clock();
    return true;
}

void feeder_sim_run_ms(uint32_t ms)
{
    uint32_t start = (uint32_t)fake_time_ticks();

    while ((uint32_t)fake_time_ticks() - start < ms) {
        feeder_sim_step();
    }
    sim_sync_clock();
}

feeder_sim_stats_t feeder_sim_stats(void)
{
    return s_stats;
}
