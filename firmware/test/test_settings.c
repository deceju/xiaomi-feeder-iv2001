/* Settings registry — spec/30-processes/config-store.md § Settings registry,
 * rewrite-kit/07 L14, L19, L24, S20. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "test_mqtt_drain.h"

#include "config_port.h"
#include "fake_config_port.h"
#include "fake_mqtt_port.h"
#include "feed_limit.h"
#include "mqtt_client_test.h"
#include "mqtt_ha_discovery.h"
#include "mqtt_outbox.h"
#include "mqtt_settings.h"
#include "problem.h"
#include "settings.h"
#include "web_admin.h"

static void settings_test_reset(void)
{
    fake_config_port_reset();
    mqtt_settings_test_reset();
    feed_limit_test_reset();
    problem_test_reset();
}

static char s_cmd[128];

static port_err_t settings_test_cmd(mqtt_settings_topic_t t, const char *json)
{
    (void)snprintf(s_cmd, sizeof(s_cmd), "%s", json);
    return mqtt_settings_handle(t, s_cmd, strlen(s_cmd));
}

void test_settings_table_is_consistent(void)
{
    unsigned i;
    unsigned j;

    for (i = 0u; i < SETTING_COUNT; i++) {
        const setting_def_t *d = settings_def((setting_id_t)i);

        TEST_ASSERT_NOT_NULL(d->group);
        TEST_ASSERT_NOT_NULL(d->key);
        TEST_ASSERT_TRUE_MESSAGE(d->field != NULL && d->field[0] != '\0', d->name);
        TEST_ASSERT_TRUE_MESSAGE(settings_valid((setting_id_t)i, d->def) ||
                                     (d->def == 0u && (d->flags & SETTING_F_UNSET) != 0u),
                                 d->name);
        if (d->type == SETTING_ENUM) {
            TEST_ASSERT_NOT_NULL_MESSAGE(d->options, d->name);
        }
        /* Every setting can be read and written over MQTT (L24). */
        TEST_ASSERT_TRUE_MESSAGE(mqtt_settings_topic_of((setting_id_t)i, NULL), d->name);
        for (j = i + 1u; j < SETTING_COUNT; j++) {
            const setting_def_t *e = settings_def((setting_id_t)j);

            TEST_ASSERT_FALSE_MESSAGE(strcmp(d->group, e->group) == 0 &&
                                          strcmp(d->key, e->key) == 0,
                                      d->name);
        }
    }
}

void test_settings_defaults_when_unset(void)
{
    settings_test_reset();
    TEST_ASSERT_EQUAL_UINT16(50u, settings_get(SETTING_OVERFILL_THRESHOLD_G));
    TEST_ASSERT_EQUAL_UINT16(4u, settings_get(SETTING_DISPLAY_BRIGHTNESS));
    TEST_ASSERT_EQUAL_UINT16(0u, settings_get(SETTING_DAILY_MAX_G));
    TEST_ASSERT_EQUAL_STRING("open_loop",
                             settings_enum_name(SETTING_FEED_MODE, settings_get(SETTING_FEED_MODE)));
}

void test_settings_reads_values_stored_by_earlier_firmware(void)
{
    const config_port_t *cfg = config_port_get();

    settings_test_reset();
    TEST_ASSERT_EQUAL(PORT_OK, cfg->write("feed", "overfill_enabled", "true"));
    TEST_ASSERT_EQUAL(PORT_OK, cfg->write("feed", "mode", "compensated"));
    TEST_ASSERT_EQUAL(PORT_OK, cfg->write("power", "battery_wifi", "scheduled_only"));
    TEST_ASSERT_TRUE(settings_get_bool(SETTING_OVERFILL_ENABLED));
    TEST_ASSERT_EQUAL_UINT16(1u, settings_get(SETTING_FEED_MODE));
    TEST_ASSERT_EQUAL_UINT16(2u, settings_get(SETTING_BATTERY_WIFI));
}

void test_settings_out_of_range_stored_value_clamps_or_defaults(void)
{
    const config_port_t *cfg = config_port_get();

    settings_test_reset();
    TEST_ASSERT_EQUAL(PORT_OK, cfg->write("feed", "overfill_threshold_g", "900"));
    TEST_ASSERT_EQUAL_UINT16(500u, settings_get(SETTING_OVERFILL_THRESHOLD_G));
    TEST_ASSERT_EQUAL(PORT_OK, cfg->write("display", "brightness", "9"));
    TEST_ASSERT_EQUAL_UINT16(4u, settings_get(SETTING_DISPLAY_BRIGHTNESS));
    TEST_ASSERT_EQUAL(PORT_OK, cfg->write("display", "brightness", "2x"));
    TEST_ASSERT_EQUAL_UINT16(4u, settings_get(SETTING_DISPLAY_BRIGHTNESS));
}

