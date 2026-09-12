# OTA transfer investigation and local4 results

## Current outcome (2026-09-12)

Local4 is installed and successfully completed an unrestricted gzip OTA reflash
in 39.097 seconds. Pairing and door state were retained. The user subsequently
reported substantially reduced packet loss; no numeric long-duration follow-up
sample was supplied. Keep local4 running for normal-use observation. Earlier
failures below are historical evidence, not the current installation status.

## Initial local2 attempts

Two authorized uploads on 2026-09-12 failed and recovered to local2. The door
reported closed after both; pairing remained intact and crashCount stayed at 1.
The next captured attempt is recorded separately below.

| Attempt | Image bytes | Pacing | Client result |
| --- | ---: | --- | --- |
| Uncompressed local3 | 800320 | Unrestricted | Reset after 53.281 s |
| Gzip local3 | 574976 | curl limit-rate 8k | Reset after 49.180 s |

Both curl runs reported 133206 uploaded bytes. This measures bytes handed to
the client transport, not bytes acknowledged by the controller or written to
flash; it cannot establish controller progress.

The second device log records upload start at uptime 02:53:13.473, services
shutdown at 13.484, initial progress at 13.802, upload abort at 43.492, and
recovery reboot at 44.994. This matches the 30-second idle guard and 1.5-second
recovery delay. The first attempt has the same pattern. Neither log contains
a 5% progress message.

## Source findings

- The ESP8266WebServer multipart parser delivers each full 2048-byte upload
  buffer to our handler. Each delivery resets the idle timer, before flash
  writing. The near-exact start-to-abort interval therefore suggests neither
  transfer reached its first full-buffer callback. This is an inference: the
  local2 logging did not report partial-buffer bytes or last-progress time.
- HomeKit teardown closes its own client sockets, listener and mDNS responder.
  No global TCP stop call was found in this path. Door communication shutdown
  disables its software serial receiver and frees serial buffers.
- Update.begin allocates staging state and a buffer; actual flash writes happen
  later. There was no logged begin, size, MD5 or flash-write error before abort.
- The parser waits for data while the TCP client remains connected. Our idle
  guard closes that connection, allowing the parser to report ABORTED and
  normal-loop recovery to reboot. A longer curl timeout cannot override this
  device-side guard.
- The then-bundled lib/lwip2/README.md documented a custom limit of two TCP retransmissions; the
  standard pinned framework header defaults to twelve. This is a candidate for
  improving loss tolerance, not a demonstrated cause of these receive stalls.
  In particular, retransmitting controller data and receiving upload data are
  different directions of traffic. The custom library predates our changes.
- Gzip round-trip verification passed locally, and the prepared image's first
  4096 bytes match the local2 image installed over USB. The Arduino updater and
  eboot sources support gzip. Neither attempt got far enough to implicate image
  installation or decompression.

## Next evidence

A TCP-header capture of an authorized upload can distinguish missing ACKs,
retransmission backoff, a zero receive window and a connection reset. Capture
only host 10.0.90.4 port 80; do not capture payload or unrelated traffic.
An attempted capture during a read-only status GET failed because macOS denied
access to /dev/bpf0; passwordless sudo is unavailable. The status GET succeeded.

The user subsequently authorized interactive sudo with Touch ID. A narrowly
filtered live capture then succeeded (25 packets, no kernel drops) and was
stopped after two read-only status requests:

- At 10:53:28–33 EDT, six outgoing SYNs were visible with no incoming SYN-ACK;
  curl hit its five-second connection timeout.
- At 10:53:59, the next connection required one SYN retry. The controller then
  advertised MSS 536 and a 2144-byte receive window. Its complete 1894-byte
  status response arrived in about 141 ms after handshake, with no visible
  response-data retransmissions. Total curl time was 1.155 seconds.
- Status still reported local2, door Closed, paired true, crashCount 1.

This establishes intermittent connection failure before HTTP/OTA processing,
but cannot distinguish radio loss, mesh behavior or device scheduling. The
small GET does not fill the receive window and cannot explain the upload stall.
No OTA was performed during this capture.

## Authorized captured upload at 10:57 EDT

A subsequent separately authorized attempt used the same gzip image and 8 KiB/s
pacing. Capture text is `/tmp/ratgdo-ota-capture.txt` (144 packets, no kernel
drops, including preflight requests). The upload used client port 52671.

- Connection establishment took seven SYN transmissions, completing at
  10:57:36.336.
- Transfer progressed for about ten seconds. At 10:57:46.968 the controller
  acknowledged TCP sequence 23785 with a nonzero receive window of 872 bytes.
  Sequence counts include HTTP/multipart headers, not just firmware bytes.
- The Mac repeatedly retransmitted sequence 23785, initially 512 bytes and
  later 536 bytes. No controller packets appeared until 10:58:16.757.
- That next packet was a FIN acknowledging sequence 24321 with a receive window
  of 1944. Thus another 536 bytes had been received by connection closure, but
  the capture cannot say when they arrived at the device. The Mac sent more
  data, and the controller reset the connection.
- curl failed with Broken pipe after 47.743 seconds, reporting 154598 bytes
  handed to its transport. This again exceeds actual acknowledged bytes.
