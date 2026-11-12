/*
 * Settings over MQTT — spec/30-processes/mqtt-protocol.md.
 *
 * Every settings topic is one row of s_topics: its retained state topic, the
 * settings it carries, and how its payload looks (a JSON object of fields, or
 * a bare value). One handler validates a whole command before applying any
 * field, so a command is all-or-nothing.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "eaten_today.h"
#include "feed_history.h"
#include "feed_limit.h"
#include "mqtt_child_lock.h"
#include "mqtt_dispense_amount.h"
#include "mqtt_display.h"
#include "mqtt_feed_mode.h"
#include "mqtt_feed_overfill.h"
#include "mqtt_json.h"
#include "mqtt_outbox.h"
#include "mqtt_power.h"
#include "mqtt_settings.h"
#include "mqtt_topics.h"
#include "settings.h"

#define MQTT_SETTINGS_MAX_FIELDS 4u
#define MQTT_SETTINGS_PAYLOAD    128u

typedef enum {
    MQTT_SETTINGS_JSON,     /* {"field":value,...} */
    MQTT_SETTINGS_BARE,     /* the value alone: an enum name, or ON/OFF */
} mqtt_settings_style_t;

typedef struct {
    const char *suffix;
    mqtt_settings_style_t style;
    uint8_t count;
    setting_id_t ids[MQTT_SETTINGS_MAX_FIELDS];
    int (*format)(char *buf, size_t len);   /* NULL = generic */
    const char *log_label;                  /* logged on change, NULL = per field */
} mqtt_settings_topic_def_t;

static int mqtt_settings_format_limit(char *buf, size_t len);
static int mqtt_settings_format_dispense_amount(char *buf, size_t len);

static const mqtt_settings_topic_def_t s_topics[MQTT_SETTINGS_TOPIC_COUNT] = {
    [MQTT_SETTINGS_FEED_MODE] = { "feed/mode", MQTT_SETTINGS_BARE, 1u,
                                  { SETTING_FEED_MODE }, NULL, NULL },
    [MQTT_SETTINGS_CHILD_LOCK] = { "child_lock", MQTT_SETTINGS_BARE, 1u,
                                   { SETTING_CHILD_LOCK }, NULL, NULL },
    [MQTT_SETTINGS_OVERFILL] = { "feed/overfill", MQTT_SETTINGS_JSON, 2u,
                                 { SETTING_OVERFILL_ENABLED, SETTING_OVERFILL_THRESHOLD_G },
                                 NULL, NULL },
    [MQTT_SETTINGS_DISPLAY] = { "display", MQTT_SETTINGS_JSON, 4u,
                                { SETTING_DISPLAY_MODE, SETTING_DISPLAY_BRIGHTNESS,
                                  SETTING_DISPLAY_BATTERY_OFF_S, SETTING_DISPLAY_MAINS_OFF_S },
                                NULL, "display settings changed" },
    [MQTT_SETTINGS_POWER] = { "power", MQTT_SETTINGS_JSON, 1u,
                              { SETTING_BATTERY_WIFI }, NULL, NULL },
    [MQTT_SETTINGS_FEED_LIMIT] = { "feed/limit", MQTT_SETTINGS_JSON, 2u,
                                   { SETTING_DAILY_MAX_G, SETTING_NOT_EATEN_H },
                                   mqtt_settings_format_limit, NULL },
    [MQTT_SETTINGS_DISPENSE_AMOUNT] = { "feed/dispense_amount", MQTT_SETTINGS_JSON, 1u,
                                        { SETTING_BUTTON_G },
                                        mqtt_settings_format_dispense_amount, NULL },
};

typedef struct {
    char topic[96];
    char last[MQTT_SETTINGS_PAYLOAD];
    bool last_valid;
} mqtt_settings_topic_state_t;

static mqtt_settings_topic_state_t s_state[MQTT_SETTINGS_TOPIC_COUNT];

static bool mqtt_settings_is_child_lock(setting_id_t id)
{
    return id == SETTING_CHILD_LOCK;
}

