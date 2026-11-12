/*
 * Display settings — spec/30-processes/config-store.md (display group).
 * Values live in the settings registry.
 */

#include "display_settings.h"

#include <string.h>

#include "config_keys.h"
#include "config_port.h"
#include "settings.h"

const char *display_settings_mode_name(display_mode_t mode)
{
    const char *name = settings_enum_name(SETTING_DISPLAY_MODE, (uint16_t)mode);

    return name[0] != '\0' ? name : "weight";
}

bool display_settings_mode_parse(const char *name, display_mode_t *out)
{
    uint16_t v;

    if (out == NULL || !settings_parse(SETTING_DISPLAY_MODE, name, &v)) {
        return false;
    }
    *out = (display_mode_t)v;
    return true;
}

/* Earlier firmware stored "off" as a display mode: it means brightness 0. */
void display_settings_migrate(void)
{
    const config_port_t *cfg = config_port_get();
    char mode[16];

    if (cfg != NULL &&
        cfg->read(CONFIG_GROUP_DISPLAY, CONFIG_KEY_DISPLAY_MODE, mode, sizeof(mode)) == PORT_OK &&
        strcmp(mode, "off") == 0) {
        (void)cfg->write(CONFIG_GROUP_DISPLAY, CONFIG_KEY_DISPLAY_BRIGHTNESS, "0");
        (void)cfg->write(CONFIG_GROUP_DISPLAY, CONFIG_KEY_DISPLAY_MODE, "weight");
    }
}

display_mode_t display_settings_mode_get(void)
{
    return (display_mode_t)settings_get(SETTING_DISPLAY_MODE);
}

bool display_settings_mode_set(display_mode_t mode)
{
    return settings_set(SETTING_DISPLAY_MODE, (uint16_t)mode) == PORT_OK;
}

uint8_t display_settings_brightness_get(void)
{
    return (uint8_t)settings_get(SETTING_DISPLAY_BRIGHTNESS);
}

bool display_settings_brightness_set(uint8_t level)
{
    return settings_set(SETTING_DISPLAY_BRIGHTNESS, level) == PORT_OK;
}

uint16_t display_settings_battery_off_s_get(void)
{
    return settings_get(SETTING_DISPLAY_BATTERY_OFF_S);
}

bool display_settings_battery_off_s_set(uint16_t seconds)
{
    return settings_set(SETTING_DISPLAY_BATTERY_OFF_S, seconds) == PORT_OK;
}

uint16_t display_settings_mains_off_s_get(void)
{
    return settings_get(SETTING_DISPLAY_MAINS_OFF_S);
}

bool display_settings_mains_off_s_set(uint16_t seconds)
{
    return settings_set(SETTING_DISPLAY_MAINS_OFF_S, seconds) == PORT_OK;
}
