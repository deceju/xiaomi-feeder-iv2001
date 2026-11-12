/*
 * Daily limit and not-eaten alert — spec/30-processes/health-monitoring.md
 */

#include "feed_limit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_log.h"
#include "eaten_today.h"
#include "feed_history.h"
#include "problem.h"
#include "settings.h"

/* Not-eaten tracking: eaten-today made cumulative across resets. */
static bool s_seen_total;
static weight_dg_t s_last_total;
static int32_t s_cum_offset;
static bool s_armed;
static uint32_t s_feed_ms;
static int32_t s_feed_cum;
static bool s_alerted;
static int32_t s_alert_cum;

uint16_t feed_limit_daily_max_g(void)
{
    return settings_get(SETTING_DAILY_MAX_G);
}

uint8_t feed_limit_not_eaten_h(void)
{
    return (uint8_t)settings_get(SETTING_NOT_EATEN_H);
}

bool feed_limit_set_daily_max_g(uint16_t g)
{
    return settings_set(SETTING_DAILY_MAX_G, g) == PORT_OK;
}

bool feed_limit_set_not_eaten_h(uint8_t h)
{
    return settings_set(SETTING_NOT_EATEN_H, h) == PORT_OK;
}

/* The alert was switched off: stop watching and drop a raised alert. */
void feed_not_eaten_disarm(void)
{
    s_armed = false;
    problem_clear(PROBLEM_NOT_EATEN);
}

bool feed_limit_remaining_g(uint32_t *remaining_g)
{
    uint32_t today;
    uint16_t max_g = feed_limit_daily_max_g();

    if (max_g == 0u) {
        return false;
    }
    today = feed_history_grams_on_day(eaten_today_day_key_now());
    if (remaining_g != NULL) {
        *remaining_g = today >= max_g ? 0u : (uint32_t)max_g - today;
    }
    return true;
}

static int32_t feed_not_eaten_cum(void)
{
    return s_cum_offset + (int32_t)s_last_total;
}

void feed_not_eaten_on_feed(weight_dg_t delivered_dg, uint32_t now_ms)
{
    if (delivered_dg < FEED_NOT_EATEN_MIN_DG) {
        return;
    }
    s_armed = true;
    s_feed_ms = now_ms;
    s_feed_cum = feed_not_eaten_cum();
}

void feed_not_eaten_poll(weight_dg_t eaten_today_dg, uint32_t now_ms)
{
    uint8_t h = feed_limit_not_eaten_h();

    if (!s_seen_total) {
        s_seen_total = true;
        s_last_total = eaten_today_dg;
    } else if (eaten_today_dg < s_last_total) {
        s_cum_offset += (int32_t)s_last_total;      /* midnight or manual reset */
        s_last_total = eaten_today_dg;
    } else {
        s_last_total = eaten_today_dg;
    }

    if (s_alerted && feed_not_eaten_cum() - s_alert_cum >= FEED_NOT_EATEN_MIN_DG) {
        s_alerted = false;
        problem_clear(PROBLEM_NOT_EATEN);
        app_log_info("health", "not eaten: cleared (eating detected)");
    }
    if (s_alerted && !problem_is_set(PROBLEM_NOT_EATEN)) {
        s_alerted = false;                            /* cleared by the button */
    }

    if (h == 0u || !s_armed) {
        return;
    }
    if (feed_not_eaten_cum() - s_feed_cum >= FEED_NOT_EATEN_MIN_DG) {
        s_armed = false;                              /* ate: nothing to watch */
        return;
    }
    if ((uint32_t)(now_ms - s_feed_ms) >= (uint32_t)h * 3600000u) {
        char detail[24];

        s_armed = false;
        s_alerted = true;
        s_alert_cum = feed_not_eaten_cum();
        (void)snprintf(detail, sizeof(detail), "%u h", (unsigned)h);
        problem_raise(PROBLEM_NOT_EATEN, detail);
        app_log_info("health", "not eaten %u h after the last meal", (unsigned)h);
    }
}

void feed_limit_test_reset(void)
{
    s_seen_total = false;
    s_last_total = 0;
    s_cum_offset = 0;
    s_armed = false;
    s_alerted = false;
}
