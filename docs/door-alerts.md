# Garage door alert sensors (local5, ESP8266)

Two read-only HomeKit contact sensors accompany the existing garage accessory:

| Sensor | Contact opens when | Contact closes when |
| --- | --- | --- |
| Garage Left Open | The door has been continuously not fully closed for the configured duration | Fully closed, or the duration is disabled/increased beyond elapsed time |
| Garage Close Failed | Observed closing stops/reverses, remains incomplete for 60 seconds, or the existing three-second close-start check fails | Fully closed |

Set **Garage Left Open** in the existing web settings: whole minutes from 0 to
1440, default 15. Zero disables that alert without removing its HomeKit service.
Saving takes effect without a reboot and uses the normal settings persistence.
The firmware rejects malformed/out-of-range values. Changing the duration uses
time already elapsed in this boot, including time while the alert was disabled.

Enable each sensor's notifications separately in Apple Home after installation.
The two services have fixed IDs, existing service IDs are preserved, and the HAP
configuration number is 6 so paired clients can refresh the layout. Each sensor
exposes both Name and a read-only Configured Name with the labels above. This
avoids accepting name writes that would disappear on reboot. Existing Apple
Home names may remain cached; presentation still needs confirmation in Home.
Pairing storage is unchanged. Actual Home presentation/notification delivery
still requires verification on the installed firmware and the user's hub.

Opening, stopped halfway and closing all count as not fully closed. Unknown
startup state does not start an alert. Losing known status after an opening
does not clear the timer or failure latch. Timers and alerts restart on reboot;
they do not infer duration from saved door history. Elapsed open time saturates
after 24 hours, keeping an alert asserted across clock rollover.

Closing observed from any source (HomeKit, wall control or remote) arms the
failure timer. Intentional stops/reversals can also trigger it. The three-second
failure-to-start check covers commands sent by this firmware; a remote command
that causes no observable motion cannot always be detected. A later retry does
not clear a failure until the door is fully closed. Disabling the left-open timer
does not disable close-failure detection. No automatic close/retry is added.

The previous missing-close-completion callback synthesized a Closed state. It
now requests opener status and retains the last observed state, allowing the
alert timer to detect an unconfirmed close. A real closed status clears alerts.

Host tests cover stopped/reversed/normal closes, failure latching, no-start
failure, unknown states, settings changes, invalid inputs, long durations and
clock rollover. They exercise the production completion callback and all four
light/motion service combinations, checking both contact services survive and
their IDs do not collide. Notifications are emitted only on state changes;
timers and alert transitions add no settings/flash writes.

Build status: the latest local5 firmware build and host regressions passed on
2026-09-13. The latest image is 803,504 bytes;
SHA-256 `a58ef006a63b9c145713d631265009548ed2e572debb05c548a1fba80f5f8220`. Matching BIN, ELF, gzip and build log are in `.cache/firmware`.
This image includes the SSE cleanup below, installed by OTA on 2026-09-13
under charger power with the added choke. The preceding sensor-name build
was installed under laptop USB power. Apple Home sensor presentation and notifications still need user verification.

## Missed-status recovery

Local5 also services Sec+2.0 serial reception at cooperative yields, including
HomeKit TCP waits. A fixed 2 KiB RAM queue retains bytes for the normal loop;
the background service never transmits, decodes packets, or calls HomeKit.
The normal loop drains one complete packet per pass, rather than one byte.
This protects against application stalls while yields still run; it cannot
recover edges lost during interrupt starvation or an indefinitely stalled CPU.

Status queries no longer depend on another incoming packet: after five minutes
without valid status, the controller asks the opener again. During observed
movement or receive-loss recovery, queries can occur every five seconds. Three
unanswered queries cause a fallback to the five-minute cadence. A valid status
resets recovery. Queries use normal bus arbitration and never actuate the door.

`/status.json` exposes `sec2RxOverflows`, `sec2RxMaxGapMs`, `sec2StatusQueries`,
`sec2StatusAgeMs`, `sec2StatusKnown`, and `sec2BackgroundRx`. These diagnostics
live in RAM. Overflow warnings are limited to once per minute; lost bytes reset
the packet reader and trigger a status refresh. The background queue adds about
2 KiB of static RAM. Allocation failure for the yield callback is logged and
reported by `sec2BackgroundRx=false`; normal-loop reception remains available.

Polling does advance the existing rolling code, which is saved every ten
increments. Continuous silent-bus polling at five-minute intervals therefore
adds approximately 29 rolling-code saves per day, plus bounded fast retries.
This change does not alter rolling-code persistence or its reboot safety margin.
The diagnostic counters and polling timers themselves cause no flash writes.

