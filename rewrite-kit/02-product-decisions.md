# Product decisions (owner-approved — do not reopen)

## Feeding

- **Portion size** is an average that follows reality: every weighed
  successful meal moves it by 1/8 of the error (outliers ignored, clamped
  3–20 g). A calibration run (3 portions, one weighing) sets it directly; a
  reset returns to 10 g. Status shown: default / calibrated / learned / failed.
- **Feed modes**: open loop (weigh for history only) and compensated (top up
  missing grams, at most 2 extra batches).
- **Dispense button (HA)** amount in grams; empty = one portion. The physical
  button always gives one portion.
- **Bowl missing** at the start of a meal: **dispense anyway**, raise E3.
- **Daily limit** (0 = off): applies to every source (schedule, HA, web,
  button) except the UART bench console. A meal that would exceed it is
  shortened to whole portions (≥ 1); once reached, meals are refused
  (schedule slot `skipped_limit`).
- **Overfill protection**: skips scheduled meals while the bowl holds at
  least the threshold.
- **No automatic retry** of a failed meal. **No catch-up** of a meal missed
  during a reboot or power cut.
- **Feed next meal** (HA button + web): serves today's next pending slot now
  and marks it so it is not served again. If none is left today: refuse
  ("no meal left today"), do not take tomorrow's. Refuse with the reason when
  the daily limit is reached or the bowl is full (no side effects).

## Alerts

- **Problem** state with codes (E1 failed meal / nothing fell, E2 hopper
  empty, E3 bowl missing at feed, E4 connection lost > 5 min, E5 time not
  synced, E6 daily limit, E7 not eaten, E9 task stalled). HA binary_sensor +
  *Clear problem* button; red banner on the web page; the panel alternates
  `Err`, `E n` and the normal weight (option "B").
- **Not-eaten alert**: configurable hours (0 = off), raised when the bowl has
  not gone down by 5 g that long after a meal.
- **No automatic reboot** on a stalled task: report (E9 + log) and let the
  owner reboot. (The hardware watchdog for a full hang may stay — confirm with
  the owner before removing it.)
- No HA automations are created by the firmware or by the agent. **Ask the
  owner before creating any new Home Assistant entity**, explaining why.

## Web page

- Tabs: Home, Schedule, Settings, System, Help. Every HA control and value is
  also on the page.
- **French and English**, chosen in System → Language and stored on the
  device (all browsers follow it); unset = browser language.
- A field the user edits is never overwritten by the periodic refresh until
  it is saved.
- Optional web password protects **reads and writes**; the telnet console
  asks for the same password. Passwords are never displayed, exported or
  logged. Lost password = factory reset (long press) or UART.
- Help tab explains how a meal works, the slot states and every problem code.
- Firmware update can be started from the page (same as HA).

## History and diagnostics

- Last 20 meals with the reason of any failure, kept across reboots and
  firmware updates (erased only by factory reset).
- Health counters, recent log that survives a software reboot, periodic heap
  line.
- Wi-Fi signal published at most every 10 min or on a 5 dB change.

## Process

- The owner tests in real life and reports; logs pasted back should be short.
- Firmware binaries are copied by the owner to Home Assistant `www/feeder`
  and installed with "Install firmware update".
