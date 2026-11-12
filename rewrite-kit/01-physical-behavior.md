# Physical behaviour (measured)

Confidence: **field** = seen on the owner's feeder logs; **probe** = measured on
the PCB (see `spec/10-hardware/`); **inferred** = consistent with logs but not
isolated. Reference unit: IV2001, MT7682 (MHCW05P-B), mains + 4×AA, dog
kibble.

## Auger and index sensor

| Fact | Value | Confidence | Consequence for the code |
|------|-------|------------|--------------------------|
| Index disk | 2 holes at 180° on the auger shaft | probe | One hole = one portion |
| Hole period at mains speed | ~6.4 s between holes | field | A 5-portion meal takes ~32 s; session limits must scale with the target (≥ pulses × per-gap timeout + margin) |
| Beam open time per hole | ~0.2 s | field | Poll period must be well under 100 ms; never miss a 200 ms window |
| Parked position | Usually stops with a hole in the beam (beam open) | field | The first edge after start is not a portion |
| **Spring-back** | When the motor stops, the loaded auger springs back out of the hole; on the next start it re-enters the same hole within ~160 ms | field | A hole counts only after ≥ 1.5 s of closed beam (timed from motor start when the start baseline is closed). Counting that re-entry made 10 g dispenses deliver nothing |
| Start-up jolt | The beam flickers at start | field | Ignore edges before a minimum closed time |
| Index LED settle | Needs a few ms after power-on before the detector reads true | field | Settle 5 ms, retry the baseline read, assume "parked open" if unreadable |
| Mass per hole | ~8.5–9 g with the owner's kibble (12 g seen once on a weighed run) | field | Portion size is a learned average, not a constant |

## Motor current (jam detection)

| Fact | Value | Confidence |
|------|-------|------------|
| Normal rotation | 70–150 mA | field |
| Stalled (kibble wedged), beam frozen | ~280 mA sustained | field |
| Shunt | 1 Ω, pin mV = mA | probe |
| ADC path | NC7SB3157 mux → GPIO17, P1.7 low = motor, high = battery | probe |

Stall rule that works: 220–500 mA for 2 s **without an index transition** →
anti-jam (reverse ~1 s, then resume, keep the pulses already counted, 3
tries). Field log `field-logs/2026-11-10_13-portion-stall_session-timeout.log`
shows a stall 25 s into a 13-portion meal: current rises 119 → 209 → 282 mA
over ~2 s with the beam closed.

## Scale (CS1270)

| Fact | Value | Confidence |
|------|-------|------------|
| Resolution | ~1 g (0.1 g units, quantised) | field |
| Power-up | ~1.1 s before the first valid frame | spec |
| Bowl | 350 g empty bowl used as span mass | product |
| Missing bowl | Reads far below zero (≈ −300 g) | spec/field |
| Settling | Needs a settle window after dispense; a pet eating during the settle makes the delta negative or small | field |
| Creep | Small sub-threshold drift; auto-tare must only compensate per-sample creep, never erase real eating | field |

## Hopper level

IR beam across the hopper (LED on GPIO0, detector on AW9523B P1.4). Reading
the detector borrows the same GPIO expander as the index detector: **never
read the hopper while the motor runs**. An underfill with the IR still
"blocked" (food present) is a delivery fault, not an empty hopper.

## Battery ADC

4×AA pack (~6.2 V full on mains). ADC through an ~11:1 divider on the same
mux pin as the motor current. Field: readings occasionally jump to ~24.7 V
(near ADC full scale × 11) for a while, then return. **Inferred**: the pin is
read while muxed to the motor path or while GPIO17 is still owned by the
Wi-Fi SPI (it is the WFCI CS line). Current mitigation: discard > 7.5 V.
Root cause still open (08-open-questions.md).

## Shared pins with the Wi-Fi coprocessor

GPIO12–17 are shared between the WFCI SPI link to the Wi-Fi N9 core and the
feeder peripherals (I2C to the AW9523B expander, TM1637 clock, CS1270 TX,
ADC). Every peripheral access after `connsys_init()` is a short **bus loan**
(≤ 5 ms for expander/ADC/display). Consequences:

- Index beam polls can be refused (`busy`) while the Wi-Fi side is active.
  The field trace counts them (`polls ok/busy/err`); they were 0 in normal
  operation but **are a prime suspect for meals failing while the network
  misbehaves**.
- Display, scale and expander work must never hold a loan across motor spin.

## Wi-Fi / MQTT

| Fact | Confidence |
|------|------------|
| STA connect ~2.8 s after boot, SNTP sync ~0.2 s later | field (boot log) |
| A QoS 1 publish can block the client up to the command timeout (was 12 s) | code |
| The Paho client can report "connected" while every publish fails (dead TCP, no keepalive failure) — lasted ~1 h in the field | field |
| The broker sees the device go offline only through keepalive/LWT | design |

## Display

TM1637, 3 digits + icons through the AW9523B; brightness 0–4; idle blank on
battery/mains after a delay; a front-panel "Err / E n" overlay exists in the
current firmware.

## Timing budget summary (use in the simulator)

- Index poll: every ≤ 50 ms during a burst.
- Hole: open ~200 ms, period ~6.4 s on mains (slower on battery: not measured).
- Spring-back re-entry: ≤ 160 ms after start.
- Stall detection: 2 s window.
- Anti-jam reverse: 1 s; 3 tries.
- Settle after dispense: see `spec/30-processes/dispense-cycle.md`.
