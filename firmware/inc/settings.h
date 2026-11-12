/*
 * Settings registry — spec/30-processes/config-store.md § Settings registry.
 *
 * One validation path for every writer (MQTT, web, UART, import). Values are
 * stored in NVDM as text (bool "1"/"0", enum by name, uint in decimal), so
 * records written by earlier firmware read back unchanged.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "port_err.h"
#include "settings_table.h"

typedef enum {
    SETTING_BOOL,
    SETTING_UINT,
    SETTING_ENUM,
} setting_type_t;

#define SETTING_F_EXPORT   0x01u
#define SETTING_F_ZERO_OFF 0x02u
#define SETTING_F_CLAMP    0x04u
#define SETTING_F_UNSET    0x08u

#define SETTING_OPTIONS(...) ((const char *const[]){ __VA_ARGS__, NULL })

#define SETTING_ENUM_ID(id, ...) SETTING_##id,
typedef enum {
    SETTINGS_TABLE(SETTING_ENUM_ID)
    SETTING_COUNT
} setting_id_t;
#undef SETTING_ENUM_ID

typedef struct {
    const char *name;        /* id as text, for logs */
    setting_type_t type;
    const char *group;       /* NVDM group */
    const char *key;         /* NVDM key */
    const char *field;       /* JSON member name in MQTT / web payloads */
    uint16_t def;
    uint16_t min;
    uint16_t max;
    uint8_t flags;
    const char *const *options; /* ENUM names, NULL-terminated */
} setting_def_t;

const setting_def_t *settings_def(setting_id_t id);

uint16_t settings_get(setting_id_t id);
bool settings_get_bool(setting_id_t id);

/* Validate and store. PORT_ERR_INVALID_ARG when out of range, PORT_ERR_IO when
 * storage fails. Storing the current value is a no-op (no flash write). */
port_err_t settings_set(setting_id_t id, uint16_t value);

/* Is value acceptable for id? */
bool settings_valid(setting_id_t id, uint16_t value);

/* Text form (enum name, "true"/"false", decimal) → value. */
bool settings_parse(setting_id_t id, const char *text, uint16_t *out);

/* Enum name for value ("" when out of range). */
const char *settings_enum_name(setting_id_t id, uint16_t value);

/* JSON value: true/false, a number, or a quoted enum name. Returns length or -1. */
int settings_format_json_value(setting_id_t id, char *buf, size_t len);

/* Called after a value changed (not on no-op writes). */
typedef void (*settings_listener_t)(setting_id_t id, uint16_t value);
void settings_set_listener(settings_listener_t fn);

/* Side effects of a change, wired statically (settings_hooks.c). */
void settings_on_changed(setting_id_t id, uint16_t value);

/* Find a setting by NVDM group + key (import whitelist). */
bool settings_find_key(const char *group, const char *key, setting_id_t *out);

#endif /* SETTINGS_H */