void test_settings_zero_means_off_only_where_allowed(void)
{
    settings_test_reset();
    TEST_ASSERT_TRUE(settings_valid(SETTING_DAILY_MAX_G, 0u));
    TEST_ASSERT_FALSE(settings_valid(SETTING_DAILY_MAX_G, 5u));
    TEST_ASSERT_TRUE(settings_valid(SETTING_DAILY_MAX_G, 10u));
    TEST_ASSERT_FALSE(settings_valid(SETTING_OVERFILL_THRESHOLD_G, 0u));
}

void test_settings_unchanged_write_does_not_touch_flash(void)
{
    unsigned before;

    settings_test_reset();
    TEST_ASSERT_EQUAL(PORT_OK, settings_set(SETTING_DISPLAY_BRIGHTNESS, 2u));
    before = fake_config_port_write_count();
    TEST_ASSERT_EQUAL(PORT_OK, settings_set(SETTING_DISPLAY_BRIGHTNESS, 2u));
    TEST_ASSERT_EQUAL_UINT(before, fake_config_port_write_count());
}

/* L14: a threshold of 300 g must not be narrowed to 8 bits before checking. */
void test_settings_mqtt_overfill_threshold_300_stays_300(void)
{
    settings_test_reset();
    TEST_ASSERT_EQUAL(PORT_OK, settings_test_cmd(MQTT_SETTINGS_OVERFILL, "{\"threshold_g\":300}"));
    TEST_ASSERT_EQUAL_UINT16(300u, settings_get(SETTING_OVERFILL_THRESHOLD_G));
    TEST_ASSERT_EQUAL(PORT_ERR_INVALID_ARG,
                      settings_test_cmd(MQTT_SETTINGS_OVERFILL, "{\"threshold_g\":65836}"));
    TEST_ASSERT_EQUAL_UINT16(300u, settings_get(SETTING_OVERFILL_THRESHOLD_G));
}

/* A command is all-or-nothing: one bad field rejects the whole command. */
void test_settings_mqtt_command_is_all_or_nothing(void)
{
    settings_test_reset();
    TEST_ASSERT_EQUAL(PORT_ERR_INVALID_ARG,
                      settings_test_cmd(MQTT_SETTINGS_DISPLAY,
                                        "{\"brightness\":1,\"mains_off_s\":99999}"));
    TEST_ASSERT_EQUAL_UINT16(4u, settings_get(SETTING_DISPLAY_BRIGHTNESS));

    TEST_ASSERT_EQUAL(PORT_ERR_INVALID_ARG,
                      settings_test_cmd(MQTT_SETTINGS_FEED_LIMIT,
                                        "{\"daily_max_g\":200,\"not_eaten_h\":99}"));
    TEST_ASSERT_EQUAL_UINT16(0u, settings_get(SETTING_DAILY_MAX_G));
}

void test_settings_mqtt_bare_payloads(void)
{
    settings_test_reset();
    TEST_ASSERT_EQUAL(PORT_OK, settings_test_cmd(MQTT_SETTINGS_CHILD_LOCK, " ON\n"));
    TEST_ASSERT_TRUE(settings_get_bool(SETTING_CHILD_LOCK));
    TEST_ASSERT_EQUAL(PORT_ERR_INVALID_ARG, settings_test_cmd(MQTT_SETTINGS_CHILD_LOCK, "on"));
    TEST_ASSERT_EQUAL(PORT_OK, settings_test_cmd(MQTT_SETTINGS_FEED_MODE, "compensated"));
    TEST_ASSERT_EQUAL_UINT16(1u, settings_get(SETTING_FEED_MODE));
    TEST_ASSERT_EQUAL(PORT_ERR_INVALID_ARG, settings_test_cmd(MQTT_SETTINGS_FEED_MODE, "fast"));
}

