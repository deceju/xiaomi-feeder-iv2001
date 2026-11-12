# Lessons and required regression tests

Every row was a real bug on the owner's feeder or in review. The rewrite ships
a test for each (simulator scenario numbers refer to 06-test-strategy.md).

## Motor and dispensing

| # | Symptom in the field | Root cause | Regression test |
|---|----------------------|------------|-----------------|
| L1 | 10 g (1 portion) dispense: motor barely turns, nothing falls; 11 g works | Parked hole counted as the first pulse when the baseline read failed (expander busy) and defaulted to "closed" | S1 + baseline read failure injected → assume parked open |
| L2 | 1-portion bursts stop after a twitch; N-portion meals one short | Start-up beam flicker counted as a hole | S2 |
| L3 | 10 g dispenses deliver nothing (after L1/L2 fixes) | **Spring-back**: the auger re-enters the same hole within 160 ms of the next start | S1 with spring-back; hole counts only after ≥ 1.5 s closed |
| L4 | A stall after the first pulse ran until the 20 s limit | Index timeout covered only "0 pulses", not each gap | per-gap timeout test |
| L5 | 5-portion meals cut short | Session limit fixed at 20 s while 5 holes take ~32 s | S3 |
| L6 | 13-portion meal: anti-jam starts and is cut the same millisecond as "session timeout (20000 ms)" | Anti-jam phases used the 20 s motor maximum instead of the burst limit | S4 (field log) |
| L7 | After anti-jam, extra portions dispensed | Pulse count restarted after resume | S4 asserts total pulses |
| L8 | Stalled auger at ~280 mA not detected | Only instant/sustained ADC thresholds; stall current is lower | S5 with 280 mA |
| L9 | One failed meal turned every later one into `empty_hopper` | Underfill latched "empty" even with the IR blocked | S10 |
| L10 | Hopper reads disturbed the index during meals | Hopper IR read borrows the same expander | no hopper reads while a meal is active (test) |
| L11 | 10 g constant under-dispensed by ~15 % | Real mass per hole ~8.5 g | S7 (learning) |
| L12 | Owner confusion: calibration auto vs manual | Decision churn | 02-product-decisions.md fixes it |

## Weighing and eaten-today

| # | Symptom | Root cause | Test |
|---|---------|------------|------|
| L13 | Bowl weight stuck after eating; eaten-today never counts | Auto-tare pinned the presented mass to the last anchor on every quiet sample (eating erased) | pet eats 20 g between meals → bowl −20 g, eaten +20 g |
| L14 | Overfill threshold 300 stored as 44 | MQTT value narrowed to uint8 before the range check | range-check before narrowing, registry types |
| L15 | Battery ~24.8 V spikes alternating with 6.2 V | ADC read on a pin shared with Wi-Fi SPI / motor mux (suspected); no plausibility filter | S19 + root-cause investigation (08) |

## Network, web, security

| # | Symptom | Root cause | Test |
|---|---------|------------|------|
| L16 | MQTT "publish failed" every 250 ms for an hour; never reconnected | Paho kept "connected" on a dead socket; publish failures never tore down the session | S14 |
| L17 | The log ring held only that flood | No per-tag rate limit; debug traces also filled the ring | log rate-limit test; debug not in the ring |
| L18 | QoS 1 publish could block the net task 12 s | Command timeout too long, blocking design | timeout test (≤ 4 s) |
| L19 | Password change always "rejected", nothing in the log | Page sent `password`, handler read `new` (two hand-written copies of the contract) | registry-generated contract test; rejections logged |
| L20 | With a password set, Feeding/Overfill settings empty on the page | Keyed reads on read+write paths dispatched as empty writes | API test per route: read vs write with/without session |
| L21 | Password only enforced on writes; data readable | Reads were never gated | S21 |
| L22 | Telnet console gave full control with no password | Bench feature left open | console auth test |
| L23 | A typed value (not-eaten hours) reverted to the old value | The 5 s refresh overwrote fields the user was editing | page test: dirty fields survive refresh |
| L24 | `cmd/reboot`, `cmd/dispense_amount`… ignored | Route missing from the dispatcher accept list (hand-maintained list) | generated routes + test that every registry command routes |
| L25 | Connect burst overflowed the 24-slot outbox | Every module enqueues its own retained message | coalescing outbox test; connect snapshot within budget |

## Build and tooling

| # | Symptom | Root cause | Test |
|---|---------|------------|------|
| L26 | HA showed `dev` as firmware version | `FW_VERSION` defined inside the bank-B-only Makefile block; make doesn't rebuild on -D changes | CI: both binaries contain the expected version |
| L27 | GigaDevice flash hung at boot | SDK combo tables and vendor flag missing; QPI sequence wrong for GD25LQ16 | keep the three flash patches; boot test on both flash vendors if available |
| L28 | Host tests hung forever | Outbox drain loops without advancing the fake clock + rate limiter | test helpers always advance time; global test timeout |
| L29 | Mock web API accepted what the firmware rejected | Mock written separately from the firmware contract | mock generated from the registry or replaced by the host-built firmware API |

## Process lessons

- **Diagnostics before fixes.** Two motor "fixes" based on hypotheses made the
  feeder worse. Field traces settled it in one pass. Ship history, counters
  and a reboot-surviving log first.
- **One definition per contract.** L19, L20, L24, L25, L29 all come from
  hand-maintained copies.
- **Fakes must be at least as strict as reality** (L16, L28, L29).
- **Small PRs.** The 47-commit PR is hard to review.
