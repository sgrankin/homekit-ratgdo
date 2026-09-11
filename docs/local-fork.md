# Local reliability fork

Upstream: `https://github.com/ratgdo/homekit-ratgdo` (`upstream`).
Personal remote: `https://github.com/sgrankin/homekit-ratgdo.git` (`origin`).
The local patch stack is tracked by the **`local` jj bookmark**. No publishing or
physical-device update is part of the build/test workflow.

## Changes

- OTA disables all application mDNS advertisements before HomeKit closes the shared
  responder. This remains disabled after abort or success until reboot.
- A firmware upload with no progress for 30 seconds has its own socket closed from
  a scheduled callback. The timer is not a hardware watchdog. It also stays armed
  if a write fails while the HTTP parser is still receiving the request.
- After abort or updater failure, the updater buffer and pending boot command are
  cleared. Once the parser unwinds, the main loop performs a controlled reboot
  after at least 1.5 seconds from failure. This restores HomeKit/GDO services using
  normal boot initialization and saves the reboot log. Invalid preflight metadata
  or an oversized file does not stop services and does not require a reboot.
- Successful query-parameter uploads preserve the existing explicit-reboot API.
  Legacy successful uploads without query parameters still reboot automatically.
- The pinned HomeKit library has a checked local patch for event ownership,
  allocation failures, notification ordering and safe traversal when clients close.
  See [the dependency patch](../patches/homekit/README.md).
- Default ESP8266 builds identify themselves as **`2.2.4-local1`**, derived from the
  upstream manifest plus `VERSION_TAG`. Increase the local suffix when preparing
  another installed build; the upstream manifest remains untouched.

This fixes the decoded post-abort crash and identified memory defects. It does
not establish why the old firmware's TCP transfers stall, or validate actual RF,
flash or opener timing on hardware. A controller still on 2.2.1 will continue to
use the old updater until a new image has successfully booted.

## Host tests (normal development loop)

```sh
python3 test/host/run.py
```

On this Mac's current beta SDK/linker combination:

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk python3 test/host/run.py
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
ESP8266 emulation. Follow it with a firmware build; use a spare board disconnected
from the opener for real Wi-Fi/OTA soak tests. Protocol replay and obstruction/
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

Validated locally on 2026-09-11: host regression tests passed with ASan/UBSan,
including 1,000 HomeKit burst/disconnect cycles. The ARM Linux build of
`ratgdo_esp8266_hV25` succeeded as `2.2.4-local1`, using 47,072 bytes of static RAM
and 793,691 bytes of flash. The final build reused cached pinned dependencies;
its recorded source hashes match the tested working tree. Firmware SHA-256:
`3162f1af734a8490e414178637ca41c202b800c061e5ecb4915b0c6000ef2c3a`.
No hardware flashing or live OTA validation was performed.

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
