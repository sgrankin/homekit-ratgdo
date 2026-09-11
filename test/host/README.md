# Host regression tests

Format the C++ harness with `clang-format` (the style is scoped to this directory):

```sh
clang-format -i test/host/*.cpp test/host/*.h
clang-format --dry-run --Werror test/host/*.cpp test/host/*.h
```

Run `python3 test/host/run.py` from any directory. Requires Python 3 and Clang
with AddressSanitizer and UndefinedBehaviorSanitizer, and Git. The first run fetches
the pinned HomeKit source into `.cache/host-homekit`; later runs are local. This does not build or
flash the ESP8266 and never contacts a device.

The harness compiles the actual OTA handlers and mDNS announcement guard from
`src/web.cpp` (extracted verbatim into a temporary include), plus the production
`OtaSession` lifecycle. Only the HTTP, flash, timer and reboot interfaces are
faked. It tests preflight rejection, abort at zero/partial progress, write and
finalization failures, metadata, duplicate uploads, deferred reboot across clock
wrap, and an idle upload socket. It also checks that announcements after OTA
shutdown do not use the closed responder, including after success while awaiting
an explicit reboot. This is a functional host harness, not a radio/SDK emulator.

On the current macOS 27 beta installation, the default SDK's stub format is
incompatible with the available linker. Select the installed compatible SDK:

```sh
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk python3 test/host/run.py
```

`SDKROOT` and `CXX` are optional overrides; normal macOS and Linux installations
should use the first command. All generated files and executables are temporary.

The HomeKit suite additionally compiles actual notification and traversal functions
from the patched dependency, its queue/value C implementations and its ownership
helper. It checks 1,000 burst/disconnect cycles, queue overflow, latest-value
coalescing, allocation failure at event/list/deep-copy stages and client removal
during traversal. Allocation accounting checks leaks explicitly, including on
macOS where LeakSanitizer is unavailable. ASan/UBSan check invalid accesses.
The patcher is also tested against clean, already-patched and modified sources.

See [the fork workflow](../../docs/local-fork.md) for firmware builds and hardware
validation limits. No radio, real flash or physical opener is simulated here.

The write-diagnostic suite compiles the production library write function and
checks short/zero writes, state captured before local closure, notification labels,
clock rollover and quiet successful writes. It substitutes transport/heap APIs;
it does not simulate packet delivery.