The September 12 incident had a physical close around 15:45 and a reported
Open-to-Closed transition at 15:48:47, together with Light Off. HomeKit ACK
failures and opener decode errors were present nearby. This supports a missed
status update but does not establish whether software stalls or wiring noise
caused it. Host tests cover a two-second stalled consumer, queue and serial
overflows, silent-bus polling, bounded retries, and timer rollover. The changes
are now installed; long-term recovery behavior still needs observation.

Earlier OTA attempt (2026-09-12, normal-speed gzip): failed after 71.0 seconds
with a broken pipe. The device logged a 30,001 ms idle timeout after 10,240
received bytes (8,192 flashed, 1,661 partial), then rebooted through normal
recovery. Post-reboot status confirmed local4, Closed, paired, opener firmware
3.13, and unchanged crash count 1. At that point the new local5 image was not installed (later deployed below).
The latest 803,504-byte BIN and its 576,487-byte gzip are ready for USB;
gzip MD5 is `d164c7a84d5b03d6d335527e9d8bfd0d`.

## Verification upload timeout

Receive-only (`action=verify`) uploads now use the same 30-second inactivity
watchdog as firmware uploads. It is armed before metadata validation, refreshed
on accepted chunks, and canceled on completion or abort. The scheduled yield
callback closes the upload socket so the multipart parser can return; it does
not perform firmware writes or stop services. Aborting verification does not
schedule a reboot or invalidate an already staged firmware image. Host tests
exercise first-chunk stalls, progress, completion, rejected metadata, subsequent
requests, and preservation of a staged image.

This closes a recovery gap exposed by the receive-only diagnostic: that transfer
also stalled (75% controller ping loss, 0% gateway loss) without OTA service
shutdown or flash writes. The device then stopped responding, including to a
remote reboot request. The original transfer failure remains under investigation;
this fix bounds the parser stall while cooperative scheduling continues to run.


## Logging and SSE diagnostics

The log ring now wraps exact fills before writing its terminating NUL, preventing
an out-of-bounds byte write. A host regression extracts the production append
code and buffer struct, reproduces the original ASan failure, and checks retained
history across exact fills and repeated wraps.

Status JSON additionally reports `sseSubscriptions` (allocated slots, including
pending connections), `sseConnected` (SSE flag and locally connected socket),
`sseLogViewers` (connected log viewers), and `sseHeartbeats` (connected streams
with heartbeats enabled). Counts are snapshots, not browser liveness probes;
a broken peer may remain counted until TCP detects it. The existing `clients`
field counts HomeKit connections, not SSE. These fields are present in the
installed local5 image.

The main web panel requests a default one-second heartbeat; the log viewer
explicitly disables heartbeats but receives synchronous broadcasts per log line.
The old heartbeat used a scheduled recurrent callback to build small JSON,
flush/write its socket, and yield, conflicting with the scheduler's restrictions.
The installed ESP8266 cleanup moves that work into web_loop(), as detailed below.


## USB power comparison and deployment, 2026-09-13

User clarified that USB supplies ratgdo from a battery-powered laptop; otherwise
it uses an AC-mains iPhone charger. Supply noise, voltage/cable losses and ground
coupling therefore differ. The comparison did not change power alone: RSSI
improved from approximately -59 to -50 dBm on the same BSSID, and opening the
serial port appears to have reset local4 despite configuring DTR/RTS inactive.
Physical door was confirmed closed; no actuator commands were sent.

Before changing firmware, local4 completed a receive-only upload in 13.019 s:
13/13 controller and gateway pings succeeded. This contrasts with the earlier
60-second receive-only stall and 75% controller ping loss on charger power.
Idle USB baseline was 14/15 replies, average 56.5 ms.

Actual local5 OTA then succeeded in 46.011 s, uploading all 576,560 compressed
bytes with device-side completion and HTTP 200. Controller ping loss was 19.6%
(37/46 replies), gateway 0%. An explicit POST /reboot activated local5.
The installed BIN is 803,536 bytes, SHA-256
`cffa2b1190e5575fbe2d5e34a8d60f93f16eae6848069f229ddc737d5bbcd088`.

Post-boot status confirms local5, Closed, paired, crash count 1, background RX
active, zero RX overflows, and zero SSE subscriptions. Startup HomeKit reconnect
churn still occurred; after 104 s, free heap was 18,744 bytes and clients 1.
Maximum RX-service gap was 1,485 ms (includes startup). Opener firmware remained
000.000 and pin-based obstruction detection inactive. Stability is not proven.

A further diagnostic limitation surfaced: the 2,048-byte status buffer drops
trailing fields; sseSubscriptions was present, but the other SSE counters and
trailing web counters were omitted. The serializer retained valid JSON and
logged a capacity warning. This was corrected by the subsequent status-capacity deployment below.

