/*
 * The user settings — one row each. Storage, validation, MQTT state/commands,
 * the web API and the export whitelist all derive from this table
 * (spec/30-processes/config-store.md § Settings registry).
 *
 * X(id, type, nvdm_group, nvdm_key, json_field, default, min, max, flags, options)
 *
 *   type     SETTING_BOOL | SETTING_UINT | SETTING_ENUM
 *   min/max  UINT range; ENUM: index range is derived from options
 *   flags    SETTING_F_EXPORT      included in settings export/import
 *            SETTING_F_ZERO_OFF    0 is accepted outside [min, max] (feature off)
 *            SETTING_F_UNSET       0 (the default) means "not configured"; it is
 *                                  never accepted from a writer
 *            SETTING_F_CLAMP       an out-of-range stored value is clamped
 *                                  (otherwise it reads as the default)
 *   options  ENUM names (SETTING_OPTIONS), stored and published by name; the
 *            order matches the C enum the getter callers use
 */
#ifndef SETTINGS_TABLE_H
#define SETTINGS_TABLE_H

#define SETTINGS_TABLE(X)                                                                  \
    X(FEED_MODE, SETTING_ENUM, "feed", "mode", "mode",                                     \
      0, 0, 0, SETTING_F_EXPORT, SETTING_OPTIONS("open_loop", "compensated"))                                  \
    X(CHILD_LOCK, SETTING_BOOL, "feed", "child_lock", "child_lock",                        \
      0, 0, 1, SETTING_F_EXPORT, NULL)                                                     \
    X(OVERFILL_ENABLED, SETTING_BOOL, "feed", "overfill_enabled", "enabled",               \
      0, 0, 1, SETTING_F_EXPORT, NULL)                                                     \
    X(OVERFILL_THRESHOLD_G, SETTING_UINT, "feed", "overfill_threshold_g", "threshold_g",   \
      50, 30, 500, SETTING_F_EXPORT | SETTING_F_CLAMP, NULL)                               \
    X(DAILY_MAX_G, SETTING_UINT, "feed", "daily_max_g", "daily_max_g",                     \
      0, 10, 2000, SETTING_F_EXPORT | SETTING_F_ZERO_OFF, NULL)                            \
    X(NOT_EATEN_H, SETTING_UINT, "feed", "not_eaten_h", "not_eaten_h",                     \
      0, 0, 24, SETTING_F_EXPORT, NULL)                                                    \
    X(BUTTON_G, SETTING_UINT, "feed", "button_g", "g",                                     \
      0, 5, 150, SETTING_F_EXPORT | SETTING_F_UNSET, NULL)                              \
    X(DISPLAY_MODE, SETTING_ENUM, "display", "mode", "mode",                               \
      0, 0, 0, SETTING_F_EXPORT, SETTING_OPTIONS("weight", "eaten_today"))                                     \
    X(DISPLAY_BRIGHTNESS, SETTING_UINT, "display", "brightness", "brightness",             \
      4, 0, 4, SETTING_F_EXPORT, NULL)                                                     \
    X(DISPLAY_BATTERY_OFF_S, SETTING_UINT, "display", "battery_off_s", "battery_off_s",    \
      60, 0, 600, SETTING_F_EXPORT, NULL)                                                  \
    X(DISPLAY_MAINS_OFF_S, SETTING_UINT, "display", "mains_off_s", "mains_off_s",          \
      0, 0, 3600, SETTING_F_EXPORT, NULL)                                                  \
    X(BATTERY_WIFI, SETTING_ENUM, "power", "battery_wifi", "battery_wifi",                 \
      0, 0, 0, SETTING_F_EXPORT, SETTING_OPTIONS("on", "off", "scheduled_only"))

#endif /* SETTINGS_TABLE_H */
