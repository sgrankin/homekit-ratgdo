# HomeKit dependency patch

Upstream `dkerr64/Arduino-HomeKit-ESP8266` still points to `a917a7137b7366511ecfad788f00a8540257f752`
as of this review. The library pin alone cannot supply these fixes.

`event-ownership.patch` is an ordinary patch against that revision. It:

- coalesces pending notifications by characteristic before allocating another event;
- uses a bounded FIFO without pointer overwrite;
- frees pending events and their values on disconnect;
- propagates event/value/client/list allocation failures, requesting a client reconnect;
- transfers values when creating the send list instead of allocating extra deep copies;
- saves the next client before processing can free the current client;
- retries unsent HomeKit bytes within a budget and confirms acknowledgement before
  source-buffer reuse; aborts failed streams to release borrowed storage;
- aborts sockets on teardown so vanished peers do not retain TCP buffers;
- logs a bounded recovery/failure summary; see
  [transport review](../../docs/wifi-release-review.md) and
  [diagnostic fields](../../docs/homekit-write-diagnostics.md).

The PlatformIO pre-script applies it after dependency installation and before
compilation. `manifest.json` records SHA-256 of every changed file before and after
patching. Application is idempotent; unknown or partially patched sources stop the
build. The scripts never edit the global PlatformIO library cache.

When changing the upstream pin, review/rebase the patch and regenerate the hashes.
Do not bypass the check or apply with fuzz. An upstream library fix can replace
this patch after the host regression suite passes against it. To reapply a changed
patch during local development, remove the generated environment-specific
`.pio/libdeps/<environment>/HomeKit-ESP8266` directory and let PlatformIO reinstall
it. Do not delete hand-edited sources elsewhere.
