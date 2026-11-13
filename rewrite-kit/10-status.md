# Status (branch `rewrite`)

The owner asked for the work to be done here rather than by a separate agent,
with no device test until the end (rollback: the previous firmware).

That changed the approach. Each change is either verified by a host test,
the simulator, or the full `make ci` gate, or it is not made. Rewrites that
only the device could validate were deferred.

## Done

| Phase | What | Bugs found on the way |
|-------|------|-----------------------|
| P0 | `make ci`: printf ban, file lists vs sources, web bundle + budget, web tests, host tests `-Werror`, ASan/UBSan, ARM build A+B, flash/RAM budgets, version in both images | overlapping `strncpy` in OTA status (ASan); format attribute on `app_log` |
| P1 | Physical simulator (`firmware/test/sim/`) + S1–S6 | reintroducing L3, L5, L7, L8 makes them fail |
| P2 | Settings registry (`settings_table.h`), MQTT settings topics as a table, export/import from the registry, HA contract test | export missed daily limit + not-eaten; import wrote unvalidated values; feed/limit command applied half a bad command; button amount 0-means-unset made explicit |
| P6 (part) | Whole meals in the simulator (S7–S10, S12, S8) | daily limit overshot by compensation and by the 1-portion minimum; eating during the settle caused top-ups and wrong learning |
| P4 (part) | QoS 1 redelivered command dropped | — |
| P7 (part) | Crash note in the retained log | — |
| P8 (part) | Device text escaped in page templates | — |
| Scheduler | Window between polls (S17/S18) | 00:00 slot never fired; forward clock jumps skipped slots |
| Tests | Bounded outbox drain helper, empty outbox per test | test hangs (L28 again) |

## Deliberately not done (and why)

- **Storage format change / migration (P3).** NVDM text records already
  read back correctly through the registry. A format migration that fails
  on the device can lose Wi-Fi credentials and make the feeder unreachable,
  and it could not be tested here.
- **Task re-architecture (single `app` owner, event bus, net task owning
  MQTT).** The current tasks pass the simulator and the host suite; moving
  ownership without device timing is the riskiest kind of change.
- **httpd patch for cookies/headers (P8, Q7).** Needs the device to verify.
- **Page generated from the registry (P9).** Possible next step; low bug
  value now that the contract test pins the registry against HA.

These remain good next steps once the owner can run the acceptance script
(06) on the feeder.

## Owner test of this branch

Install bank images built from `rewrite` the usual way (HA `www/feeder`,
OTA). Watch the first meals. Rollback: the previous images in
`~/homelab/Croquettes/firmware-diag/versions/`.
