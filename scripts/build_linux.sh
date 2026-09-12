#!/bin/sh
# Run inside an isolated Linux container, /source read-only, /artifacts writable.
set -eu
python3 -m pip install --quiet platformio==6.1.19
mkdir -p /work
# Include checked-out submodule contents, but never modify the real jj/git store.
tar -C /source --exclude=.git --exclude=.jj --exclude=.pio --exclude=.cache -cf - . | tar -C /work -xf -
cd /work
git init -q
git config user.name 'Local build'
git config user.email build@localhost
git add src/www
git commit -qm 'Web content snapshot for asset cache keys'
pio run -e ratgdo_esp8266_hV25 -j 2 -v > /artifacts/build.log 2>&1 || {
    tail -80 /artifacts/build.log
    exit 1
}
sha256sum src/ota_session.h src/door_alerts.h src/sec2_rx.h src/comms.cpp src/homekit.cpp src/homekit_decl.c src/web.cpp platformio.ini patches/homekit/event-ownership.patch > /artifacts/source.sha256
cp .pio/build/ratgdo_esp8266_hV25/firmware.bin .pio/build/ratgdo_esp8266_hV25/firmware.elf /artifacts/
cd /artifacts
md5sum firmware.bin > firmware.md5
sha256sum firmware.bin > firmware.sha256
tail -18 build.log
