# Engineering checklist, mapped to this device

The feeder is a single MT7682 microcontroller (FreeRTOS, ~78 KB free heap,
1 MB application bank, NVDM key-value flash) on a home LAN, talking MQTT to
Home Assistant and serving a small web page. Many web-backend practices do
not apply as such; some apply in an adapted form. Status: **Applies**,
**Adapted** (same intent, device-sized), **N/A** (with the reason, so nobody
builds it by reflex).

## Security

| Practice | Status | Requirement for the rewrite |
|----------|--------|------------------------------|
| Authentication | Applies | Optional web password → session token (04 § Network). Telnet console asks the same password. MQTT uses broker credentials. |
| Authorization | Adapted | One role (owner). Every write and every read needs the session when a password is set. UART = physical access = full access. |
| Role-Based Access Control | N/A | Single user. |
| Row-Level Security | N/A | No multi-tenant data. |
| IDOR | Adapted | Indexes in requests (history, schedule slots) bounds-checked; no object belongs to another user. |
| Session Management | Applies | Random 128-bit token in RAM, idle expiry (e.g. 12 h), invalidated on password change and reboot. |
| Token Expiration and Refresh | Adapted | Expiry + re-login; no refresh tokens. |
| Cookie Flags | Applies | `HttpOnly; SameSite=Strict; Path=/`. `Secure` only if HTTPS exists (it doesn't on device). |
| CSRF | Applies | SameSite=Strict + require a custom header (`X-Feeder: 1`) on writes. |
| Brute Force Protection | Applies | Exponential delay per failed login (1 s, 2 s, 4 s … capped 30 s); log each failure. |
| Account Lockout | Adapted | Temporary lockout after 10 failures (5 min); never permanent (owner would be locked out of his own device). |
| Password Reset Flows | Adapted | Physical only: long-press factory reset or UART command. No email. |
| Magic Links / Passkeys / SSO / 2FA | N/A | No accounts, no identity provider, LAN device. Document why. |
| API Keys | Adapted | MQTT credentials; optionally a read-only status token for HA REST sensors. |
| Hashing vs Encryption | Applies | Web password stored as a **salted hash** (PBKDF2-SHA256 via mbedTLS, per-device salt). Wi-Fi/MQTT passwords must stay reversible (needed to connect): store as-is, never echo. |
| Salting | Applies | Random salt per device, stored with the hash. |
| Secrets Management | Applies | Secrets never in logs, status, export, MQTT, error messages. Build-time secrets (`tools/ota/.env`) stay git-ignored. |
| Secret Rotation | Applies | Change web/Wi-Fi/MQTT credentials from the page (Wi-Fi with trial + fallback); session tokens die on change. |
| Encryption in Transit | Adapted | HTTP and MQTT are plain on the LAN. Optional MQTT TLS (mbedTLS) if RAM allows — measure. Remote access only through Home Assistant / the owner's nginx, never by exposing the device. |
| HTTPS / TLS | Adapted | Not on the device (RAM, cert management). Recommended path: reverse proxy (HA/nginx) if the page must leave the LAN. |
| Encryption at Rest | Adapted | NVDM is plain flash; physical access = full access. State it in the docs; do not pretend otherwise. |
| Input Validation / Data Validation on the Server | Applies | One validation path from the settings registry; every command field range-checked; unknown fields rejected; max body sizes. |
| HTTP Status Codes | Applies | 200 ok, 400 invalid field (body says which), 401 no/expired session, 404 unknown route, 409 busy (meal running), 413 body too large, 423 locked out, 503 sensor unavailable. JSON error body `{"error":"code","field":"…"}` kept for the page. |
| Mass Assignment | Applies | Import and JSON commands only touch whitelisted, exportable, non-secret fields. |
| SQL Injection | N/A | No SQL. Equivalent risk: **JSON/HTML injection** — escape every string the device emits in JSON (SSID, NTP host, problem text) and never build HTML from device strings. |
| XSS | Applies | Page inserts device-originated strings with `textContent`; no `innerHTML` with SSID/history/problem text; test with an SSID containing `<img onerror>` and quotes. |
| Content Security Policy | Applies | `Content-Security-Policy: default-src 'self'; script-src 'self' 'sha256-…'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'` (hash the inline script at build time). |
| Security Headers | Applies | `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`, `Cache-Control: no-store` on `/api/*`. |
| Clickjacking | Applies | `frame-ancestors 'none'` / `X-Frame-Options: DENY`. |
| SSRF | Adapted | The OTA download URL is user-supplied: allow only `http://` to private LAN ranges or an explicit host, cap size, check the image header/bank before writing. |
| Open Redirects | N/A | No redirects with user-controlled targets. Keep it that way. |
| Path Traversal | Applies | Fixed route table only; no file serving from paths. |
| Command Injection | Adapted | UART/telnet CLI parses tokens, never forwards strings to a shell (none exists); NTP/broker hosts validated as host names. |
| File Upload Security | Adapted | Settings import: max size, JSON schema, whitelist. Firmware is pulled (not uploaded); verify image header and size before the bank switch. |
| Dependency Vulnerabilities | Applies | Pin the SDK (lwIP, mbedTLS versions noted), pin npm dev tools, run `npm audit` in CI for the page toolchain; list known SDK CVEs that matter on a LAN device. |
| Principle of Least Privilege | Applies | Telnet console off by default in release builds; web layer cannot call adapters directly; tasks only touch ports they own. |
| Audit Logs | Applies | Log every configuration change and command with its source (`src=web|mqtt|button|schedule|uart`) — no secret values. |
| Penetration testing | Adapted | Checklist pass + fuzzing of the JSON parser, HTTP request parser and MQTT command handlers on host (libFuzzer/AFL), plus a manual LAN test (nmap, curl without session, oversized bodies). |

## Reliability

| Practice | Status | Requirement |
|----------|--------|-------------|
| Error Handling | Applies | Typed result enum everywhere; no silent drop; every failure has a reason string that reaches the log, and history/problem when user-visible. |
| Timeouts | Applies | Every blocking op has one: MQTT write (4 s), HTTP handler → app reply (3 s), scale frame, expander loan (5 ms), OTA chunk, DNS. |
| Exponential Backoff | Applies | Wi-Fi and MQTT reconnect with jitter, capped (60 s). |
| Circuit Breakers | Adapted | MQTT dead-session detection (3 failed writes or missed PINGRESP → reconnect). Hopper IR/scale: after N consecutive read errors mark the sensor "unavailable" (problem) instead of hammering it. |
| Idempotency | Applies | MQTT QoS 1 can redeliver: dispense/feed_next commands accept an optional `id`; the last 8 ids are remembered and duplicates acknowledged without dispensing again. Setting writes are naturally idempotent. |
| Race Conditions / Concurrency | Applies | Single writer per state; queues between tasks; no shared mutable struct read across tasks without a snapshot copy; ISR → queue only. Review every `volatile`. |
| Async / Await | Adapted | FreeRTOS queues + state machines; no blocking waits in the app task except bounded queue receives. |
| Background Jobs | Applies | Motor, OTA, history flush, hopper sampling are jobs with explicit lifecycle and cancellation. |
| Cron Jobs | Applies | The meal scheduler: local time (POSIX TZ) with DST rules: a slot in a skipped hour (spring) fires at the next valid minute once; a repeated hour (autumn) fires once. Test both. |
| Message Queues | Applies | FreeRTOS queues sized from worst-case bursts; a full queue is logged and counted, never silently dropped. MQTT outbox coalesces by topic. |
| Graceful Shutdown | Applies | Reboot/OTA: refuse during a meal (or finish it), stop the motor, flush pending NVDM writes and the history, publish `offline`, close sockets, then reset. |
| Health Checks | Applies | `GET /api/v1/health` (no auth, no data): `ok`/`degraded` + uptime; MQTT LWT `connection`; task heartbeats. |
| Graceful degradation | Applies | Scale missing → open-loop meals still run (estimated grams); MQTT down → schedule still runs; time unknown → E5, no schedule, manual still works. |
| Memory Leaks | Applies | No malloc after init; heap min-ever and per-task stack high-water published in diagnostics and checked in the soak test. |
| Disaster Recovery | Applies | A/B banks (stay on the old bank if the new one fails), UART flash recovery, factory reset, settings export/import, history in NVDM. Document the recovery steps in the Help tab and README. |
| Backups | Applies | Settings + schedule export (no secrets); restore validates before writing. |
| Database Migrations | Applies | Versioned NVDM records + one-time legacy migration (04 § Storage), host-tested against a snapshot of the current keys. |
| Database Indexing / Normalization / Read Replicas / Connection Pooling / Soft Deletes | N/A | No database. History is a fixed ring (oldest overwritten by design); one MQTT connection. |
| Caching / Cache Invalidation | Adapted | Web page served with `ETag` = firmware version (+ gzip); `/api/*` `no-store`. Sensor snapshots carry an age; consumers reject stale data instead of caching it. |
| CDNs / Load Balancing / Reverse Proxies / API Gateways / Horizontal vs Vertical Scaling / Stateless Services | N/A | One device, one LAN client population. (Reverse proxy only as the optional remote-access path above.) |
| Rate Limiting | Applies | Login attempts; dispense commands (one at a time, `busy`); MQTT publish coalescing; log lines rate-limited per tag (the "publish failed" flood filled the ring). |
| Webhooks | N/A | MQTT is the push channel to HA. |

## Observability

| Practice | Status | Requirement |
|----------|--------|-------------|
| Logging / Structured Logging | Applies | `[t] [tag] event key=value` lines; info+ to the RAM ring (kept across software reboot), debug to UART/telnet. |
| Distributed Tracing | Adapted | A meal correlation id across log lines, history, MQTT event. |
| Monitoring / Alerting | Applies | Problem state E1–E9 → HA binary_sensor; HA availability via LWT; the owner builds his own notifications in HA. |
| Uptime Monitoring | Adapted | `uptime_s`, boots counter, last reset reason, LWT. |
| Error Tracking | Applies | Crash record (HardFault registers, task, uptime) in `.noinit`, reported once after reboot (log + diagnostics). |
| Profiling | Applies | Per-task CPU time (FreeRTOS run-time stats) and stack high-water marks on demand (UART command + diagnostics page). |
| Debugging | Applies | UART console, telnet (password), `/api/log`, motor run trace (debug level), simulator replays of field logs. |
| Cost Monitoring | Adapted | No cloud cost. Equivalent: **power budget** on battery (radio-on time, motor energy per meal, sleep current) and **flash wear** (NVDM writes per day). |

## Delivery

| Practice | Status | Requirement |
|----------|--------|-------------|
| Environment Variables | Adapted | Build-time only (`FW_VERSION`, toolchain paths, `tools/ota/.env`). Runtime config lives in NVDM. |
| Docker | Applies (build) | A container image with the ARM toolchain, the fetched SDK and Node for the page, so any machine (or Codex/CI) builds bit-identical binaries. |
| CI/CD | Applies | GitHub Actions: generator up to date, host tests, sanitizers (ASan/UBSan on host), fuzz smoke, web tests, ARM build both banks, size budgets (bank, page gz), version string check, release artifacts (`petfeeder_a/b.bin` + checksums). |
| Infrastructure as Code | Adapted | Registry → HA discovery is the "infra as code"; optional example HA YAML in the repo. |
| Staging Environments | Adapted | Host simulator + a second feeder or a bench unit on a test MQTT prefix (`petfeeder-test/…`). |
| Feature Flags | Adapted | Compile-time (`WEB_UI_ENABLE`, `REMOTE_CLI_ENABLE`) and runtime toggles in the registry for risky behaviours (e.g. stall back-off). |
| Code Review | Applies | Every PR reviewed by a second agent/model and the owner skims; a PR template with the checklist below. |
| Branching Strategies | Applies | `main` protected; one short-lived branch per theme; rebase on main; small PRs. |
| Semantic Versioning | Applies | `vMAJOR.MINOR.PATCH` tags; `FW_VERSION` from `git describe`; MAJOR when an interface in 03-interfaces.md breaks. |
| API Versioning | Applies | `/api/v1/…`; MQTT payloads gain fields only (additive); a breaking topic change gets a new topic and a deprecation period. |
| API Documentation | Applies | Generated from the registry: MQTT topics, HA entities, web endpoints, NVDM records — committed under `docs/` and checked in CI. |
| Load Testing | Adapted | Web: 4 parallel clients polling + writes for 10 min; MQTT: 100 commands burst; scheduler: 10 slots per day for 7 simulated days. Measure heap/stack, latency, dropped messages. |
| Automated Testing | Applies | Host unit + simulator + integration on every commit (CI); on-device acceptance script before every release (06-test-strategy.md). |
| Unit vs Integration vs End-to-End | Applies | See 06-test-strategy.md. |

## Product quality

| Practice | Status | Requirement |
|----------|--------|-------------|
| Time Zones | Applies | POSIX TZ string, presets; DST transitions tested; history stores UTC + local day. |
| Character Encoding | Applies | UTF-8 everywhere; SSIDs and host names may contain non-ASCII; JSON escaping; panel shows only its own glyphs. |
| Accessibility | Applies | Labels on every control, focus order, contrast, no colour-only status (problem banner has text). |
| Mobile Responsiveness | Applies | Phone-first page (16 px gutters, no horizontal scroll); test at 375 px. |
| Data Privacy (GDPR) | Adapted | Local-only data (Wi-Fi name, logs, meal history). No telemetry leaves the LAN. Factory reset erases everything. Document it. |
