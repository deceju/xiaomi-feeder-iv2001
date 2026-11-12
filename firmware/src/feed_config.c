/*
 * Feed settings — spec/30-processes/config-store.md (feed group).
 * Values live in the settings registry.
 */

#include "feed_config.h"

#include "settings.h"

bool feed_config_child_lock_is_active(void)
{
    return settings_get_bool(SETTING_CHILD_LOCK);
}

bool feed_config_child_lock_set(bool locked)
{
    return settings_set(SETTING_CHILD_LOCK, locked ? 1u : 0u) == PORT_OK;
}

/* Returns the state now in effect. */
bool feed_config_child_lock_toggle(void)
{
    bool locked = !feed_config_child_lock_is_active();

    return feed_config_child_lock_set(locked) ? locked : !locked;
}

const char *feed_config_mode_string(dispense_mode_t mode)
{
    const char *name = settings_enum_name(SETTING_FEED_MODE, (uint16_t)mode);

    return name[0] != '\0' ? name : "open_loop";
}

dispense_mode_t feed_config_mode_get(void)
{
    return (dispense_mode_t)settings_get(SETTING_FEED_MODE);
}

bool feed_config_mode_set(dispense_mode_t mode)
{
    return settings_set(SETTING_FEED_MODE, (uint16_t)mode) == PORT_OK;
}

bool feed_config_overfill_enabled_get(void)
{
    return settings_get_bool(SETTING_OVERFILL_ENABLED);
}

bool feed_config_overfill_enabled_set(bool enabled)
{
    return settings_set(SETTING_OVERFILL_ENABLED, enabled ? 1u : 0u) == PORT_OK;
}

uint16_t feed_config_overfill_threshold_g_get(void)
{
    return settings_get(SETTING_OVERFILL_THRESHOLD_G);
}

bool feed_config_overfill_threshold_g_set(uint16_t threshold_g)
{
    return settings_set(SETTING_OVERFILL_THRESHOLD_G, threshold_g) == PORT_OK;
}
