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
host regressions and JavaScript syntax check. The image is 802,400 bytes;
SHA-256 `49621883830450bc50b50fa96db07c55806acbf8c49023993a010846d08652a8`. Matching BIN, ELF, gzip and build log are in `.cache/firmware`.
The device continues running local4; local5 has not been flashed or tested in Home.
