# Rewrite kit — xiaomi.feeder.iv2001 local firmware

This folder is the hand-off for a clean rewrite of the application firmware.
It captures what the current branch (`feat/complete-feeder-features`, PR #1)
learned on a real feeder, so the rewrite is **construction, not exploration**.

The goal is not less code. The goal is firmware that is **simple, ordered,
efficient and free of the bugs below**, now that the physical behaviour of
the device is understood.

## Read in this order

| File | What it gives you |
|------|-------------------|
| [01-physical-behavior.md](01-physical-behavior.md) | Measured behaviour of the motor, index sensor, scale, hopper, battery ADC, shared bus, Wi-Fi/MQTT. The ground truth. |
| [02-product-decisions.md](02-product-decisions.md) | Decisions the owner already made. Do not reopen them. |
| [03-interfaces.md](03-interfaces.md) | External contract to keep: MQTT topics, Home Assistant entities, web API, NVDM keys, migration. |
| [04-target-architecture.md](04-target-architecture.md) | The structure to build: settings registry, state model, state machines, events, storage, network, web. |
| [05-engineering-checklist.md](05-engineering-checklist.md) | Every general engineering practice (security, reliability, ops…) mapped to this device: applies / adapted / not applicable, with the concrete requirement. |
| [06-test-strategy.md](06-test-strategy.md) | Unit, simulator, integration, on-device acceptance, soak and fuzz tests. Definition of done. |
| [07-lessons-and-regressions.md](07-lessons-and-regressions.md) | Every bug met in the field, its root cause, and the regression test the rewrite must ship with. |
| [08-open-questions.md](08-open-questions.md) | What is still unknown and how to find out on the device. |
| [09-rewrite-plan.md](09-rewrite-plan.md) | Phases, milestones, review loop, prompts for the coding agent. |
| `field-logs/` | Real device logs, used as simulator fixtures. |

## Ground rules for the coding agent

1. **The repo's `AGENTS.md` still applies**: spec → failing test → code → refactor,
   ports/adapters layering, no SDK headers in `src/`, SDK changes as patches only.
2. **`spec/` stays the behavioural source of truth.** This kit says *how to
   organise* and *what we learned*; when it states a behaviour that `spec/`
   does not, update `spec/` first.
3. **Every item in 07-lessons-and-regressions.md becomes a test before the
   matching code exists.**
4. **Hardware access**: the agent cannot reach the feeder. On-device tests are
   scripts the owner runs (see 06-test-strategy.md § On-device acceptance);
   their output is pasted back.
5. **Small pull requests**, one theme each (see 09-rewrite-plan.md).
6. **Never trade a field-proven behaviour for elegance** without a simulator
   test that reproduces the field log it came from.
