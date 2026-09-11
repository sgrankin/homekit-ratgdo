# ESP8266 stability review — 2026-09-11

Reviewed checkout `ea987e3` (v2.2.4 source), the ten tags v2.1.2–v2.2.4, and the pinned Arduino-HomeKit-ESP8266 dependency `a917a7137b7366511ecfad788f00a8540257f752`. No firmware, settings, pairing, or door operations were changed. This is a source review with limited read-only network observations, not a diagnosis from a captured device crash.

## Follow-up: supplied crash identifies post-OTA mDNS failure

The user subsequently provided the EEPROM crash and logs from September 9, running v2.2.1. This supersedes the initial prioritization of the queue leak as a possible explanation for this particular crash. They report previous versions commonly needed two or three OTA attempts with intervening automatic reboots; v2.2.1 now fails consistently.

The shipped `docs/firmware/homekit-ratgdo-v2.2.1.elf` contains symbols (no useful source line mapping was returned). Decoding the fault PC and relevant candidate return addresses gives:

| Address | Symbol |
| --- | --- |
| `0x4023b104` — fault PC | `MDNSResponder::_sendMDNSMessage_Multicast(...) + 0x3c` |
| `0x4023b1a1` | `MDNSResponder::_sendMDNSMessage(...) + 0x4d` |
| `0x40236ec9` | `MDNSResponder::_announce(bool, bool) + 0x55` |
| `0x4022cfc4` | `add_dynamic_mdns() + 0x31c` |
| `0x40232240` | `web_loop() + 0x64` |
| `0x402269fe` | `loop() + 0x1e2` |

Stack words are not a formal unwind, but this coherent call sequence and the exact fault-PC symbol strongly identify the failing path. `excvaddr=0` is consistent with its null UDP-context access.

The source explains it:

1. Firmware upload shuts down HomeKit through `arduino_homekit_close()`.
2. That function calls **`MDNS.close()` on the shared global responder**.
3. Arduino core 3.1.2 closes the UDP context and sets `m_pUDPContext=0`. Its `_resetProbeStatus(false)` leaves the host probe state as `ProbingStatus_Done`.
4. The application keeps running `web_loop()` after an aborted upload. Its periodic/dynamic announcement calls are not guarded by the OTA shutdown state.
5. `add_dynamic_mdns()` invokes `MDNS.announce()`. The core permits announcement in the `Done` state and enters `_sendMDNSMessage_Multicast`, which accesses the null UDP context.

Core 3.1.2 was identified in the release ELF's embedded version string; matching local framework source was inspected. This is a strong explanation for the **delayed crash after upload abort**, and the same unguarded application lifecycle remains in the reviewed v2.2.4 source. The periodic announce was added in commit `69580a2` on November 17, 2025; OTA HomeKit shutdown predates it. That timing fits failure/reboot reports across several versions before maintenance mode.

Exact supplied-log chronology:

- **184:02:52.236**: upload starts; 795,824-byte v2.2.3 image, 1,294,336 bytes available.
- **184:02:52.267**: updater reports 00% after initialization and MD5 setup.
- **184:03:30.601**: upload aborted, 38.365 seconds after start.
- **184:04:14.906**: recorded exception, **44.305 seconds after abort**, 82.670 seconds after start.

Thus flash capacity is ruled out for this attempt. The mDNS crash is downstream of the transfer abort; it does **not** explain why the initial upload stalled. The HTTP parser emits the abort callback when the upload stream cannot continue, including connection loss. No flash-write or MD5-mismatch error appears in this excerpt. Sampled free heap (19,568 bytes; minimum 14,928) does not demonstrate OOM; sampling cannot exclude short transients or fragmentation, especially because the service loop is suspended during OTA.

