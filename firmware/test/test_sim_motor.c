/* Motor burst against the physical simulator — rewrite-kit/06 scenarios S1-S6.
 * spec/30-processes/motor-index.md, rewrite-kit/01-physical-behavior.md */

#include <string.h>

#include "unity.h"

#include "app_event.h"
#include "app_event_port.h"
#include "fake_adc_port.h"
#include "fake_gpio_expander_port.h"
#include "fake_time.h"
#include "fake_wfci_bus_port.h"
#include "feeder_sim.h"
#include "motor_ctrl.h"
#include "motor_ctrl_test.h"
#include "motor_index_port_provider_host.h"
#include "motor_jam.h"
#include "motor_port_provider_host.h"

extern void fake_app_event_q_reset(void);
extern void freertos_notify_test_reset(void);

static void sim_motor_setup(const feeder_sim_cfg_t *cfg)
{
    motor_index_port_host_reset();
    motor_port_host_reset();
    fake_gpio_expander_reset();
    fake_wfci_bus_reset();
    fake_adc_port_reset();
    fake_time_reset();
    freertos_notify_test_reset();
    fake_app_event_q_reset();
    app_event_port_init();
    motor_index_port_host_use_adapter(true);
    motor_port_host_use_integration(true);
    motor_ctrl_start();
    motor_ctrl_test_reset();
    feeder_sim_begin(cfg);
}

static int sim_motor_last_event(void)
{
    app_event_t ev;
    int type = -1;

    while (app_event_try_receive(&ev)) {
        if (ev.type == EVT_BURST_DONE || ev.type == EVT_MOTOR_FAULT) {
            type = (int)ev.type;
        }
        app_event_release(&ev);
    }
    return type;
}

static feeder_sim_stats_t sim_motor_burst(uint8_t portions, uint32_t max_ms)
{
    TEST_ASSERT_EQUAL(PORT_OK, motor_ctrl_request_burst(portions, MOTOR_BURST_TIMEOUT_MS));
    TEST_ASSERT_TRUE_MESSAGE(feeder_sim_run_until_idle(max_ms), "motor never went idle");
    return feeder_sim_stats();
}

/* S1: 1 portion from the parked position (beam open): one full hole. */
void test_sim_s1_one_portion_from_parked_open(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feeder_sim_stats_t st;
    motor_run_stats_t run;

    sim_motor_setup(&cfg);
    st = sim_motor_burst(1u, 30000u);
    motor_ctrl_last_run(&run);

    TEST_ASSERT_EQUAL(EVT_BURST_DONE, sim_motor_last_event());
    TEST_ASSERT_EQUAL_UINT32(1u, st.holes_dropped);
    TEST_ASSERT_EQUAL_UINT32(85u, st.dropped_dg);
    TEST_ASSERT_EQUAL_UINT8(1u, run.pulses);
}

/* S1b: after a burst the auger springs back before the hole; the next start
 * re-enters that same hole within 160 ms. It must not count as a portion. */
void test_sim_s1_springback_reentry_is_not_a_portion(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feeder_sim_stats_t st;

    sim_motor_setup(&cfg);
    st = sim_motor_burst(1u, 30000u);
    TEST_ASSERT_EQUAL_UINT32(1u, st.holes_dropped);
    TEST_ASSERT_FALSE(feeder_sim_beam_open());   /* sprung back */
    (void)sim_motor_last_event();

    st = sim_motor_burst(1u, 30000u);
    TEST_ASSERT_EQUAL(EVT_BURST_DONE, sim_motor_last_event());
    TEST_ASSERT_EQUAL_UINT32(2u, st.holes_dropped);
}

/* S2: the start-up jolt makes the beam flicker open; ignored. */
void test_sim_s2_start_flicker_is_not_a_portion(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feeder_sim_stats_t st;

    cfg.start_pos_ms = 3000u;   /* between holes: beam closed */
    cfg.flicker_ms = 60u;
    sim_motor_setup(&cfg);
    st = sim_motor_burst(1u, 30000u);

    TEST_ASSERT_EQUAL(EVT_BURST_DONE, sim_motor_last_event());
    TEST_ASSERT_EQUAL_UINT32(1u, st.holes_dropped);
}