Evidence is saved under /tmp/ratgdo-usb-power-serial.log,
/tmp/ratgdo-ota-retry-usb-power-*, /tmp/ratgdo-ota-retry-usb-power-flash-*, and
/tmp/ratgdo-local5-usb-*. This comparison strengthens a power/setup-dependent
hypothesis but cannot distinguish charger noise from signal/placement effects
or a fresh reset. It does not establish that software issues are resolved.


## Status capacity correction and HomeKit observation

The ESP8266 status buffer is now 2,560 bytes (+512 bytes). Firmware rebuilt with
host regressions passing and deployed by OTA under USB power in 31.725 s; ping
loss during transfer was 6.2% (30/32 replies), gateway 0%. Post-boot HTTP status
was 2,146 bytes and contained all four SSE counters plus webRequests,
webMaxResponseTime and ttcActive. All SSE counts were zero. It reported Closed,
GDO firmware 3.13, background RX enabled, zero overflows, clients 1 and free heap
18,784 bytes at uptime 60 s.

The earlier startup burst involved six controller addresses (.150 through .155).
Two connections (.151 and .155) supplied the same absent pairing identifier,
causing authentication rejection. Separate sends timed out waiting for TCP ACKs
and were aborted by ratgdo. Those are distinct failure paths, not proof that
pairing storage is corrupt. The library also emits Client verified after its
final verification response send fails, so that message alone does not prove
that the controller received the response.

On the status-buffer-fix boot, Apple TV .155 encountered one 2-second ACK timeout sending
the final verification response, disconnected, retried immediately, and verified
on the second connection. Subsequent status showed one client. We have not yet
identified why the ACK is absent. No SSE subscriptions were active; heartbeat
callbacks are therefore not an explanation for this observed failure.

### Sensor naming follow-up (2026-09-13)

Apple Home displayed both contact sensors using the accessory name despite
their distinct Name characteristics. Added read-only Configured Name labels
with IDs 103/113 and advanced the HAP configuration number to 6. Existing
service/state IDs and pairing storage are unchanged. Host layout checks pass
for all four light/motion combinations; the complete host suite and offline
ESP8266 build pass.

Normal-speed OTA under laptop USB power succeeded in 25.922 seconds; device
and gateway pings both received 26/26 replies. After the requested reboot,
mDNS advertises c#=6 with the same accessory ID and paired flag. Status reports
Closed, paired, two HomeKit clients and zero receive overflows at 29 seconds
uptime. Apple Home's rendering of the updated names still needs user confirmation;
it may retain names already cached in the Home database.

### SSE heartbeat cleanup (installed 2026-09-13)

ESP8266 dashboard heartbeats now run from web_loop(), replacing per-client
Ticker callbacks that scheduled TCP writes and yields inside a recurrent
callback. The ESP8266 core explicitly forbids blocking work and yield/delay
inside those callbacks. A round-robin scan sends at most one due heartbeat per
loop pass, skips disabled/disconnected streams, and coalesces missed intervals.
Heartbeats pause while OTA services are stopped. Individual writes retain the
existing socket timeout; this change does not make all web writes nonblocking.
ESP32 retains its existing timer path.

The host test exercises the production scheduler for fairness, disabled and
disconnected streams, long pauses, OTA suspension, removal during a send, and
32-bit millisecond wrap. The full host suite and offline ESP8266 build pass.
Before installing this cleanup, the first 60-ping sample after adding the choke had no loss, 45.424 ms average latency
and 186.080 ms maximum; the gateway also had no loss. This short sample does
not isolate the choke as the cause of the improvement.

The user then requested deployment of the heartbeat cleanup. Normal-speed OTA
under charger power with the choke succeeded in 79.840 seconds (HTTP 200,
Upload Success), followed by an explicit reboot to activate it. During upload,
ratgdo received 58/80 pings (27.5% loss), average 103.221 ms and maximum
643.736 ms; the gateway received 80/80. Thus the earlier zero-loss idle result
does not establish loss-free operation during OTA under charger power.

At 77 seconds uptime after activation, status reported Closed, paired, one
HomeKit client, -53 dBm, zero receive overflows and unchanged crash count 1.
The first 60-ping sample spanning startup received 50/60 (16.7% loss), with
loss concentrated earlier in the sample; its final 29 replies were consecutive.
The concurrent gateway sample received 60/60.

A subsequent settled sample received 28/30 pings (6.7% loss), averaging
46.492 ms with a 186.489 ms maximum. Some loss therefore remains after startup;
the choke may help, but the earlier zero-loss sample was not a lasting guarantee.
