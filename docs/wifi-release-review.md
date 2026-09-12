# ESP8266 network review: last ten releases

Compared v2.1.2 through v2.2.4, using v2.1.1 as the baseline so changes in the
first release are included. This is source/tag evidence, not proof that all
historical release artifacts used identical framework versions.

## Direct Wi-Fi/TCP settings

Across the range, `src/wifi_8266.cpp` only gains the web header and a call to
`notify_new_ipv4_address()` after acquiring an address. Station mode, disabled
Wi-Fi sleep, PHY selection, power selection, auto-reconnect and saved-credential
connection behavior are unchanged. `lib/lwip2` has no commits in the range; its
custom maximum of two TCP retries predates these releases.

The HomeKit 500 ms paired-socket timeout, synchronous writes, and short-write →
close policy also predate the range. The top-level `espressif8266` PlatformIO
platform was unpinned throughout, and EspSoftwareSerial follows `autobaud`, so
source comparison alone cannot exclude dependency/build variation in binaries.
Our local fork pins the ESP8266 platform to 4.2.1 (Arduino core 3.1.2).

## Changes that could affect network behavior indirectly

| Release | Relevant change / assessment |
| --- | --- |
| 2.1.2 | Earlier OTA failure checks; no direct normal-operation radio-setting change found. |
| 2.1.3 | Publishes door/device status over mDNS, with a subsequent rate-limiting change in the same release. This adds multicast work/traffic; no evidence yet that it causes current loss. |
| 2.1.4 | Notifies web clients of a new IPv4 address; adds NTP-server configuration. |
| 2.1.5 | Fixes SSE free-slot selection; affects browser connection handling. |
| 2.1.6 | HomeKit pin ec2c3b13 → 7b8be01b: accept-race fix, advertised HAP version 1.0 → 1.1, restored Ed25519 verification, and log ordering. Verification adds CPU work during reconnects; do not disable this authentication check as a workaround. |
| 2.2.0 | HomeKit pin → a917a713, mainly reduced log verbosity. Replaces configuration map with array to reduce memory use; enables ESP8266 encoder support. |
| 2.2.1 | Fixes optional-light service handling and lowers log verbosity. Encoder remains compiled in. |
| 2.2.2 | Disables ESP8266 encoder; adds JSON bounds checks and reduces unnecessary lock-state updates. These are generally protective changes. |
| 2.2.3 | Authentication/provisioning SSID fixes; adds opener firmware query. No direct radio-setting change found. |
| 2.2.4 | GPIO/status and web asset changes; no direct radio-setting change found. |

The verification-duration comment in the library contrasts 35 ms with verification
skipped versus 794 ms with verification enabled. This is upstream commentary, not
a measurement of this device. The captured failures happen after successful
verification, so that change does not directly explain the failed accessory-list
write. Additional reconnect work could amplify an already unhealthy connection.

References: [firmware comparison](https://github.com/ratgdo/homekit-ratgdo/compare/v2.1.1...v2.2.4),
[HomeKit pin comparison](https://github.com/dkerr64/Arduino-HomeKit-ESP8266/compare/ec2c3b13d9c25c86c8f753f369b24dbc9422df48...a917a7137b7366511ecfad788f00a8540257f752),
[restored verification](https://github.com/dkerr64/Arduino-HomeKit-ESP8266/commit/59a362ce0e65383af1c114bc7f3489f0f5aaf7d8),
[mDNS status addition](https://github.com/ratgdo/homekit-ratgdo/commit/a52feb0acfab5290eddd15717d0f18705d424b74),
[mDNS rate limiting](https://github.com/ratgdo/homekit-ratgdo/commit/a47d92cd646cbb43efa58481ebdb393086c8ada1).

## Measured failure and targeted local change

The user reports MTR loss of 62.7% across 1,159 probes, mean 140.3 ms and worst
about 1,447 ms. Lights are off; other devices are not exhibiting the problem.
This does not establish the TCP loss rate or distinguish localized RF, mesh path,
firmware scheduling and network-stack behavior.

The previously installed 2.2.4-local2 captured an encrypted accessory-list response accepting
421/536 bytes in 1,121 ms while TCP remained ESTABLISHED. Send capacity recovered
from 421 to 1,072 bytes before the library closed the connection. Free heap was
21,000 bytes, largest block 17,136, fragmentation 18%. Separately, the hub sometimes
requests an unknown pairing identity; successful verification occurs on other
connections. The send fix does not change pairing storage or acceptance rules.

The local3 change retains the exact bytes and resumes the unsent suffix. It uses
at most eight write calls per buffer and checks a 2-second retry budget between
calls (or the original socket timeout when longer, preserving initial-pairing
allowance). Core calls can overrun the budget internally; this is not a hard
real-time deadline. Writes and ACK waits yield to the network.

Because the synchronous Arduino client borrows source memory, a full accepted
count alone is insufficient. The core's write implementation ignores the result
of its own ACK wait. We explicitly check flush completion with TCP still
ESTABLISHED before letting serialization reuse the buffer. On failure, abort()
releases outstanding borrowed data; stop() can leave it queued after a failed
flush. This prevents a stalled connection from retaining reused ciphertext.

One recovery message per connection documents successful retries; an unrecovered
buffer generates one failure record, and subsequent writes on that failed context
are suppressed. No additional buffers or flash logging are introduced. Host tests
exercise the production send functions with deferred reads of borrowed buffers,
partial/zero writes, peer close, ACK failure, clock wrap and encryption-call/nonce
continuity. They do not establish real RF performance.

The local3 send change itself did not change radio settings or lwIP retries.
It is now deployed as part of local4, which additionally restores standard TCP
retries and aborts HomeKit sockets on teardown. The original two-retry setting
addressed vanished-peer heap retention; see [the rationale](../lib/lwip2/README.md).
Local4 passed USB installation and an unrestricted gzip OTA reflash. The user
reports markedly reduced packet loss. The firmware, reboot and USB/power context
changed, so attribution remains uncertain. See [current results](ota-transfer-investigation.md).
