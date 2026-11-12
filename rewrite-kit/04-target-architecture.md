# Target architecture

Keep: LinkIt SDK v4.7.1 (patches only), ports/adapters layering, FreeRTOS,
A/B OTA, Unity host tests, `spec/` tiers. Change: how the application layer
is organised.

## 1. Principles

- **One owner per piece of state.** Each mutable datum has exactly one task
  that writes it. Other tasks send messages or read snapshots.
- **Declarative over repeated.** Anything that today exists in five
  hand-written copies (NVDM key, MQTT topic, HA entity, web field, page
  label) is generated from one table.
- **Explicit state machines** for everything that unfolds in time (motor
  burst, meal, MQTT session, Wi-Fi, OTA, calibration). Transitions are a
  table; time and sensors are injected.
- **Events, not cross-calls.** A finished meal emits one `meal_result`;
  history, schedule, problems, portion learning, not-eaten, MQTT and the
  display subscribe.
- **Every blocking call has a timeout; every failure has a reason** that ends
  up in the log and, when user-visible, in the problem state or history.
- **No dynamic allocation after init** (static pools, fixed queues).

## 2. Tasks

| Task | Owns | Inputs | Notes |
|------|------|--------|-------|
| `app` | feeder state model, settings, schedule, meal job, problems, history | event queue | the only writer of application state |
| `motor` | motor + index + current sensing | command queue | hard real-time loop (≤ 50 ms polls), reports `burst_result` |
| `net` | Wi-Fi + MQTT session + outbox | queue from `app`, socket | single owner of the MQTT client |
| `httpd` (SDK) | none | HTTP | handlers only read snapshots or post commands to `app` and wait for the reply (with timeout) |
| `ota` | download/flash | command | already isolated |

The current `app_lock` mutex (web handlers running app logic directly) goes
away: the web layer sends `cmd` messages to `app` and waits on a reply slot.

## 3. Settings registry

One table drives storage, validation, MQTT, HA discovery, web API, export
and page labels:

```c
SETTING(feed_daily_max_g, U16, "feed", "daily_max_g", 0, 0, 2000, 10,
        .mqtt = "feed/limit", .field = "daily_max_g",
        .ha = HA_NUMBER("daily_limit", "Daily limit", "g", CONFIG),
        .label_en = "Daily limit", .label_fr = "Plafond par jour",
        .export = true, .secret = false)
```

Generated/derived:
- typed getters/setters with range checks (single validation path for MQTT,
  web, UART, import);
- NVDM load/store with versioned schema (see § 6);
- MQTT state payloads (grouped per topic) and command parsing;
- HA discovery configs (object_id stable);
- web `/api/settings` JSON + a settings descriptor the page renders;
- export/import whitelist (`.export`), never secrets (`.secret`);
- FR/EN labels (the page translates by key, not by English text).

A Python or C-preprocessor generator is fine; the generated files are
committed and a test checks they are up to date.

## 4. State model and publishing

`feeder_state_t` (bowl, hopper, battery, mains, meal in progress, problems,
time sync, rssi, ota…) lives in `app`. Each field has a dirty bit. The `net`
task receives "state changed" notifications and publishes **only the dirty
groups**, retained, with coalescing (latest value wins) instead of one
outbox entry per change. On connect it publishes a full snapshot. No module
formats MQTT payloads by itself.

## 5. State machines

**Motor burst** (motor task): `IDLE → STARTING (LED settle, baseline) →
RUNNING → (hole confirmed after ≥ 1.5 s closed) → … → STOPPING → DONE`,
side branches `STALL → REVERSE → RESUME` (≤ 3), `FAULT(reason)`. Inputs:
clock, beam sample (ok/busy/err), current sample. Output: `burst_result`
{pulses, reason, antijam, polls ok/busy/err, peak mA, duration}.

**Meal job** (app task): `CHECKS (limit, overfill for schedule, busy) →
BASELINE → BATCH (burst) → SETTLE → [TOP-UP ≤ 2] → RESULT`. Produces one
`meal_result` with everything the history needs. Cancel and timeout are
transitions, not flags.

**MQTT session** (net task): `DOWN → CONNECTING → SUBSCRIBING → SNAPSHOT →
UP → (publish failures ≥ 3 | ping timeout) → DOWN`, exponential backoff with
jitter, capped. Active liveness: PINGREQ on idle and a deadline on PINGRESP;
a failed write closes the socket immediately.

**Wi-Fi**: trial credentials with fallback, reconnect with backoff, captive
portal as its own state.

## 6. Storage

- Small typed records, each with `{magic, version, len, crc}`; one NVDM key
  per record (settings group, calibration, schedule, history ring header +
  fixed-size binary entries, counters).
- Writes are coalesced (≥ 2 s debounce, immediate for calibration/secrets)
  to limit flash wear; counters flushed on change but rate-limited.
- **Migration**: on first boot after the rewrite, read the legacy keys (list
  in `spec/30-processes/config-store.md`), convert, write the new records,
  keep the legacy keys until the new ones verify, then erase them.
- A RAM log ring in a `.noinit` section survives software reboots (exists
  today; keep), plus a crash record (HardFault PC/LR/task) in the same
  section.

## 7. Network

**MQTT**: one client owned by `net`; QoS 0 for telemetry, QoS 1 for state
and events; command dedup (see 05 § Idempotency); outbox is a coalescing
table keyed by topic, not a FIFO of payload copies.

**Web**: patch the SDK httpd (or wrap it) so handlers see the method,
headers and query string. Then:
- `POST /api/login {password}` → session token (random 128-bit, in RAM,
  expiry, rotated on reboot) returned in a cookie `HttpOnly; SameSite=Strict`;
- every other `/api/*` call requires the cookie; CSRF covered by SameSite +
  a custom header check;
- real HTTP status codes (200/400/401/404/409/503);
- `GET` for reads, `POST/PUT` for writes, versioned under `/api/v1/`.
If patching httpd proves impossible, keep the current body-key scheme but
isolate it in one module.

**Page**: generated from the settings descriptor where possible; strings by
key (`t('settings.daily_limit')`) with FR/EN tables; all dynamic text
inserted with `textContent` (never `innerHTML` with device strings such as
SSIDs); dirty-field tracking built into the form helper.

## 8. Display

A single presentation function computes the frame from the state model
(scene, problem overlay, dispense indicator, OTA, blank) every tick. No
module writes the panel directly.

## 9. Diagnostics

Structured log lines: `[t] [tag] event key=value …` with a per-meal
correlation id (`meal=17`) used in history, MQTT event and logs. Levels:
debug traces go to UART/telnet only, info+ to the RAM ring. Health counters,
task heartbeats with stack high-water marks and heap min, crash record.

## 10. Directory layout (proposal)

```
firmware/
  core/        state model, events, registry (generated), errors
  feed/        meal job, portion learning, limits, schedule
  motor/       burst state machine, index, jam
  sense/       scale, hopper, battery
  net/         wifi, mqtt session, outbox, ha discovery
  web/         api handlers, auth, page (generated assets)
  platform/    ports + adapters (unchanged contract)
  diag/        log, history, counters, crash, watch
  sim/         physical simulator (host only)
tools/registry/ generator + settings table
```
