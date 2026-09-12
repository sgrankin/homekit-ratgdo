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
host regressions and JavaScript syntax check. The image is 803,472 bytes;
SHA-256 `a93244896ceefe66b68e5ef4254c2817e22ae72ce9f6982842562ca6430be543`. Matching BIN, ELF, gzip and build log are in `.cache/firmware`.
The device continues running local4; local5 has not been flashed or tested in Home.

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
3.13, and unchanged crash count 1. The new local5 image is **not installed**.
The verified 803,472-byte BIN and its 576,513-byte gzip are ready for USB;
gzip MD5 is `6603914b682d655ae088050645112b6d`.
