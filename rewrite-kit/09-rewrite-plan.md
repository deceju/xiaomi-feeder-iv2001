# Rewrite plan

Goal: same behaviour as today (02 + 03), simpler and more reliable code
(04), with every lesson in 07 covered by a test. Not a feature project.

## Rules for the agent (Codex or other)

1. Work on a branch `rewrite/<phase>` from `rewrite-kit`. One phase = one
   or a few small PRs (≤ ~800 changed lines excluding generated files).
2. Spec first, then failing tests, then code (definition of done in 06).
3. Never change a public interface in 03 without writing it in the PR
   description and getting the owner's OK.
4. Never touch `sdk/` except through the existing patch mechanism.
5. Keep both banks building and the host suite green after every commit.
6. Ask instead of guessing when 01/02 do not answer a question; add the
   question to 08.
7. The owner tests on his feeder: every PR that changes behaviour on the
   device ends with the exact commands/steps he must run and what to paste
   back.

## Phases

| # | Phase | Content | Exit criteria |
|---|-------|---------|---------------|
| P0 | CI + tooling | GitHub Actions (or local `make ci`): host tests with ASan/UBSan, web tests, ARM build both banks in Docker (pinned toolchain image), size budgets, version check (L26), global test timeout | CI green on the current code |
| P1 | Physical simulator | `firmware/sim/` per 06; S1–S6 written against the **current** motor code first (they must pass or reveal bugs) | S1–S6 run in CI |
| P2 | Settings registry + generator | `tools/registry/`, generated getters/validation/NVDM/MQTT/HA/web descriptor/labels; migrate settings one group at a time behind the same interfaces | generated files checked in CI; L14, L19, L24 tests |
| P3 | Storage + migration | versioned records, debounce, legacy migration (S22), crash record | migration tested from a real NVDM dump; acceptance on device |
| P4 | Net task + MQTT session | single owner, session state machine, liveness, coalescing outbox, dedup (S14, S15, L16–L18, L25) | broker-cut test on device |
| P5 | Motor state machine | burst SM per 04 § 5 on the simulator (S1–S6, L1–L10) | acceptance steps 2–3 on device, field-log scenario passes |
| P6 | Meal job + events | meal SM, `meal_result` event, history/problems/learning/limits/not-eaten as subscribers (S7–S13, S16) | 3 days on the owner's feeder without a wrong portion |
| P7 | Health + diagnostics | problems, counters, task watch, structured logs with meal id, rate limits | E1–E9 reproduced in the simulator |
| P8 | Web API + auth | httpd patch (Q7), `/api/v1`, session cookie, status codes, lockout, security headers (S21, L20–L22) | web tests + load test |
| P9 | Page | settings rendered from the descriptor, i18n by key, textContent only, dirty fields (L23) | Playwright tests, size ≤ 24 KB gz |
| P10 | Display + cleanup | single presentation function; delete dead code; docs | 24 h soak + full acceptance; release vX.0.0 |

P1 comes before any rewrite so the simulator is validated against code
whose field behaviour we know.

## Review loop

For each PR: the writing model opens it; a **second model** (Claude or
another) reviews it with this prompt:

> Review this PR against rewrite-kit/ (01 physical behaviour, 02 decisions,
> 03 interfaces, 05 checklist, 07 lessons). List: behaviour changes not
> allowed by 02/03; missing tests for 07 rows touched; blocking calls without
> timeout; shared state written by more than one task; secrets that could
> leak in logs/status/export; anything the simulator cannot see. Be specific
> (file:line).

The owner merges only when CI is green and review comments are answered.

## Prompt to start Codex (phase P0)

> Read rewrite-kit/README.md and every file it lists, in order, before doing
> anything. Then do phase P0 of rewrite-kit/09-rewrite-plan.md on a new
> branch `rewrite/p0-ci`. Do not change firmware behaviour. When done, show
> me the CI result, the list of files changed, and the exact commands I must
> run locally to check it.

Next phases: same prompt with the phase number, after the previous PR is
merged.
