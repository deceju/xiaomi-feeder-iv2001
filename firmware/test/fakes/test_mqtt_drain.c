#include "test_mqtt_drain.h"

#include "fake_mqtt_port.h"
#include "fake_time.h"
#include "mqtt_outbox.h"

unsigned test_mqtt_drain_all(void)
{
    return test_mqtt_drain_all_each(NULL);
}

unsigned test_mqtt_drain_all_each(void (*after_each)(void))
{
    unsigned sent = 0u;
    unsigned attempts = 0u;

    while (mqtt_outbox_pending() > 0 && attempts < 512u) {
        attempts++;
        if (mqtt_outbox_drain_one(fake_mqtt_port_get())) {
            sent++;
            if (after_each != NULL) {
                after_each();
            }
        } else {
            fake_time_advance_ms(101u);
        }
    }
    return sent;
}
