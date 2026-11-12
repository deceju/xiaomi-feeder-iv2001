#ifndef FAKE_CONFIG_PORT_H
#define FAKE_CONFIG_PORT_H

#include "config_port.h"

void fake_config_port_reset(void);
const config_port_t *fake_config_port_get(void);
/* String writes since start (flash wear checks). */
unsigned fake_config_port_write_count(void);

#endif /* FAKE_CONFIG_PORT_H */
