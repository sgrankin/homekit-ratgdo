# Local reliability fork

Upstream: `https://github.com/ratgdo/homekit-ratgdo` (`upstream`).
Personal remote: `https://github.com/sgrankin/homekit-ratgdo.git` (`origin`).
The local patch stack is tracked by the **`local` jj bookmark**. No publishing or
physical-device update is part of the build/test workflow.

## Device status (2026-09-12)

Local4 is installed. USB write and independent digest verification passed, followed
by a successful unrestricted gzip OTA reflash in 39.097 seconds. Post-OTA status
confirmed the door closed, retained pairing/accessory identity, one HomeKit client
and unchanged crash count (1). The user reports substantially reduced packet loss.
Local5 adds two [door alert sensors](door-alerts.md), cooperative Sec+2.0 receive buffering, RAM diagnostics, and bounded status refresh. Its firmware build and host regressions passed. The controller still runs local4: the latest local5 OTA stalled and recovered normally. The verified image is ready for USB; see [deployment evidence](door-alerts.md#missed-status-recovery).
See the [OTA investigation](ota-transfer-investigation.md) for measured results
and remaining uncertainty about the cause of improvement.

## Changes

- OTA disables all application mDNS advertisements before HomeKit closes the shared
  responder. This remains disabled after abort or success until reboot.
- A firmware upload with no progress for 30 seconds has its own socket closed from
  a scheduled callback. The timer is not a hardware watchdog. It also stays armed
  if a write fails while the HTTP parser is still receiving the request. If the
  scheduler cannot allocate its callback, the timer retries every 100 ms.
- After abort or updater failure, the updater buffer and pending boot command are
  cleared. Once the parser unwinds, the main loop performs a controlled reboot
  after at least 1.5 seconds from failure. This restores HomeKit/GDO services using
  normal boot initialization and saves the reboot log. Invalid preflight metadata
  or an oversized file does not stop services and does not require a reboot.
- Successful query-parameter uploads preserve the existing explicit-reboot API.
  Legacy successful uploads without query parameters still reboot automatically.
- HomeKit sends retain and retry unsent bytes, confirm ACK completion before
  buffer reuse, and abort safely when the retry budget is exhausted. See the
  [release and transport review](wifi-release-review.md).
- The pinned HomeKit library has a checked local patch for event ownership,
  allocation failures, notification ordering and safe traversal when clients close.
  See [the dependency patch](../patches/homekit/README.md).
- Standard framework TCP retries replace the custom two-retry archive. The
  low-memory/no-fragmentation configuration is retained. HomeKit teardown aborts
  remaining TCP state to address the original vanished-peer heap-retention concern.
- OTA idle timeout logs parser byte counts, updater progress, TCP state, unread
  data and heap once per stall. There is no new periodic or direct flash logging.
- Default ESP8266 builds now target **`2.2.4-local5`**, derived from the
  upstream manifest plus `VERSION_TAG`. Increase the local suffix when preparing
  another installed build; the upstream manifest remains untouched.

This fixes the decoded post-abort crash and identified memory defects. It does
not establish why the old firmware's TCP transfers stalled or isolate the cause
of improved packet loss. Successful installation and one OTA transfer are not a
long-duration RF or opener-timing test.

## Host tests (normal development loop)

```sh
python3 test/host/run.py
```

On this Mac's current beta SDK/linker combination:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk \
CXX=/Library/Developer/CommandLineTools/usr/bin/clang++ python3 test/host/run.py
```

The first run fetches one pinned GitHub dependency into `.cache/host-homekit` and
applies the checked patch. Subsequent runs use that local checkout. Nothing
contacts the garage controller. To use an existing checkout, pass
`--homekit-source /path/to/Arduino-HomeKit-ESP8266` (must be at the pinned SHA).

Tests compile production OTA handlers, the mDNS guard, and the patched HomeKit
notification/traversal functions with host substitutes for hardware. They also
compile the actual HomeKit value/queue C sources. ASan/UBSan and tracked allocation
failure injection exercise bursts, disconnect cleanup, newest-value delivery,
queue overflow, partial-copy cleanup, and parser/reboot lifecycle edge cases.
The patch application itself is tested for clean application, idempotence and
rejection of unexpected sources. These tests do not run the old canned mock suite.

This is the recommended first testing layer, rather than attempting whole-chip
ESP8266 emulation. Follow it with a firmware build. No spare board is available;
any live flash or disruptive test needs explicit authorization and a closed, idle
door. Read-only observation can continue during ordinary use. Protocol replay and obstruction/
command state-machine tests are useful next additions; they are not yet included.

## Firmware build

Initialize the pinned protocol submodule once:

```sh
git submodule update --init lib/secplus
```

On a compatible host:

```sh
pio run -e ratgdo_esp8266_hV25 -j 2
```

The installed macOS ESP8266 compiler is Intel-only and cannot run on this machine.
A native ARM Linux container works with the official toolchain. With Apple's
installed `container` command, from the repository root:

```sh
mkdir -p .cache/firmware
container system start
container run --rm --cpus 2 --memory 2g \
  --mount "type=bind,source=$PWD,target=/source,readonly" \
  --mount "type=bind,source=$PWD/.cache/firmware,target=/artifacts" \
  docker.io/library/python:3.11-bookworm sh /source/scripts/build_linux.sh
container system stop
```

Only stop the container service if you started it for this build and nothing else
uses it. The script copies source into temporary container storage; it never
modifies the real jj/git store. Outputs are `.cache/firmware/firmware.bin`, its ELF,
checksums and build log. The GitHub workflow `host-regression.yml` also defines
host-regression and ESP8266 build jobs. Defining that workflow is not evidence of
an actual GitHub Actions run; local validation is separate.

Validated locally on 2026-09-12: host regressions passed with ASan/UBSan,
including 1,000 HomeKit burst/disconnect cycles, 1,000 teardown cycles, bounded
write/ACK handling and one-time OTA timeout diagnostics. The ARM Linux build of
`ratgdo_esp8266_hV25` succeeded as `2.2.4-local4`, using 47,200 bytes of static RAM
and 796,475 bytes of flash (800,624-byte image). The verbose link command confirms
the framework's `lwip2-536` library with `LWIP_FEATURES=0`, without the custom
library search path. Firmware SHA-256:
`7927701987d131ced0a080f51d84ae6930466d6e6b3085ae3519590611de73ad`.
Keep its matching ELF for crash decoding. The original verified pre-local2 backup
remains the recovery copy; the user requested stopping the new pre-local4 backup.
A full backup must not be restored blindly after opener rolling codes advance.

## Updating from upstream with jj

```sh
jj git fetch --remote upstream
jj rebase -b local -d main@upstream
```

Resolve any source conflicts, then rerun host tests and the firmware build. Keep
the dependency pin and `patches/homekit/manifest.json` synchronized. If upstream
changes touched dependency files, review/rebase the patch; the build intentionally
refuses unknown contents. Do not bypass that check to get a build through.

The patch stack keeps the original investigation, OTA fix, dependency patch and
fork workflow documentation separate. Publishing the bookmark or flashing the
physical controller remains an explicit subsequent action.
