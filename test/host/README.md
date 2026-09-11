# Host regression tests

Run `python3 test/host/run.py` from any directory. Requires Python 3 and Clang
with AddressSanitizer and UndefinedBehaviorSanitizer. This does not build or
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
