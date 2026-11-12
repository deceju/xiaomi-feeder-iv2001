#ifndef TEST_MQTT_DRAIN_H
#define TEST_MQTT_DRAIN_H

/* Drain the MQTT outbox into the fake broker, advancing the fake clock past
 * the outbox pacing between items. Bounded: never loops forever when the
 * broker refuses (rewrite-kit/07 L28). Returns the number of items sent. */
unsigned test_mqtt_drain_all(void);
/* Same, calling after_each once per item sent. */
unsigned test_mqtt_drain_all_each(void (*after_each)(void));

#endif
