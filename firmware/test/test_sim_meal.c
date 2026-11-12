/* Whole meals against the physical simulator: real app, dispense job, motor
 * task, index adapter and motor driver; simulated auger, kibble and scale.
 * rewrite-kit/06 scenarios S7-S12, spec/30-processes/dispense-cycle.md */

#include <string.h>

#include "unity.h"

#include "app.h"
#include "app_event.h"
#include "app_event_port.h"
#include "auto_tare.h"
#include "dispense.h"
#include "dispense_cli.h"
#include "display_presentation.h"
#include "fake_adc_port.h"
#include "fake_config_port.h"
#include "fake_display_port.h"
#include "fake_gpio_expander_port.h"
#include "fake_time.h"
#include "fake_wfci_bus_port.h"
#include "fake_weight_port.h"
#include "feed_history.h"
#include "feed_limit.h"
#include "feeder_runtime.h"
#include "feeder_sim.h"
#include "motor_ctrl.h"
#include "motor_ctrl_test.h"
#include "motor_index_port_provider_host.h"
#include "motor_port_provider_host.h"
#include "portion_estimate.h"
#include "problem.h"
#include "settings.h"
#include "weight_units.h"

extern void fake_app_event_q_reset(void);
extern void freertos_notify_test_reset(void);

static void meal_setup(feeder_sim_cfg_t *cfg)
{
    fake_time_reset();
    fake_config_port_reset();
    fake_weight_port_reset();
    fake_weight_port_set_cal_status(WEIGHT_CAL_SUCCESS);
    fake_app_event_q_reset();
    fake_display_port_reset();
    display_presentation_reset();
    dispense_test_reset();
    dispense_cli_test_reset();
    app_test_reset();
    auto_tare_test_reset();
    feeder_runtime_test_reset();
    feed_history_test_reset();
    feed_limit_test_reset();
    problem_test_reset();

    motor_index_port_host_reset();
    motor_port_host_reset();
    fake_gpio_expander_reset();
    fake_wfci_bus_reset();
    fake_adc_port_reset();
    freertos_notify_test_reset();
    app_event_port_init();
    motor_index_port_host_use_adapter(true);
    motor_port_host_use_integration(true);
    motor_ctrl_start();
    motor_ctrl_test_reset();

    cfg->scale = true;
    feeder_sim_begin(cfg);
    auto_tare_anchor(cfg->bowl_present ? cfg->food_dg : -cfg->bowl_dg);
}

static uint32_t meal_now_ms(void)
{
    return (uint32_t)fake_time_ticks();
}

/* Run motor, app and dispense job together until the job ends. */
static void meal_run(uint32_t max_ms)
{
    uint32_t start = meal_now_ms();

    while (app_step()) {
    }
    while (dispense_is_active() || motor_ctrl_is_active()) {
        TEST_ASSERT_TRUE_MESSAGE(meal_now_ms() - start < max_ms, "meal never ended");
        feeder_sim_step();
        while (app_step()) {
        }
        dispense_poll(meal_now_ms());
    }
}

static feed_record_t meal_last_record(void)
{
    feed_record_t r;

    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(feed_history_count() > 0u);
    TEST_ASSERT_TRUE(feed_history_get(0u, &r));   /* newest */
    return r;
}

static void meal_compensated(void)
{
    TEST_ASSERT_EQUAL(PORT_OK, settings_set(SETTING_FEED_MODE, (uint16_t)DISPENSE_MODE_COMPENSATED));
}

/* S7: compensated 30 g at 8.5 g per hole (the default estimate is 10 g):
 * at most 2 top-ups, delivered within tolerance, the estimate learns. */
void test_sim_s7_compensated_meal_reaches_target(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feed_record_t r;
    weight_dg_t before;

    meal_setup(&cfg);
    meal_compensated();
    before = portion_estimate_dg();
    TEST_ASSERT_EQUAL(DISPENSE_SUBMIT_OK, dispense_submit_grams(30u, DISPENSE_SOURCE_SCHEDULE));
    meal_run(240000u);

    r = meal_last_record();
    TEST_ASSERT_EQUAL_STRING("ok", r.res);
    TEST_ASSERT_TRUE(feeder_sim_food_dg() >= WEIGHT_G_TO_DG(30 - DISPENSE_COMP_TOLERANCE_G));
    TEST_ASSERT_TRUE(feeder_sim_food_dg() <= WEIGHT_G_TO_DG(30 + 9));
    TEST_ASSERT_TRUE(feeder_sim_stats().holes_dropped <= 5u);
    TEST_ASSERT_TRUE_MESSAGE(portion_estimate_dg() < before, "estimate did not learn");
    TEST_ASSERT_FALSE(problem_is_set(PROBLEM_FEED_FAILED));
}