After the user closed browser tabs, a read-only `/status.json` request succeeded. It confirmed Security+ 2.0, no AP lock, DHCP, one HomeKit client, current heap 19,648 bytes/minimum 14,920, and configured daily reboot. A subsequent `/showlog` GET received about 6.3 KB before its bounded timeout and showed **ongoing short HomeKit socket writes and repeated reconnects from `10.0.90.155`**, matching the supplied historical log. The browser-tab change correlates with improved HTTP access, but does not prove causation or establish the identity of that peer.

Revised fix priority: explicitly track the shared mDNS lifecycle and suppress every advertisement after shutdown; define clean OTA abort recovery; add a regression covering upload start → abort → at least one announcement interval. Separately investigate TCP transfer failure and the reconnecting HomeKit peer. Queue ownership remains a real independent defect, not the diagnosed source of this exception. No retry, flash, restart, or configuration change was performed during this follow-up.

## Device observations

mDNS discovery identified `Garage-Door-A85352.local`, IPv4 `10.0.90.4`, model `ratgdo_v2.5`, firmware **2.2.1**, build July 22, 2026. Its advertised snapshot reported closed, paired, RSSI −52 dBm, channel 8, and uptime 694,086 ms. The snapshot's own timestamp was 08:27:18 EDT; discovery occurred several minutes later. Do not interpret cached advertisements as proof of current health or a current reboot rate.

Several bounded HTTP GET attempts to `/status.json` timed out, including outside the filesystem/network sandbox. No crash log, reset reason, live heap measurement, or OTA error was obtained. Network reachability or host filtering can also cause these timeouts. The user reports OTA stalls at **00%**.

The ongoing mDNS lookup subsequently received an update at 08:35:19 with uptime 1,174,132 ms and RSSI −55 dBm. The uptime progression is consistent with no reboot between the two snapshots, despite HTTP being unreachable from this host. This argues against treating every failed web connection as proof that the radio or whole firmware is down.

## Release history

Maintenance mode began with v2.2.0, July 12, 2026. Release binary sizes below are the actual files committed in `docs/firmware`, not estimates from source size.

| Version | Binary bytes | Relevant change |
| --- | ---: | --- |
| 2.1.2 | 785,888 | OTA oversize handling avoids shutting services down before a known size failure |
| 2.1.3 | 789,536 | JSON escaping; expanded status over mDNS |
| 2.1.4 | 790,416 | IP-change notification and configurable NTP server |
| 2.1.5 | 792,736 | Command/debounce changes; SSE free-slot fix |
| 2.1.6 | 795,904 | HomeKit library 1.5.1, including verification change and accept-race fix |
| 2.2.0 | 796,320 | Maintenance mode; configuration map replaced with array; encoder compiled in; HomeKit 1.5.2 |
| 2.2.1 | 796,528 | Light-service list rebuilt; encoder timing fix; less logging |
| 2.2.2 | 793,312 | Encoder excluded on ESP8266; JSON bounds checks |
| 2.2.3 | 795,824 | Authentication and SSID-related fixes |
| 2.2.4 | 796,688 | GPIO status and web asset cache changes |

There is no dramatic binary-size jump: v2.2.4 is only **160 bytes** larger than v2.2.1. This does not establish available OTA space on the device, but argues against recent binary growth as the default explanation.

`src/wifi_8266.cpp` changes only by adding IP-change notification between v2.1.1 and v2.2.4. The radio/reconnect setup is substantially older. ESP8266 Arduino/platform versions are unpinned, however, and software serial follows a branch, so unchanged project source does not guarantee identical dependency builds.

The v2.2.0→v2.2.1 application diff is small: HomeKit service-list rebuilding, encoder timing, and log levels. The HomeKit library SHA and web upload code are unchanged across those two releases. There is no source evidence establishing a general deterioration in every recent release.

Independent reports corroborate symptoms, not causes:

