# USB installation of the local ESP8266 build

Updated 2026-09-12. These commands use locally installed esptool 5.1.0. This
procedure successfully installed local2 with a verified full backup and confirmed
preservation of flash outside the firmware region. Local4 was installed over USB
on 2026-09-12; write-time and independent firmware digest verification passed.
The user stopped the new backup in favor of the existing verified recovery copy.
The write erased only 0x00000000–0x000c3fff. Post-boot status confirmed local4,
door Closed, retained pairing/accessory identity and unchanged crashCount (1).
A HomeKit ACK timeout occurred at startup. A subsequent unrestricted gzip OTA
reflash succeeded in 39.097 seconds, and the user reported much lower packet loss.
Long-term stability and the cause of improvement remain unproven. See the
[OTA investigation](ota-transfer-investigation.md).

## Physical preparation

Close the door and pause door automations. Photograph the ratgdo wiring, then
power down before disconnecting its opener connections. Preserve the opener's
wall-control and safety-sensor connections. Bring the ratgdo to the Mac and power
it from a USB data cable while it is disconnected from the opener. This lets the
existing board serve as a temporary bench device for first boot and Wi-Fi checks.
Use a direct USB connection initially. Do not install a driver unless enumeration
fails and the USB bridge has been identified.

The instructions below target the previously identified ESP8266 ratgdo v2.5.
If it identifies as another chip or lacks 4 MB flash, stop and reassess the build.
If an optional physical rotary encoder is installed, reassess first: upstream
disabled encoder support on ESP8266 after the original 2.2.1 baseline.

## Tool and image

Run from `/Users/sgrankin/Code/homekit-ratgdo` in one terminal session:

```sh
USBTOOL="$PWD/.cache/usb-tools/bin/esptool"
FIRMWARE="$PWD/.cache/firmware/firmware.bin"
"$USBTOOL" version
"$USBTOOL" --chip esp8266 image-info "$FIRMWARE"
shasum -a 256 "$FIRMWARE"
ls /dev/cu.*
```

The tool is already installed in a repository-local virtual environment. To
recreate it: `python3 -m venv .cache/usb-tools`, then
`.cache/usb-tools/bin/python -m pip install esptool==5.1.0`.

Prepared firmware: `2.2.4-local5`, 803,824-byte file, ESP8266 image with 4 MB/DIO/
40 MHz header. Expected SHA-256:
`1e82822cb76da94dbebd7f73b777a352ca4968e03176180c203950a7406e5ca0`.
The file size includes image overhead and differs from the build's flash-usage
figure. Keep its matching `.cache/firmware/firmware.elf` for crash decoding.

Set `PORT` to the newly appearing USB serial port, not Bluetooth/debug-console:

```sh
PORT=/dev/cu.usbserial-REPLACE_ME
"$USBTOOL" --chip esp8266 --port "$PORT" --baud 115200 --after no-reset flash-id
```

This enters the bootloader and interrupts normal operation. Expect ESP8266 and
4 MB flash. If connection fails, inspect cable/port/driver before trying any write.

## Back up before writing

```sh
umask 077
BACKUP="$PWD/.cache/usb-backups/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP"
"$USBTOOL" --chip esp8266 --port "$PORT" --baud 115200 --after no-reset \
  read-flash 0 ALL "$BACKUP/full-flash.bin"
"$USBTOOL" --chip esp8266 --port "$PORT" --baud 115200 --after no-reset \
  verify-flash 0 "$BACKUP/full-flash.bin"
shasum -a 256 "$BACKUP/full-flash.bin" > "$BACKUP/full-flash.sha256"
```

Proceed only after read and verification succeed; expect a 4,194,304-byte backup.
Keep a second private copy outside the repository cache. This contains Wi-Fi and
HomeKit credentials as well as firmware/settings; do not commit or share it.
`--after no-reset` keeps application firmware from booting between these stages.

## Write firmware only, then verify

```sh
"$USBTOOL" --chip esp8266 --port "$PORT" --baud 115200 --after no-reset \
  write-flash 0x0 "$FIRMWARE"
"$USBTOOL" --chip esp8266 --port "$PORT" --baud 115200 --after no-reset \
  verify-flash 0x0 "$FIRMWARE"
```

Run each command separately and inspect success before continuing. Do not use
`erase-flash`, `--erase-all`, or a filesystem upload. The firmware starts at zero;
the old and new builds share the 4m2m layout. LittleFS begins at `0x200000`,
HomeKit storage at `0x3fb000`, and SDK Wi-Fi storage at `0x3fd000`. Firmware-only
writing should preserve those regions. The settings and HomeKit storage formats
are compatible in the reviewed sources; verify retention after boot.

After successful verification, unplug/replug USB to boot. Give it about a minute,
then check the existing IP/hostname (DHCP may change it), hard-refresh the web UI,
and confirm `2.2.4-local5`, Wi-Fi, retained settings and HomeKit pairing. Opener
state/communications are not meaningful while disconnected. Do not reset pairing
just because Home reports an unavailable door during bench testing.

Once basic boot/network checks pass, power down, restore the photographed wiring,
and check actual door state before a supervised operation. Leave deliberate OTA
interruption tests for a separate session.

## Recovery

If the image fails to boot, USB bootloader access remains the recovery path.
Recheck the write/verification output first. A firmware-only write of the saved
stock image at `0x0` preserves current data. The full dump can restore all flash
on this same board if needed, but is not the default downgrade: it also rewinds
opener rolling codes and other state. Reassess before restoring a dump after the
board has resumed communicating with the opener.

## Background source review

A separate agent reviewed the actual Arduino core/parser and persistence changes.
It found no firmware-only USB blocker and confirmed the storage compatibility
above. It identified an OTA-only allocation-failure gap in timeout scheduling. The
follow-up fix retries scheduling every 100 ms until accepted; new upload progress
replaces the retry deadline and an abort cancels it. Host fault-injection tests
cover repeated failures and eventual socket closure from the scheduled callback.

References: [upstream USB/esptool instructions](https://github.com/ratgdo/homekit-ratgdo#esptool),
[Espressif read/write/verify documentation](https://docs.espressif.com/projects/esptool/en/latest/esp8266/esptool/basic-commands.html),
[ratcloud USB installation guidance](https://ratcloud.llc/pages/firmware).