- Saved logs show upload start at uptime 00:10:29.566, abort at 00:11:09.858,
  and recovery reboot at 00:11:11.358. Unlike the first two attempts, this ran
  about forty seconds from start, consistent with initial progress followed
  by thirty seconds without another full-buffer callback.

This attempt rules out an immediate, unconditional shutdown-path failure and
shows an ACK-progress stall with the last observed receive window still open.
It does not establish whether data/ACKs were lost on the network or device TCP
processing was delayed. No 5% log entry is expected for only about 23 KiB of a
575 KB image. USB-side receive/timeout diagnostics or comparison over a different
network path remain useful; no speculative transport change was deployed.

## Simultaneous reachability comparison

After the captured attempt, thirty ICMP probes per target ran concurrently from
the Mac, at two probes per second per target:

| Target | Loss | Mean RTT | Maximum RTT |
| --- | ---: | ---: | ---: |
| Gateway 10.0.90.1 | 0% | 10.256 ms | 18.846 ms |
| Apple TV hub 10.0.90.155 | 0% | 10.443 ms | 18.663 ms |
| Controller 10.0.90.4 | 30% | 177.148 ms | 859.343 ms |

This short sample points away from a general failure of the Mac's Wi-Fi link,
but cannot separate the controller's radio, its mesh path, or its firmware.
ICMP loss is not a measurement of TCP loss. Interface inspection found en0 as
the active LAN path; the Mac's Ethernet interfaces have no active link. A wired
second host would provide a stronger comparison without touching the controller.

The user supplied SSH host `atom`. Its route to the controller is Ethernet
`eth0`, source 10.0.90.6. An overlapping thirty-probe comparison produced:

| Source / target | Loss | Mean RTT | Maximum RTT |
| --- | ---: | ---: | ---: |
| atom / gateway | 0% | 0.277 ms | 0.377 ms |
| atom / Apple TV | 0% | 11.080 ms | 29.650 ms |
| atom / controller | 60% | 131.027 ms | 654.029 ms |
| Mac / controller | 53.3% | 145.098 ms | 705.169 ms |

Three subsequent sequential HTTP status reads from atom all returned full
1894-byte HTTP 200 responses. Connection times were 0.097, 1.054 and 3.233
seconds; total times were 0.481, 1.132 and 3.537 seconds. This corroborates
variable TCP connection establishment from Ethernet, while small transfers can
still complete. The controller problem is not specific to the Mac's Wi-Fi leg.
Controller radio/firmware and its particular AP/mesh path remain unresolved.
No firmware upload or device settings change was made from atom.

## Changes deployed in local4

The retry-limit rationale was verified in original commits 99f1d634 and
64e137c8: a vanished peer could retain about 4 KiB of TCP heap for about thirty
minutes. Local4 removes the custom archive, retains the framework's low-memory,
low-flash/no-fragmentation configuration, and aborts HomeKit sockets on teardown
so they cannot retain pending TCP state. Local3's bounded write/ACK handling is
included. This is not a claim that ordinary HTTP connections have the same
application-level bounds; observe heap under loss after deployment.

Local4 also emits one `OTA idle timeout` record before closing a stalled upload:
`idle` is milliseconds since the last full-buffer callback; `total` and `partial`
are the multipart parser's completed and current firmware byte counts; `flashed`
is the updater's written-byte progress (it excludes its buffered bytes); `tcp`
is TCP state; `rx` is unread socket data; `heap` is free heap. Like existing
errors this uses the normal logger; no periodic logging or new direct flash
write is added. The existing recovery reboot can save the log as before.

Host tests check the timeout record and suppress duplicates for the same stall,
in addition to existing recovery/scheduling/rollover cases. The teardown test
exercises the production function over 1000 vanished-peer sockets and would
fail with graceful stop instead of abort. Deployment results follow.

## Local4 installation and successful OTA reflash

Local4 was subsequently installed and digest-verified over USB with user
authorization. The user then authorized an OTA reflash and explicitly requested
unrestricted upload speed. The gzip image was 575098 bytes, MD5
`9ea0802a28f0ceaadfc3d81c12ac4f8e`, expanding to the verified 800624-byte image.
The upload returned HTTP 200 and `Upload Success.` after 39.097 seconds. An
explicit reboot completed installation. Post-boot status confirmed local4,
uptime 27975 ms, door Closed, paired true, one HomeKit client, unchanged accessory
identity and crashCount 1. The first boot-check connection timed out; the second
succeeded. Thus OTA succeeded, but intermittent network trouble is not ruled out.

TCP capture: `/tmp/ratgdo-local4-ota-capture.txt`, 2470 packets, no kernel drops.
This single successful attempt does not isolate the cause of improvement: the
firmware, reboot and USB/power circumstances changed since the failed attempts.

If installing diagnostic firmware over USB, log one record when the idle guard
fires: parser totalSize/currentSize, updater progress, TCP state, available RX,
heap and elapsed time since the last callback. This avoids repetitive logging
and would establish whether bytes reached the parser. Do not increase the
timeout or remove recovery merely to conceal the stall.

Restoring the standard framework TCP library is a separate candidate change.
Verify the linked library and test loss/backpressure behavior before deployment;
more retransmissions can also retain pending connections and buffers longer.