- [Issue 351](https://github.com/ratgdo/homekit-ratgdo/issues/351): v2.2.1 unstable in both HomeKit and web UI, and unable to upload a downgrade; reporter found v2.2.0 stable.
- [Issue 348](https://github.com/ratgdo/homekit-ratgdo/issues/348): problems after v2.1.6.
- [Issue 326](https://github.com/ratgdo/homekit-ratgdo/issues/326): OTA stalls at 0%, predating maintenance mode.

## Highest-priority source findings

### 1. HomeKit event ownership leaks — concrete defect, device impact unmeasured

In the [pinned HomeKit server](https://github.com/dkerr64/Arduino-HomeKit-ESP8266/blob/a917a7137b7366511ecfad788f00a8540257f752/src/arduino_homekit_server.cpp):

- Line 42 sets the event queue capacity to four.
- Lines 192–194 initialize it as `LIFO, true`: overwrite allowed.
- Lines 694–703 allocate a new event, copy its value, and push its pointer.
- The actual `cQueue.c` implementation overwrites an existing pointer when full, with no destructor or free of the displaced event.
- Lines 212–216 clear and destroy the queue on disconnect without freeing queued events; `q_clean` aliases `q_flush`, which only resets indices/count.

Thus bursts exceeding four pending notifications leak event allocations. Disconnects with pending events leak too. Allocation results are also dereferenced without null checks in notification, JSON/TLV, and other HomeKit paths. These are credible routes from memory pressure to crashes. The project separately reboots when sampled free heap drops below 4 KiB (`src/ratgdo.cpp:440`), so memory loss can also look like Wi-Fi disconnection without a hard exception.

These ownership defects predate the reviewed release window. They may become more visible with different clients, reconnection patterns, or event volume; that has not been demonstrated on this device.

Local reproduction compiled the dependency's **actual `cQueue.c`** with the exact four-slot LIFO/overwrite policy under AddressSanitizer and UndefinedBehaviorSanitizer. Five allocated event pointers produced four retrievable pointers and one orphan. This verifies the queue ownership failure, not whole-firmware behavior. The harness explicitly cleaned up the orphan afterward.

Another consequence worth testing: notification processing drains the LIFO queue and overwrites the previously collected value for the same characteristic. That can select an older value over the newest one. A bounded queue that coalesces by characteristic before allocation would address both pressure and ordering.

### 2. OTA failure leaves services stopped — confirmed behavior

`src/web.cpp:1888` suspends the service loop, stops GDO communications, and closes HomeKit **before** `Update.begin()`. Error and abort paths do not restore these services. The UI explicitly says reboot is required to restore HomeKit. An unsuccessful upload can therefore create persistent “No Response” even if the radio remains connected.

At upload start the handler reports 00%; subsequent progress reports are spaced by five percent, about 40 KB for this firmware. A 00% stall can mean failure before or during that first interval. It does not identify flash capacity, a download failure, or zero bytes received by itself.

GitHub download and MD5 calculation happen in the **browser**, followed by a LAN POST to the controller (`src/www/functions.js:1024`). Separate browser download, metadata POST, firmware POST, flash writes, and finalization in future diagnostics. The updater's `helperUpdateUnderway` parser also assumes closing quotes exist and dereferences unchecked `strchr` results. Reject malformed metadata locally before changing service state.

Recommended fix: explicit update state transitions, validation before shutdown, clear stage/error reporting, and a defined recovery path after failure. Simply calling initialization again is unsafe without checking what HomeKit initialization mutates.

### 3. JSON and web/network memory pressure

The installed v2.2.1 JSON builder writes through unchecked pointers into fixed buffers. A warning after serialization cannot prevent overflow. v2.2.2 adds bounds checks, which is a reason to prefer targeted fixes over assuming older firmware is universally safer. The device's actual status response could not be collected, so overflow is a risk rather than a demonstrated cause.

Eight SSE clients plus up to eight HomeKit clients are ambitious for this memory budget. Browser tabs hold sockets and timers; streaming logs adds traffic. The custom lwIP library limits TCP retries to two (`lib/lwip2/README.md`), a longstanding choice that can make transient packet loss more visible. Evaluate changes with controlled loss tests before replacing it: longer retry retention also costs memory.

## What to remove

Prioritize runtime allocations and connections over deleting source files.

1. Exclude encoder code on the installed v2.2.1 baseline; v2.2.2 onward already does this. The observed binary reduction across that release is not an isolated encoder-size measurement.
2. Reduce web SSE fan-out, or offer simple polling for a single diagnostics client. Preserve a compact status page, local firmware upload, and bounded crash/reboot logs.
3. Remove dynamic door-state mDNS TXT updates if unused, retaining HomeKit discovery records.
4. Make syslog, serial provisioning/CLI extras, and unused protocol implementations compile-time optional. Confirm the actual opener protocol before selecting only one; the live status needed to verify it was unavailable.
5. Trim browser assets if flash footprint matters, but these are embedded flash content, not equivalent-sized permanent RAM allocations.

Laser, vehicle-distance, and other DISCO features are already guarded out of the normal ESP8266 build. Removing those files will primarily reduce maintenance clutter. Gateway periodic ping and crash-injection features are also already disabled in the default build. Do not remove obstruction handling, required protocol state, or rolling-code persistence.

## Local testing assessment

The advertised infrastructure substantially overstates what is tested:

- Native performance tests return constant heap=50,000 and fragmentation=5; no real firmware allocation pressure is measured.
- Integration tests assign mock states and assert those assignments. HomeKit notification mocks are empty.
- Hardware tests implement a separate mock door instead of exercising the production controller.
- Web tests install canned responses as the requests module, with API names that differ from the firmware. All 16 passed locally in effectively zero time, with no device/network calls.
- `run_tests.sh` uses `set -e` and `((TESTS_PASSED++))` starting from zero. A small shell reproduction confirmed exit status 1 at the first increment.
- `src/test_millis.h` is not included by production code. Its intended wrapper calls back into `millis()` rather than preserving an original implementation, so it is not a working rollover harness as written.
- PlatformIO was not installed in the active shell; no full firmware build or native PlatformIO suite was run.

A useful next step is a host test target compiling actual production modules with injected clock, transport, flash, and allocation interfaces. Test queue bursts/disconnects/OOM first; then bounded JSON with long and escaped input, OTA interruption and recovery at every stage, protocol replay with dropped/duplicate packets, timer rollover, and obstruction/state invariants. ASan/UBSan and allocation failure injection are useful here.

This can greatly reduce physical flashes. It cannot certify ESP8266 RF behavior, real SDK timing, power supply quality, or flash-write behavior. A spare disconnected board can provide the final radio/OTA soak without touching the garage. [Wokwi's supported hardware list](https://docs.wokwi.com/getting-started/supported-hardware) includes ESP32 families but not ESP8266, so it is not a drop-in emulator for this firmware.

## Matter and Thread

Matter is an application protocol; Thread is an IPv6 mesh transport. Matter can also use Wi-Fi. Changing HomeKit to Matter over Wi-Fi does not itself repair radio loss, power problems, heap corruption, or door-protocol errors.

The [official Espressif Matter SDK](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/introduction.html) targets ESP32-family chips, not ESP8266. Thread requires an 802.15.4 radio absent from this controller; supported examples include ESP32-C6 and ESP32-H2. Therefore a practical native Matter/Thread migration is a hardware migration and a firmware port, not a runtime swap on the existing ESP8266. An external bridge is another architecture, but retains the existing device's Wi-Fi dependency.

[Matter 1.5 added garage-door closures](https://csa-iot.org/newsroom/matter-1-5-introduces-cameras-closures-and-enhanced-energy-management-capabilities/). Standard support alone does not establish correct exposure in the user's Apple Home version; verify that before a port. There is no evidence from this review that Matter would be inherently more stable. For reliability, fix the measured ownership/recovery defects first; if replacing hardware, evaluate the additional ESP32 memory and maintained firmware independently of choosing HomeKit versus Matter.
