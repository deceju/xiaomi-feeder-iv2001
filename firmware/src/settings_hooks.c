/*
 * Side effects of a settings change — spec/30-processes/config-store.md
 * § Settings registry. Wired statically so every writer (MQTT, web, UART,
 * import) gets them.
 */
#include "app_log.h"
#include "feed_limit.h"
#include "settings.h"

void settings_on_changed(setting_id_t id, uint16_t value)
{
    switch (id) {
    case SETTING_CHILD_LOCK:
        app_log_info("app", "child_lock %s", value != 0u ? "on" : "off");
        break;
    case SETTING_BATTERY_WIFI:
        app_log_info("app", "battery wifi: %s", settings_enum_name(id, value));
        break;
    case SETTING_NOT_EATEN_H:
        if (value == 0u) {
            feed_not_eaten_disarm();
        }
        break;
    default:
        break;
    }
}