void test_settings_state_payloads_match_the_protocol(void)
{
    char buf[128];

    settings_test_reset();
    TEST_ASSERT_TRUE(mqtt_settings_format(MQTT_SETTINGS_DISPLAY, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"mode\":\"weight\",\"brightness\":4,\"battery_off_s\":60,\"mains_off_s\":0}", buf);
    TEST_ASSERT_TRUE(mqtt_settings_format(MQTT_SETTINGS_OVERFILL, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"enabled\":false,\"threshold_g\":50}", buf);
    TEST_ASSERT_TRUE(mqtt_settings_format(MQTT_SETTINGS_POWER, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"battery_wifi\":\"on\"}", buf);
    TEST_ASSERT_TRUE(mqtt_settings_format(MQTT_SETTINGS_CHILD_LOCK, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("OFF", buf);
}

void test_settings_not_eaten_off_clears_the_alert(void)
{
    settings_test_reset();
    TEST_ASSERT_EQUAL(PORT_OK, settings_test_cmd(MQTT_SETTINGS_FEED_LIMIT, "{\"not_eaten_h\":6}"));
    problem_raise(PROBLEM_NOT_EATEN, "6 h");
    TEST_ASSERT_EQUAL(PORT_OK, settings_test_cmd(MQTT_SETTINGS_FEED_LIMIT, "{\"not_eaten_h\":0}"));
    TEST_ASSERT_FALSE(problem_is_set(PROBLEM_NOT_EATEN));
}

/* Export carries every exportable setting, the daily limit included. */
void test_settings_export_includes_every_exportable_setting(void)
{
    static char buf[2048];
    char needle[64];
    unsigned i;

    settings_test_reset();
    for (i = 0u; i < SETTING_COUNT; i++) {
        const setting_def_t *d = settings_def((setting_id_t)i);
        uint16_t v = 1u;   /* a valid value other than the default: stored */

        if (d->type == SETTING_UINT) {
            v = (d->def != d->max) ? d->max : d->min;
        } else if (d->def == 1u) {
            v = 0u;
        }
        TEST_ASSERT_EQUAL_MESSAGE(PORT_OK, settings_set((setting_id_t)i, v), d->name);
    }
    TEST_ASSERT_TRUE(web_admin_export(buf, sizeof(buf)) > 0);
    for (i = 0u; i < SETTING_COUNT; i++) {
        const setting_def_t *d = settings_def((setting_id_t)i);

        (void)snprintf(needle, sizeof(needle), "\"%s/%s\"", d->group, d->key);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, needle), needle);
    }
}

/* S20: import validates registry values instead of writing them raw. */
void test_settings_import_rejects_out_of_range_values(void)
{
    static char body[256];

    settings_test_reset();
    (void)snprintf(body, sizeof(body),
                   "{\"feed/overfill_threshold_g\":\"9000\",\"display/brightness\":\"2\","
                   "\"feed/daily_max_g\":\"5\",\"wifi/pass\":\"x\"}");
    TEST_ASSERT_EQUAL(PORT_OK, web_admin_run(WEB_ADMIN_IMPORT, body, strlen(body)));
    TEST_ASSERT_EQUAL_UINT16(50u, settings_get(SETTING_OVERFILL_THRESHOLD_G));
    TEST_ASSERT_EQUAL_UINT16(2u, settings_get(SETTING_DISPLAY_BRIGHTNESS));
    TEST_ASSERT_EQUAL_UINT16(0u, settings_get(SETTING_DAILY_MAX_G));
    {
        char v[8];

        TEST_ASSERT_NOT_EQUAL(PORT_OK, config_port_get()->read("wifi", "pass", v, sizeof(v)));
    }
}

/* The Home Assistant entity of every setting agrees with the registry: same
 * state topic, same range, same options (one definition per contract, L19). */
static long settings_test_json_num(const char *cfg, const char *key)
{
    char needle[24];
    const char *p;

    (void)snprintf(needle, sizeof(needle), "\"%s\":", key);
    p = strstr(cfg, needle);
    return p != NULL ? strtol(p + strlen(needle), NULL, 10) : -1;
}

void test_settings_home_assistant_entities_match_the_registry(void)
{
    static char cfg[1024];
    char topic[64];
    char tmpl[64];
    unsigned i;
    unsigned e;

    for (i = 0u; i < SETTING_COUNT; i++) {
        const setting_def_t *d = settings_def((setting_id_t)i);
        mqtt_settings_topic_t t = MQTT_SETTINGS_TOPIC_COUNT;
        bool found = false;
        static const char *const suffix[MQTT_SETTINGS_TOPIC_COUNT] = {
            "feed/mode", "child_lock", "feed/overfill", "display", "power",
            "feed/limit", "feed/dispense_amount",
        };

        TEST_ASSERT_TRUE(mqtt_settings_topic_of((setting_id_t)i, &t));
        (void)snprintf(topic, sizeof(topic), "/%s\"", suffix[t]);
        (void)snprintf(tmpl, sizeof(tmpl), "value_json.%s ", d->field);
        for (e = 0u; e < mqtt_ha_discovery_entity_count(); e++) {
            const char *oid = NULL;

            if (mqtt_ha_discovery_format_entity(e, cfg, sizeof(cfg), "abc123", &oid) <= 0 ||
                strstr(cfg, "\"state_topic\"") == NULL || strstr(cfg, topic) == NULL) {
                continue;
            }
            /* Bare topics carry one setting; JSON topics name the field. */
            if (t != MQTT_SETTINGS_FEED_MODE && t != MQTT_SETTINGS_CHILD_LOCK &&
                strstr(cfg, tmpl) == NULL) {
                continue;
            }
            found = true;
            if (d->type == SETTING_UINT) {
                long lo = (d->flags & SETTING_F_ZERO_OFF) != 0u ? 0 : (long)d->min;

                TEST_ASSERT_EQUAL_INT32_MESSAGE(lo, settings_test_json_num(cfg, "min"), oid);
                TEST_ASSERT_EQUAL_INT32_MESSAGE((long)d->max, settings_test_json_num(cfg, "max"),
                                                oid);
            }
            if (d->type == SETTING_ENUM && strstr(cfg, "\"options\"") != NULL) {
                unsigned k;

                for (k = 0u; d->options[k] != NULL; k++) {
                    char opt[32];

                    (void)snprintf(opt, sizeof(opt), "\"%s\"", d->options[k]);
                    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(cfg, opt), oid);
                }
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, d->name);
    }
}
