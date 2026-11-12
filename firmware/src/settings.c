/*
 * Settings registry — spec/30-processes/config-store.md § Settings registry.
 */
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_log.h"
#include "config_port.h"

#define SETTING_DEF_ROW(id, type, group, key, field, def, min, max, flags, options) \
    { #id, type, group, key, field, def, min, max, flags, options },

static const setting_def_t s_defs[SETTING_COUNT] = {
    SETTINGS_TABLE(SETTING_DEF_ROW)
};

#undef SETTING_DEF_ROW

static settings_listener_t s_listener;

const setting_def_t *settings_def(setting_id_t id)
{
    return ((unsigned)id < SETTING_COUNT) ? &s_defs[id] : NULL;
}

static uint16_t settings_enum_count(const setting_def_t *d)
{
    uint16_t n = 0u;

    while (d->options != NULL && d->options[n] != NULL) {
        n++;
    }
    return n;
}

static uint16_t settings_max(const setting_def_t *d)
{
    switch (d->type) {
    case SETTING_BOOL:
        return 1u;
    case SETTING_ENUM:
        return (uint16_t)(settings_enum_count(d) - 1u);
    case SETTING_UINT:
    default:
        return d->max;
    }
}

static uint16_t settings_min(const setting_def_t *d)
{
    return (d->type == SETTING_UINT) ? d->min : 0u;
}

bool settings_valid(setting_id_t id, uint16_t value)
{
    const setting_def_t *d = settings_def(id);

    if (d == NULL) {
        return false;
    }
    if (value == 0u && (d->flags & SETTING_F_ZERO_OFF) != 0u) {
        return true;
    }
    return value >= settings_min(d) && value <= settings_max(d);
}

const char *settings_enum_name(setting_id_t id, uint16_t value)
{
    const setting_def_t *d = settings_def(id);

    if (d == NULL || d->type != SETTING_ENUM || value >= settings_enum_count(d)) {
        return "";
    }
    return d->options[value];
}

static bool settings_parse_uint(const char *text, uint16_t *out)
{
    char *end = NULL;
    unsigned long v;

    if (text[0] < '0' || text[0] > '9') {
        return false;
    }
    v = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || v > 0xFFFFul) {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

/* Text → raw value (no range check). */
static bool settings_parse_raw(const setting_def_t *d, const char *text, uint16_t *out)
{
    uint16_t i;

    switch (d->type) {
    case SETTING_BOOL:
        if (strcmp(text, "1") == 0 || strcmp(text, "true") == 0) {
            *out = 1u;
            return true;
        }
        if (strcmp(text, "0") == 0 || strcmp(text, "false") == 0) {
            *out = 0u;
            return true;
        }
        return false;
    case SETTING_ENUM:
        for (i = 0u; d->options != NULL && d->options[i] != NULL; i++) {
            if (strcmp(text, d->options[i]) == 0) {
                *out = i;
                return true;
            }
        }
        return false;
    case SETTING_UINT:
    default:
        return settings_parse_uint(text, out);
    }
}

bool settings_parse(setting_id_t id, const char *text, uint16_t *out)
{
    const setting_def_t *d = settings_def(id);
    uint16_t v;

    if (d == NULL || text == NULL || out == NULL || !settings_parse_raw(d, text, &v) ||
        !settings_valid(id, v)) {
        return false;
    }
    *out = v;
    return true;
}

uint16_t settings_get(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    const config_port_t *cfg = config_port_get();
    char buf[24];
    uint16_t v;

    if (d == NULL) {
        return 0u;
    }
    if (cfg == NULL || cfg->read(d->group, d->key, buf, sizeof(buf)) != PORT_OK ||
        !settings_parse_raw(d, buf, &v)) {
        return d->def;
    }
    if (settings_valid(id, v) || (v == 0u && (d->flags & SETTING_F_UNSET) != 0u)) {
        return v;
    }
    if ((d->flags & SETTING_F_CLAMP) != 0u) {
        return (v < settings_min(d)) ? settings_min(d) : settings_max(d);
    }
    return d->def;
}

bool settings_get_bool(setting_id_t id)
{
    return settings_get(id) != 0u;
}

static void settings_format_stored(const setting_def_t *d, uint16_t value, char *buf, size_t len)
{
    switch (d->type) {
    case SETTING_BOOL:
        (void)snprintf(buf, len, "%s", value != 0u ? "1" : "0");
        break;
    case SETTING_ENUM:
        (void)snprintf(buf, len, "%s", d->options[value]);
        break;
    case SETTING_UINT:
    default:
        (void)snprintf(buf, len, "%u", (unsigned)value);
        break;
    }
}

port_err_t settings_set(setting_id_t id, uint16_t value)
{
    const setting_def_t *d = settings_def(id);
    const config_port_t *cfg = config_port_get();
    char buf[24];

    if (d == NULL || !settings_valid(id, value)) {
        return PORT_ERR_INVALID_ARG;
    }
    if (settings_get(id) == value) {
        return PORT_OK;
    }
    if (cfg == NULL) {
        return PORT_ERR_IO;
    }
    settings_format_stored(d, value, buf, sizeof(buf));
    if (cfg->write(d->group, d->key, buf) != PORT_OK) {
        app_log_error("settings", "%s: store failed", d->key);
        return PORT_ERR_IO;
    }
    settings_on_changed(id, value);
    if (s_listener != NULL) {
        s_listener(id, value);
    }
    return PORT_OK;
}

int settings_format_json_value(setting_id_t id, char *buf, size_t len)
{
    const setting_def_t *d = settings_def(id);
    uint16_t v;
    int n;

    if (d == NULL || buf == NULL || len == 0u) {
        return -1;
    }
    v = settings_get(id);
    switch (d->type) {
    case SETTING_BOOL:
        n = snprintf(buf, len, "%s", v != 0u ? "true" : "false");
        break;
    case SETTING_ENUM:
        n = snprintf(buf, len, "\"%s\"", settings_enum_name(id, v));
        break;
    case SETTING_UINT:
    default:
        n = snprintf(buf, len, "%u", (unsigned)v);
        break;
    }
    return (n > 0 && (size_t)n < len) ? n : -1;
}

void settings_set_listener(settings_listener_t fn)
{
    s_listener = fn;
}

bool settings_find_key(const char *group, const char *key, setting_id_t *out)
{
    unsigned i;

    if (group == NULL || key == NULL) {
        return false;
    }
    for (i = 0u; i < SETTING_COUNT; i++) {
        if (strcmp(s_defs[i].group, group) == 0 && strcmp(s_defs[i].key, key) == 0) {
            if (out != NULL) {
                *out = (setting_id_t)i;
            }
            return true;
        }
    }
    return false;
}
