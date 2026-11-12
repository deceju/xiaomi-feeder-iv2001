# Open questions (unknown, find out on the device)

| # | Question | What we know | How to find out |
|---|----------|--------------|-----------------|
| Q1 | Why did two scheduled meals end `failed` with no food while MQTT was in its dead state? | Manual dispense from the web worked right after (logs still flooded). No history existed yet. | The current firmware records the reason (history: holes, polls busy/err, why/jam). Wait for the next failure, or reproduce: scheduled meal while the broker is stopped. Check `busy` polls. |
| Q2 | Does Wi-Fi activity starve the index beam reads (bus loans refused)? | Trace counts `busy` polls; 0 in normal operation. | Simulator S6 + device test: meal during heavy MQTT/web traffic and during a broker outage; compare busy counts. If confirmed: give the motor priority (net pauses bus loans while a burst runs). |
| Q3 | Root cause of the 24.7 V battery readings | Pin shared with WFCI CS and the motor mux; near full-scale × 11. | Log mux state, loan state and raw ADC for each battery sample; sample only with the loan held and P1.7 high after settle. |
| Q4 | "Overfill protection" switch not turning on from HA (reported once) | Handler and parser looked correct in review; never reproduced. | Toggle from HA with the log open; look for the `cmd feed_overfill` line and the published state. |
| Q5 | Motor speed and current on battery | Not measured. | Run the acceptance script on battery; record hole period and current. Simulator parameter. |
| Q6 | Is MQTT over TLS affordable in RAM? | ~78 KB free heap at boot. | Prototype with mbedTLS, measure heap min during a meal + web + OTA. |
| Q7 | Can the SDK httpd expose headers and query strings for a proper session cookie? | Today the CGI gets only path + body. | Read the SDK httpd source; patch if small. Otherwise keep the body-key scheme isolated. |
| Q8 | Keep or remove the hardware watchdog (30 s, resets on a full app hang)? | Owner prefers no automatic reboot; watchdog predates that decision. | Ask the owner before changing it. |