/* S7b: after a few meals the estimate converges near the real 8.5 g. */
void test_sim_s7_estimate_converges_over_meals(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    unsigned i;

    meal_setup(&cfg);
    meal_compensated();
    for (i = 0u; i < 6u; i++) {
        TEST_ASSERT_EQUAL(DISPENSE_SUBMIT_OK, dispense_submit_grams(30u, DISPENSE_SOURCE_SCHEDULE));
        meal_run(240000u);
    }
    TEST_ASSERT_INT32_WITHIN(8, 85, portion_estimate_dg());
}

/* S9: no bowl. The meal still runs (owner's decision), E3 raised, nothing learned. */
void test_sim_s9_missing_bowl_still_feeds_with_alert(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feed_record_t r;
    weight_dg_t before;

    cfg.bowl_present = false;
    meal_setup(&cfg);
    before = portion_estimate_dg();
    TEST_ASSERT_EQUAL(DISPENSE_SUBMIT_OK, dispense_submit_grams(20u, DISPENSE_SOURCE_SCHEDULE));
    meal_run(240000u);

    r = meal_last_record();
    TEST_ASSERT_TRUE(feeder_sim_stats().holes_dropped >= 2u);
    TEST_ASSERT_FALSE(r.bowl);
    TEST_ASSERT_TRUE(problem_is_set(PROBLEM_BOWL_MISSING));
    TEST_ASSERT_EQUAL_INT32(before, portion_estimate_dg());
}

/* S10: the hopper IR still sees food but nothing falls (bridged kibble):
 * E1 "nothing fell", not an empty hopper. */
void test_sim_s10_nothing_fell_raises_feed_failed(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();

    cfg.hopper_dg = 0u;
    meal_setup(&cfg);
    TEST_ASSERT_EQUAL(DISPENSE_SUBMIT_OK, dispense_submit_grams(20u, DISPENSE_SOURCE_SCHEDULE));
    meal_run(240000u);

    TEST_ASSERT_TRUE(problem_is_set(PROBLEM_FEED_FAILED));
    TEST_ASSERT_FALSE(problem_is_set(PROBLEM_HOPPER_EMPTY));
}

/* S12: daily limit 40 g; meals of 30 g: the first is served, the second is
 * cut to what is left (no top-up past it), the third is refused. The total
 * never passes the limit. */
void test_sim_s12_daily_limit_shortens_then_refuses(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    feed_record_t r;

    meal_setup(&cfg);
    meal_compensated();
    TEST_ASSERT_EQUAL(PORT_OK, settings_set(SETTING_DAILY_MAX_G, 40u));

    TEST_ASSERT_EQUAL(DISPENSE_SUBMIT_OK, dispense_submit_grams(30u, DISPENSE_SOURCE_SCHEDULE));
    meal_run(240000u);
    TEST_ASSERT_EQUAL(DISPENSE_SUBMIT_OK, dispense_submit_grams(30u, DISPENSE_SOURCE_SCHEDULE));
    meal_run(240000u);
    r = meal_last_record();
    TEST_ASSERT_TRUE_MESSAGE(r.got <= 15, "second meal not shortened");

    (void)dispense_submit_grams(30u, DISPENSE_SOURCE_SCHEDULE);
    meal_run(240000u);
    r = meal_last_record();
    TEST_ASSERT_EQUAL_STRING("limit", r.res);
    TEST_ASSERT_TRUE(problem_is_set(PROBLEM_DAILY_LIMIT));
    TEST_ASSERT_TRUE(feeder_sim_food_dg() <= WEIGHT_G_TO_DG(40));
}

/* S8: the dog eats 10 g while the scale settles after the first batch. The
 * drop must not be taken as kibble that never fell: no extra top-up for it,
 * and the portion estimate does not learn from that meal. */
void test_sim_s8_eating_during_settle_is_not_underdelivery(void)
{
    feeder_sim_cfg_t cfg = feeder_sim_default_cfg();
    weight_dg_t before;

    cfg.eat_at_ms = 27000u;     /* 4 holes end ~25.6 s; settle runs 3 s */
    cfg.eat_dg = 100;
    meal_setup(&cfg);
    meal_compensated();
    before = portion_estimate_dg();
    TEST_ASSERT_EQUAL(DISPENSE_SUBMIT_OK, dispense_submit_grams(40u, DISPENSE_SOURCE_SCHEDULE));
    meal_run(240000u);

    TEST_ASSERT_TRUE_MESSAGE(feeder_sim_stats().dropped_dg <= WEIGHT_G_TO_DG(40 + 9),
                             "topped up for what the dog ate");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(before, portion_estimate_dg(), "learned from a disturbed meal");
}
