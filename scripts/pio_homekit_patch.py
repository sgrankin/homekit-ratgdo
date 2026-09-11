# PlatformIO installs lib_deps before running extra_scripts.
from pathlib import Path
import runpy

Import("env")

if env.subst("$PIOPLATFORM") == "espressif8266" and not env.IsCleanTarget():
    patcher = runpy.run_path(str(Path(env.subst("$PROJECT_DIR")) / "scripts/patch_homekit.py"))
    source = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "HomeKit-ESP8266"
    patcher["apply"](source)
