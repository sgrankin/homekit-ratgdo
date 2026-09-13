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
configuration number advances to 5 so paired clients can refresh the layout.
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

Build status: local5 firmware build passed on 2026-09-12, following the passing
host regressions and JavaScript syntax check. The image is 803,536 bytes;
SHA-256 `cffa2b1190e5575fbe2d5e34a8d60f93f16eae6848069f229ddc737d5bbcd088`. Matching BIN, ELF, gzip and build log are in `.cache/firmware`.
Installed by OTA on 2026-09-13 under laptop USB power; see the deployment comparison below. Apple Home sensor presentation and notifications still need user verification.

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
overflows, silent-bus polling, bounded retries, and timer rollover. Hardware
validation of these changes is still pending.

Latest OTA attempt (2026-09-12, normal-speed gzip): failed after 71.0 seconds
with a broken pipe. The device logged a 30,001 ms idle timeout after 10,240
received bytes (8,192 flashed, 1,661 partial), then rebooted through normal
recovery. Post-reboot status confirmed local4, Closed, paired, opener firmware
3.13, and unchanged crash count 1. At that point the new local5 image was not installed (later deployed below).
The verified 803,536-byte BIN and its 576,560-byte gzip are ready for USB;
gzip MD5 is `2ba27fdde8a2df2147c4887ae15c8cdc`.

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
field counts HomeKit connections, not SSE. These fields require the new image.

The main web panel requests a default one-second heartbeat; the log viewer
explicitly disables heartbeats but receives synchronous broadcasts per log line.
The existing heartbeat uses a scheduled recurrent callback to build small JSON,
flush/write its socket, and yield. That conflicts with the scheduler's restriction
against yielding or long-running work in recurrent callbacks. The current change
adds observability; moving heartbeat I/O out of that context is still pending.


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
logged a capacity warning. Status capacity needs correction in a follow-up.

Evidence is saved under /tmp/ratgdo-usb-power-serial.log,
/tmp/ratgdo-ota-retry-usb-power-*, /tmp/ratgdo-ota-retry-usb-power-flash-*, and
/tmp/ratgdo-local5-usb-*. This comparison strengthens a power/setup-dependent
hypothesis but cannot distinguish charger noise from signal/placement effects
or a fresh reset. It does not establish that software issues are resolved.