/* S3: 5 portions at 6.4 s per hole complete in ~32 s without a session timeout. */
void test_sim_s3_five_portions_at_field_speed(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feeder_sim_stats_t st;
    motor_run_stats_t run;

    sim_motor_setup(&cfg);
    st = sim_motor_burst(5u, 60000u);
    motor_ctrl_last_run(&run);

    TEST_ASSERT_EQUAL(EVT_BURST_DONE, sim_motor_last_event());
    TEST_ASSERT_EQUAL_UINT32(5u, st.holes_dropped);
    TEST_ASSERT_EQUAL_UINT8(5u, run.pulses);
    TEST_ASSERT_UINT32_WITHIN(1500u, 32000u, run.dur_ms);
}

/* S4 (field log 2026-11-10): 13 portions, the auger jams 25 s in; one
 * reverse clears it. The meal completes with exactly 13 holes. */
void test_sim_s4_late_jam_cleared_by_antijam(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feeder_sim_stats_t st;
    motor_run_stats_t run;

    cfg.jam_at_ms = 25000u;
    cfg.jam_clear_reverses = 1u;
    sim_motor_setup(&cfg);
    st = sim_motor_burst(13u, 180000u);
    motor_ctrl_last_run(&run);

    TEST_ASSERT_EQUAL(EVT_BURST_DONE, sim_motor_last_event());
    TEST_ASSERT_EQUAL_UINT32(13u, st.holes_dropped);
    TEST_ASSERT_EQUAL_UINT8(13u, run.pulses);
    TEST_ASSERT_EQUAL_UINT8(1u, run.antijam);
    TEST_ASSERT_EQUAL_STRING("stall", run.jam);
}

/* S5: a jam that never clears: 3 anti-jam tries, then a fault. */
void test_sim_s5_permanent_jam_faults_after_three_tries(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feeder_sim_stats_t st;
    motor_run_stats_t run;

    cfg.jam_at_ms = 3000u;
    cfg.jam_clear_reverses = 0u;
    sim_motor_setup(&cfg);
    st = sim_motor_burst(2u, 120000u);
    motor_ctrl_last_run(&run);

    TEST_ASSERT_EQUAL(EVT_MOTOR_FAULT, sim_motor_last_event());
    TEST_ASSERT_EQUAL_UINT32(0u, st.holes_dropped);
    TEST_ASSERT_EQUAL_UINT8(MOTOR_ANTI_JAM_MAX_RETRIES, run.antijam);
    TEST_ASSERT_EQUAL_STRING("antijam", run.why);
}

/* S6: Wi-Fi holds the shared bus 30 % of the time; holes still count right. */
void test_sim_s6_bus_contention_keeps_count(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feeder_sim_stats_t st;
    motor_run_stats_t run;

    cfg.busy_pct = 30u;
    cfg.seed = 7u;
    sim_motor_setup(&cfg);
    st = sim_motor_burst(3u, 60000u);
    motor_ctrl_last_run(&run);

    TEST_ASSERT_EQUAL(EVT_BURST_DONE, sim_motor_last_event());
    TEST_ASSERT_EQUAL_UINT32(3u, st.holes_dropped);
    TEST_ASSERT_EQUAL_UINT8(3u, run.pulses);
    TEST_ASSERT_TRUE(run.poll_busy > 0u);
}

/* Empty hopper: the auger turns, holes pass, nothing falls. */
void test_sim_empty_hopper_turns_without_kibble(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feeder_sim_stats_t st;
    motor_run_stats_t run;

    cfg.hopper_dg = 0u;
    sim_motor_setup(&cfg);
    st = sim_motor_burst(2u, 60000u);
    motor_ctrl_last_run(&run);

    TEST_ASSERT_EQUAL(EVT_BURST_DONE, sim_motor_last_event());
    TEST_ASSERT_EQUAL_UINT32(0u, st.dropped_dg);
    TEST_ASSERT_EQUAL_UINT8(2u, run.pulses);
}
