# External interfaces to keep

The rewrite may change everything inside the firmware. These surfaces are
used by the owner's Home Assistant, his web bookmarks and a friend's feeder,
so they stay compatible (or change with a documented migration and a version
bump). Full details live in `spec/30-processes/`.

## MQTT (`petfeeder/<device_id>/…`) — `spec/30-processes/mqtt-protocol.md`

**State (retained unless noted):** `connection` (LWT online/offline), `state`,
`bowl_weight`, `eaten_today`, `hopper`, `hopper_ir`, `battery`,
`battery_voltage`, `mains`, `power`, `display`, `child_lock`, `feed/mode`,
`feed/overfill`, `feed/dispense_amount`, `feed/limit`, `portion`, `problem`,
`schedule/state`, `schedule/next`, `timezone`, `wifi_rssi` (QoS 0),
`dispense/progress` (QoS 0), `ota/status`, `ota/base`, `config`,
`dispense/event` (not retained, fire-and-forget).

**Commands (`cmd/…`):** `dispense`, `dispense/cancel`, `dispense_amount`,
`feed/mode`, `feed/overfill`, `feed_limit`, `portion_cal`, `calibrate`,
`child_lock`, `display`, `power`, `eaten_reset`, `problem_clear`, `reboot`,
`ota`, `ota_base`, `config`, `schedule/set|delete|toggle|enable|today|skip|feed_next`.

## Home Assistant discovery

Entity `object_id`s are part of the contract (renaming one orphans the
owner's dashboards/automations):

`binary_sensor`: mains, problem · `button`: dispense, feed_next,
problem_clear, portion_cal, calibrate_zero, calibrate_span, eaten_reset,
reboot, ota_install · `event`: dispense_completed · `number`:
dispense_amount, daily_limit, not_eaten_h, overfill_threshold_g,
display_brightness, display_battery_off, display_mains_off · `select`:
display_mode, battery_wifi · `sensor`: bowl_weight, eaten_today, battery,
battery_voltage, hopper_level, hopper_ir, portion_size, dispense_progress,
wifi_rssi, device_timezone, ota_state, firmware_bank · `switch`:
feeding_schedule, child_lock, overfill_protection, weight_compensation ·
`text`: ota_base.

Device block: identifiers, name, manufacturer, model, `sw_version`,
`configuration_url` = `http://<ip>`. Retired entities publish an empty
retained config (e.g. `dispense_custom`).

## Web API — `spec/30-processes/web-ui.md`

`/api/status` (composite), `/api/schedule/state|next`, `/api/config`,
`/api/feed/mode|overfill`, `/api/bowl_weight`, `/api/hopper`,
`/api/history`, `/api/log`, `/api/export`; writes: `dispense`,
`dispense/cancel`, `dispense_amount`, `schedule/*` (incl. `feed_next`),
`feed/*`, `feed_limit`, `portion_cal`, `calibrate`, `child_lock`, `display`,
`power`, `eaten_reset`, `problem_clear`, `reboot`, `ota_base`, `ota_install`,
`lang`, `network`, `password`, `unlock`, `reprovision`, `factory_reset`,
`import`.

The current auth mechanism (`key=<password>\n` body prefix, reads as POST)
is a workaround for an httpd that hides request headers. The rewrite **may**
replace it (see 04-target-architecture.md § Web) as long as the page and
the docs change with it; nothing outside the page uses it.

## NVDM keys — `spec/30-processes/config-store.md`

Groups: `wifi`, `mqtt`, `feed`, `display`, `eaten`, `schedule`, `time`,
`calib`, `power`, `system`, `hist`. A device updated over the air from the
current firmware **must keep its Wi-Fi, MQTT, calibration, schedule,
portion size and settings**. If the storage format changes, the rewrite ships
a one-time migration (see 05-engineering-checklist.md § Database migrations)
with a host test that loads a snapshot of the current keys.

## OTA

A/B application banks, firmware folder URL + `petfeeder_a.bin` /
`petfeeder_b.bin`, `cmd/ota {"install":true}`. Bootloader and partition
layout (`spec/40-architecture/partition-layout.md`) do not change: a
rewrite must be installable over the air from the current firmware.

## Physical controls

Dispense button = one portion; reset short press = setup AP; reset long press
(> 7 s) = factory reset; reset + dispense 3 s = child lock toggle; UART0
console (bench).
