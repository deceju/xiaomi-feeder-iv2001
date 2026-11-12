# Test strategy

## Levels

| Level | Runs where | Covers | Gate |
|-------|------------|--------|------|
| **Unit** | host (Unity, gcc + ASan/UBSan) | pure modules: registry validation, parsers, state machines with injected clock/sensors, portion EMA, limits, DST, JSON escaping | every commit |
| **Simulator** | host | the whole application (app + motor + meal + scheduler + MQTT session + web handlers) against a **physical simulator** and fake network | every commit |
| **Integration (adapters)** | target build + host link where possible | adapters compile and link; port contracts | every commit (ARM build) |
| **Fuzz** | host (libFuzzer) | JSON parser, HTTP request parser, MQTT command handlers, settings import | CI smoke (60 s), longer weekly |
| **End-to-end on device** | owner's feeder or a bench unit | the acceptance script below | before every release |
| **Soak** | device | 24 h endurance | before a release that touches motor, net or storage |
| **Load** | device | web + MQTT bursts (05 § Load Testing) | before a release that touches net/web |

## Physical simulator (host)

A deterministic model driven by a virtual clock:

- **Auger**: angular position; speed from supply (mains: hole every 6.4 s;
  battery: parameter); index holes at 0° and 180° with a 0.2 s open window;
  **spring-back**: on stop, back off a configurable angle so the next start
  re-enters the hole within 160 ms; start-up jolt (beam flicker for a few
  ms).
- **Current**: 70–150 mA turning (noise), 280 mA when a jam is injected
  (beam frozen), reverse current.
- **Kibble**: grams per hole (default 8.5 g, configurable, noise ±10 %);
  hopper level that empties; jam probability or scripted jams.
- **Scale**: bowl present/missing, 1 g quantisation, settle time, creep,
  a "pet" that eats N g at scripted times (including during the settle).
- **Expander/bus**: beam reads can return `busy` or `err` at scripted rates
  (to reproduce Wi-Fi contention).
- **Battery ADC**: normal pack voltage with scripted glitches (24.7 V).
- **Network**: MQTT broker fake that can accept, delay, drop silently (dead
  TCP with the client still "connected"), or refuse; Wi-Fi up/down.
- **Clock**: wall clock with TZ/DST, NTP available or not.

Field logs in `field-logs/` become scripted scenarios with expected results.

## Required simulator scenarios (minimum)

| # | Scenario | Expected |
|---|----------|----------|
| S1 | 1 portion from parked (beam open) with spring-back | exactly 1 hole counted, ~8.5 g |
| S2 | 1 portion with start-up flicker | flicker ignored |
| S3 | 5 portions at 6.4 s/hole | completes, ~32 s, no session timeout |
| S4 | 13 portions, jam injected at +25 s (field log) | anti-jam retries under the burst limit, resumes, pulses kept, meal completes or ends `stuck` with reason `antijam` |
| S5 | jam that never clears | 3 tries, `stuck`, history reason, E1 |
| S6 | beam reads `busy` 30 % during a meal | meal still correct; busy count recorded |
| S7 | compensated 30 g with 8.5 g/hole | top-ups ≤ 2, delivered within tolerance, portion estimate moves toward 8.5 g |
| S8 | pet eats during settle | portion learning ignores the sample |
| S9 | bowl missing at start | meal runs, E3, history `bowl=0`, no learning |
| S10 | nothing falls (empty hopper, IR still blocked) | E1 "nothing fell", not "empty hopper" |
| S11 | hopper truly empty | E2, `empty_hopper` |
| S12 | daily limit 40 g: 30 g then 30 g then 30 g | 30, shortened to 10, refused (`limit`), E6, cleared at midnight |
| S13 | feed next meal (pending, none left, limit, bowl full, busy) | served once / refused with the right reason, no side effects |
| S14 | MQTT dead session (writes fail, client "connected") | reconnect after 3 failures, `mqtt_lost` +1, E4 only if > 5 min |
| S15 | MQTT QoS 1 duplicate `cmd/dispense` with the same id | one meal |
| S16 | reboot during a meal | motor stopped, history records `aborted`, no catch-up |
| S17 | DST spring forward with a 02:30 slot | fires once at the next valid minute |
| S18 | DST fall back with a 02:30 slot | fires once |
| S19 | battery ADC glitch 24.7 V | ignored, logged |
| S20 | settings import with unknown/secret/out-of-range fields | rejected fields listed, nothing secret written |
| S21 | web: no session / wrong password ×10 / XSS SSID | 401, lockout delay, page shows the SSID as text |
| S22 | legacy NVDM snapshot (current firmware) | migrated: Wi-Fi, MQTT, calibration, schedule, portion, settings intact |
| S23 | 7 simulated days, 3 meals/day, random eating | no leak (heap/stack), history consistent, eaten-today resets at midnight |

## On-device acceptance (owner runs it)

A script `tools/acceptance/run.sh <feeder-ip> [password]` that drives the web
API and MQTT and prints a pass/fail table plus the history and log tail to
paste back. Steps:

1. Read status, version, bank, health; record counters.
2. Dispense 10 g ×3 (one at a time); check history: 1 hole each, delivered
   weight, no E1.
3. Dispense 50 g; check top-ups and delivered.
4. Portion calibration; check status and value.
5. Feed next meal (with a test slot 2 min ahead).
6. Set and clear daily limit; check refusal.
7. Problem clear; check MQTT `problem` topic.
8. Broker restart (owner restarts the MQTT add-on when prompted); check
   reconnect within 60 s and `mqtt_lost` +1.
9. Reboot from the API; check previous-boot log, history intact.
10. OTA to the other bank and back (optional, owner confirms).

The owner gets the exact command and instructions to run it, and pastes the
summary block back.

## Definition of done (per PR)

- Spec updated first (tiers as in `AGENTS.md`).
- New behaviour covered by unit/simulator tests that failed before the code.
- All regressions in 07-lessons-and-regressions.md still covered.
- Host tests (with sanitizers), web tests, ARM build both banks, size
  budgets, generator check: green in CI.
- Reviewed by a second model; review comments resolved or answered.
- If it touches motor/net/storage: acceptance script run on the device and
  its summary attached to the PR.