static int mqtt_settings_format_limit(char *buf, size_t len)
{
    int n = snprintf(buf, len, "{\"daily_max_g\":%u,\"today_g\":%lu,\"not_eaten_h\":%u}",
                     (unsigned)settings_get(SETTING_DAILY_MAX_G),
                     (unsigned long)feed_history_grams_on_day(eaten_today_day_key_now()),
                     (unsigned)settings_get(SETTING_NOT_EATEN_H));

    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

/* An unset button amount publishes 10 g (Home Assistant needs a number). */
static int mqtt_settings_format_dispense_amount(char *buf, size_t len)
{
    uint16_t g = settings_get(SETTING_BUTTON_G);
    int n = snprintf(buf, len, "{\"g\":%u}", (unsigned)(g != 0u ? g : 10u));

    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

int mqtt_settings_format(mqtt_settings_topic_t t, char *buf, size_t len)
{
    const mqtt_settings_topic_def_t *d;
    size_t off = 0u;
    uint8_t i;
    int n;

    if ((unsigned)t >= MQTT_SETTINGS_TOPIC_COUNT || buf == NULL || len == 0u) {
        return -1;
    }
    d = &s_topics[t];
    if (d->format != NULL) {
        return d->format(buf, len);
    }
    if (d->style == MQTT_SETTINGS_BARE) {
        setting_id_t id = d->ids[0];
        uint16_t v = settings_get(id);

        if (mqtt_settings_is_child_lock(id)) {
            n = snprintf(buf, len, "%s", v != 0u ? "ON" : "OFF");
        } else {
            n = snprintf(buf, len, "%s", settings_enum_name(id, v));
        }
        return (n < 0 || (size_t)n >= len) ? -1 : n;
    }

    buf[off++] = '{';
    for (i = 0u; i < d->count; i++) {
        n = snprintf(buf + off, len - off, "%s\"%s\":", i > 0u ? "," : "",
                     settings_def(d->ids[i])->field);
        if (n < 0 || (size_t)n >= len - off) {
            return -1;
        }
        off += (size_t)n;
        n = settings_format_json_value(d->ids[i], buf + off, len - off);
        if (n < 0) {
            return -1;
        }
        off += (size_t)n;
    }
    if (off + 2u > len) {
        return -1;
    }
    buf[off++] = '}';
    buf[off] = '\0';
    return (int)off;
}

void mqtt_settings_set_device_id(const char *device_id)
{
    unsigned t;

    for (t = 0u; t < MQTT_SETTINGS_TOPIC_COUNT; t++) {
        s_state[t].last_valid = false;
        if (device_id == NULL || device_id[0] == '\0' ||
            mqtt_topic_format(s_state[t].topic, sizeof(s_state[t].topic), device_id,
                              s_topics[t].suffix) != PORT_OK) {
            s_state[t].topic[0] = '\0';
        }
    }
}

static void mqtt_settings_publish(mqtt_settings_topic_t t, bool force)
{
    mqtt_settings_topic_state_t *st = &s_state[t];
    char payload[MQTT_SETTINGS_PAYLOAD];
    int n;

    if (st->topic[0] == '\0') {
        return;
    }
    n = mqtt_settings_format(t, payload, sizeof(payload));
    if (n < 0) {
        return;
    }
    if (!force && st->last_valid && strcmp(st->last, payload) == 0) {
        return;
    }
    if (!mqtt_outbox_enqueue(st->topic, payload, (size_t)n, 1, true)) {
        app_log_debug("mqtt", "settings enqueue failed topic=%s", st->topic);
        return;
    }
    (void)snprintf(st->last, sizeof(st->last), "%s", payload);
    st->last_valid = true;
}

void mqtt_settings_publish_snapshot(mqtt_settings_topic_t t)
{
    if ((unsigned)t < MQTT_SETTINGS_TOPIC_COUNT) {
        mqtt_settings_publish(t, false);
    }
}

void mqtt_settings_connect_snapshot(void)
{
    unsigned t;

    for (t = 0u; t < MQTT_SETTINGS_TOPIC_COUNT; t++) {
        mqtt_settings_publish((mqtt_settings_topic_t)t, true);
    }
}

void mqtt_settings_on_change(setting_id_t id)
{
    unsigned t;
    uint8_t i;

    for (t = 0u; t < MQTT_SETTINGS_TOPIC_COUNT; t++) {
        for (i = 0u; i < s_topics[t].count; i++) {
            if (s_topics[t].ids[i] == id) {
                mqtt_settings_publish((mqtt_settings_topic_t)t, false);
            }
        }
    }
}

/* Bare payload: trim whitespace, then an enum name or ON/OFF. */
static bool mqtt_settings_parse_bare(setting_id_t id, const char *payload, size_t len,
                                     uint16_t *out)
{
    char buf[24];
    size_t start = 0u;
    size_t end;

    if (len == 0u || len >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, payload, len);
    buf[len] = '\0';
    while (buf[start] != '\0' && isspace((unsigned char)buf[start])) {
        start++;
    }
    end = strlen(buf + start);
    while (end > 0u && isspace((unsigned char)buf[start + end - 1u])) {
        buf[start + --end] = '\0';
    }
    if (mqtt_settings_is_child_lock(id)) {
        if (strcmp(buf + start, "ON") == 0) {
            *out = 1u;
            return true;
        }
        if (strcmp(buf + start, "OFF") == 0) {
            *out = 0u;
            return true;
        }
        return false;
    }
    return settings_parse(id, buf + start, out);
}

/* One JSON member → validated value. Returns false when present but invalid. */
static bool mqtt_settings_parse_member(setting_id_t id, const char *json, size_t len,
                                       bool *present, uint16_t *out)
{
    const setting_def_t *d = settings_def(id);
    char text[24];
    unsigned u = 0u;
    bool b = false;

    *present = mqtt_json_has_key(json, len, d->field);
    if (!*present) {
        return true;
    }
    switch (d->type) {
    case SETTING_BOOL:
        if (!mqtt_json_find_bool(json, len, d->field, &b)) {
            return false;
        }
        *out = b ? 1u : 0u;
        return true;
    case SETTING_ENUM:
        return mqtt_json_find_string(json, len, d->field, text, sizeof(text)) &&
               settings_parse(id, text, out);
    case SETTING_UINT:
    default:
        if (!mqtt_json_find_uint(json, len, d->field, &u) || u > 0xFFFFu ||
            !settings_valid(id, (uint16_t)u)) {
            return false;
        }
        *out = (uint16_t)u;
        return true;
    }
}

port_err_t mqtt_settings_handle(mqtt_settings_topic_t t, const void *payload, size_t len)
{
    const mqtt_settings_topic_def_t *d;
    bool present[MQTT_SETTINGS_MAX_FIELDS] = { false };
    uint16_t value[MQTT_SETTINGS_MAX_FIELDS] = { 0u };
    bool any = false;
    bool changed = false;
    port_err_t err;
    uint8_t i;

    if ((unsigned)t >= MQTT_SETTINGS_TOPIC_COUNT || payload == NULL || len == 0u) {
        return PORT_ERR_INVALID_ARG;
    }
    d = &s_topics[t];

    if (d->style == MQTT_SETTINGS_BARE) {
        if (!mqtt_settings_parse_bare(d->ids[0], (const char *)payload, len, &value[0])) {
            return PORT_ERR_INVALID_ARG;
        }
        present[0] = true;
        any = true;
    } else {
        for (i = 0u; i < d->count; i++) {
            if (!mqtt_settings_parse_member(d->ids[i], (const char *)payload, len,
                                            &present[i], &value[i])) {
                return PORT_ERR_INVALID_ARG;
            }
            any = any || present[i];
        }
    }
    if (!any) {
        return PORT_ERR_INVALID_ARG;
    }

    for (i = 0u; i < d->count; i++) {
        if (!present[i] || settings_get(d->ids[i]) == value[i]) {
            continue;
        }
        err = settings_set(d->ids[i], value[i]);
        if (err != PORT_OK) {
            return err;
        }
        changed = true;
    }
    if (changed) {
        if (d->log_label != NULL) {
            app_log_info("app", "%s", d->log_label);
        }
        mqtt_settings_publish(t, false);
    }
    return PORT_OK;
}

bool mqtt_settings_topic_of(setting_id_t id, mqtt_settings_topic_t *out)
{
    unsigned t;
    uint8_t i;

    for (t = 0u; t < MQTT_SETTINGS_TOPIC_COUNT; t++) {
        for (i = 0u; i < s_topics[t].count; i++) {
            if (s_topics[t].ids[i] == id) {
                if (out != NULL) {
                    *out = (mqtt_settings_topic_t)t;
                }
                return true;
            }
        }
    }
    return false;
}

void mqtt_settings_test_reset(void)
{
    memset(s_state, 0, sizeof(s_state));
}

/* --- Topic entry points kept for the dispatcher and the CLI ------------------ */

void mqtt_feed_mode_set_device_id(const char *device_id)
{
    mqtt_settings_set_device_id(device_id);
}

void mqtt_feed_mode_publish_snapshot(void)
{
    mqtt_settings_publish_snapshot(MQTT_SETTINGS_FEED_MODE);
}

void mqtt_feed_mode_connect_snapshot(void)
{
    mqtt_settings_publish(MQTT_SETTINGS_FEED_MODE, true);
}

port_err_t mqtt_feed_mode_apply(dispense_mode_t mode)
{
    port_err_t err;

    if (settings_get(SETTING_FEED_MODE) == ((uint16_t)mode)) {
        return PORT_OK;
    }
    err = settings_set(SETTING_FEED_MODE, (uint16_t)mode);
    if (err == PORT_OK) {
        mqtt_settings_publish(MQTT_SETTINGS_FEED_MODE, false);
    }
    return err;
}

port_err_t mqtt_feed_mode_handle(const void *payload, size_t len)
{
    return mqtt_settings_handle(MQTT_SETTINGS_FEED_MODE, payload, len);
}

void mqtt_feed_mode_test_reset(void)
{
    s_state[MQTT_SETTINGS_FEED_MODE].topic[0] = '\0';
    s_state[MQTT_SETTINGS_FEED_MODE].last_valid = false;
}

void mqtt_child_lock_set_device_id(const char *device_id)
{
    mqtt_settings_set_device_id(device_id);
}

void mqtt_child_lock_publish_snapshot(void)
{
    mqtt_settings_publish_snapshot(MQTT_SETTINGS_CHILD_LOCK);
}

void mqtt_child_lock_connect_snapshot(void)
{
    mqtt_settings_publish(MQTT_SETTINGS_CHILD_LOCK, true);
}

port_err_t mqtt_child_lock_apply(bool locked)
{
    port_err_t err;

    if (settings_get(SETTING_CHILD_LOCK) == (locked ? 1u : 0u)) {
        return PORT_OK;
    }
    err = settings_set(SETTING_CHILD_LOCK, locked ? 1u : 0u);
    if (err == PORT_OK) {
        mqtt_settings_publish(MQTT_SETTINGS_CHILD_LOCK, false);
    }
    return err;
}

port_err_t mqtt_child_lock_handle(const void *payload, size_t len)
{
    return mqtt_settings_handle(MQTT_SETTINGS_CHILD_LOCK, payload, len);
}

void mqtt_child_lock_test_reset(void)
{
    s_state[MQTT_SETTINGS_CHILD_LOCK].topic[0] = '\0';
    s_state[MQTT_SETTINGS_CHILD_LOCK].last_valid = false;
}

bool mqtt_feed_overfill_format_snapshot(char *buf, size_t len)
{
    return mqtt_settings_format(MQTT_SETTINGS_OVERFILL, buf, len) > 0;
}

void mqtt_feed_overfill_set_device_id(const char *device_id)
{
    mqtt_settings_set_device_id(device_id);
}

void mqtt_feed_overfill_publish_snapshot(void)
{
    mqtt_settings_publish_snapshot(MQTT_SETTINGS_OVERFILL);
}

void mqtt_feed_overfill_connect_snapshot(void)
{
    mqtt_settings_publish(MQTT_SETTINGS_OVERFILL, true);
}

port_err_t mqtt_feed_overfill_handle(const void *payload, size_t len)
{
    return mqtt_settings_handle(MQTT_SETTINGS_OVERFILL, payload, len);
}

void mqtt_feed_overfill_test_reset(void)
{
    s_state[MQTT_SETTINGS_OVERFILL].topic[0] = '\0';
    s_state[MQTT_SETTINGS_OVERFILL].last_valid = false;
}

bool mqtt_display_format_snapshot(char *buf, size_t len)
{
    return mqtt_settings_format(MQTT_SETTINGS_DISPLAY, buf, len) > 0;
}

void mqtt_display_set_device_id(const char *device_id)
{
    mqtt_settings_set_device_id(device_id);
}

void mqtt_display_publish_snapshot(void)
{
    mqtt_settings_publish_snapshot(MQTT_SETTINGS_DISPLAY);
}

void mqtt_display_connect_snapshot(void)
{
    mqtt_settings_publish(MQTT_SETTINGS_DISPLAY, true);
}

port_err_t mqtt_display_handle(const void *payload, size_t len)
{
    return mqtt_settings_handle(MQTT_SETTINGS_DISPLAY, payload, len);
}

void mqtt_display_test_reset(void)
{
    s_state[MQTT_SETTINGS_DISPLAY].topic[0] = '\0';
    s_state[MQTT_SETTINGS_DISPLAY].last_valid = false;
}

battery_wifi_t mqtt_power_battery_wifi_get(void)
{
    return (battery_wifi_t)settings_get(SETTING_BATTERY_WIFI);
}

bool mqtt_power_format_snapshot(char *buf, size_t len)
{
    return mqtt_settings_format(MQTT_SETTINGS_POWER, buf, len) > 0;
}

void mqtt_power_set_device_id(const char *device_id)
{
    mqtt_settings_set_device_id(device_id);
}

void mqtt_power_connect_snapshot(void)
{
    mqtt_settings_publish(MQTT_SETTINGS_POWER, true);
}

port_err_t mqtt_power_handle(const void *payload, size_t len)
{
    return mqtt_settings_handle(MQTT_SETTINGS_POWER, payload, len);
}

void mqtt_power_test_reset(void)
{
    s_state[MQTT_SETTINGS_POWER].topic[0] = '\0';
    s_state[MQTT_SETTINGS_POWER].last_valid = false;
}

bool dispense_amount_get(uint16_t *grams)
{
    uint16_t g = settings_get(SETTING_BUTTON_G);

    if (grams == NULL || g == 0u) {
        return false;
    }
    *grams = g;
    return true;
}

void mqtt_dispense_amount_set_device_id(const char *device_id)
{
    mqtt_settings_set_device_id(device_id);
}

void mqtt_dispense_amount_connect_snapshot(void)
{
    mqtt_settings_publish(MQTT_SETTINGS_DISPENSE_AMOUNT, true);
}

port_err_t mqtt_dispense_amount_handle(const void *payload, size_t len)
{
    return mqtt_settings_handle(MQTT_SETTINGS_DISPENSE_AMOUNT, payload, len);
}

void mqtt_dispense_amount_test_reset(void)
{
    s_state[MQTT_SETTINGS_DISPENSE_AMOUNT].topic[0] = '\0';
    s_state[MQTT_SETTINGS_DISPENSE_AMOUNT].last_valid = false;
}

void mqtt_feed_limit_set_device_id(const char *device_id)
{
    mqtt_settings_set_device_id(device_id);
}

int mqtt_feed_limit_format(char *buf, size_t len)
{
    return mqtt_settings_format(MQTT_SETTINGS_FEED_LIMIT, buf, len);
}

void mqtt_feed_limit_connect_snapshot(void)
{
    mqtt_settings_publish(MQTT_SETTINGS_FEED_LIMIT, true);
}

void mqtt_feed_limit_sync(void)
{
    mqtt_settings_publish(MQTT_SETTINGS_FEED_LIMIT, false);
}

port_err_t mqtt_feed_limit_handle(const void *payload, size_t len)
{
    return mqtt_settings_handle(MQTT_SETTINGS_FEED_LIMIT, payload, len);
}
