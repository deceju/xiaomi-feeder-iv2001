/*
 * Settings over MQTT — spec/30-processes/mqtt-protocol.md.
 */
#ifndef MQTT_SETTINGS_H
#define MQTT_SETTINGS_H

#include <stddef.h>

#include "port_err.h"
#include "settings.h"

typedef enum {
    MQTT_SETTINGS_FEED_MODE,
    MQTT_SETTINGS_CHILD_LOCK,
    MQTT_SETTINGS_OVERFILL,
    MQTT_SETTINGS_DISPLAY,
    MQTT_SETTINGS_POWER,
    MQTT_SETTINGS_FEED_LIMIT,
    MQTT_SETTINGS_DISPENSE_AMOUNT,
    MQTT_SETTINGS_TOPIC_COUNT
} mqtt_settings_topic_t;

void mqtt_settings_set_device_id(const char *device_id);
/* Retained state payload of a topic. Returns its length or -1. */
int mqtt_settings_format(mqtt_settings_topic_t t, char *buf, size_t len);
void mqtt_settings_publish_snapshot(mqtt_settings_topic_t t);
void mqtt_settings_connect_snapshot(void);
/* Publish the topics that carry id (deduplicated against the last payload). */
void mqtt_settings_on_change(setting_id_t id);
/* Validate the whole command, then apply it. */
port_err_t mqtt_settings_handle(mqtt_settings_topic_t t, const void *payload, size_t len);
/* The topic that carries id (every setting has one). */
bool mqtt_settings_topic_of(setting_id_t id, mqtt_settings_topic_t *out);
void mqtt_settings_test_reset(void);

#endif /* MQTT_SETTINGS_H */
