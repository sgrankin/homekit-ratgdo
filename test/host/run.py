#!/usr/bin/env python3
"""Compile real firmware paths against host fakes. Never contacts a device."""
import argparse
import os
from pathlib import Path
import platform
import runpy
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature + "\n{")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def run(command):
    subprocess.run([str(x) for x in command], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--homekit-source", type=Path, help="Use an existing pinned HomeKit checkout")
    args = parser.parse_args()
    compiler = shlex.split(os.environ.get("CXX", "clang++"))
    flags = ["-std=c++11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    if platform.system() == "Darwin":
        # Keep Apple's linker and SDK paired with the selected Xcode toolchain.
        flags += ["-isysroot", os.environ.get("SDKROOT") or subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip(),
                  "--ld-path=" + subprocess.check_output(["xcrun", "--find", "ld"], text=True).strip()]
    with tempfile.TemporaryDirectory(prefix="ratgdo-host-") as tmp:
        tmp = Path(tmp)
        source = (ROOT / "src/web.cpp").read_text()
        handlers = function(source, "void announce_mdns()")
        handlers += "\n" + source[source.index("#ifdef ESP8266\nvoid check_upload_timeout()"):]
        (tmp / "ota_handlers.inc").write_text(handlers)
        run(compiler + flags + ["-I", ROOT / "src", "-I", tmp, ROOT / "test/host/ota.cpp", "-o", tmp / "ota"])
        run([tmp / "ota"])
        homekit = prepare_homekit(args.homekit_source)
        test_patcher(homekit, tmp / "patch-check")
        build_homekit(compiler, flags, homekit, tmp)


def prepare_homekit(source):
    patcher = runpy.run_path(str(ROOT / "scripts/patch_homekit.py"))
    manifest = patcher["MANIFEST"]
    if source is None:
        source = ROOT / ".cache/host-homekit"
        if not source.exists():
            source.mkdir(parents=True)
            run(["git", "init", "-q", source])
            run(["git", "-C", source, "fetch", "-q", "--depth=1", manifest["upstream"], manifest["revision"]])
            run(["git", "-C", source, "checkout", "-q", "--detach", "FETCH_HEAD"])
    source = source.resolve()
    actual = subprocess.check_output(["git", "-C", source, "rev-parse", "HEAD"], text=True).strip()
    if actual != manifest["revision"]:
        raise RuntimeError("HomeKit test checkout does not match pinned revision")
    patcher["apply"](source)
    return source


def test_patcher(source, target):
    patcher = runpy.run_path(str(ROOT / "scripts/patch_homekit.py"))
    manifest = patcher["MANIFEST"]
    for name, spec in manifest["files"].items():
        if spec["before"] is not None:
            dest = target / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(subprocess.check_output(["git", "-C", source, "show", manifest["revision"] + ":" + name]))
    patcher["apply"](target)
    patcher["apply"](target)  # idempotent after a successful application
    modified = target / "src/arduino_homekit_server.cpp"
    modified.write_text(modified.read_text() + "\n// unreviewed upstream change\n")
    try:
        patcher["apply"](target)
    except RuntimeError:
        print("Dependency patch: clean apply, idempotency, drift rejection: passed")
    else:
        raise AssertionError("Dependency patch accepted unexpected source contents")


def build_homekit(compiler, flags, source, tmp):
    code = (source / "src/arduino_homekit_server.cpp").read_text()
    signatures = [
        "void client_notify_characteristic(homekit_characteristic_t *ch, homekit_value_t value,\n\t\tvoid *context)",
        "void homekit_server_process_notifications(homekit_server_t *server)",
        "void homekit_server_process(homekit_server_t *server)",
    ]
    # Upstream functions use K&R braces; normalize only the opening-brace layout.
    for signature in signatures:
        code = code.replace(signature + " {", signature + "\n{")
    (tmp / "homekit_handlers.inc").write_text("\n".join(function(code, sig) for sig in signatures))
    header = (source / "src/arduino_homekit_server.h").read_text()
    start = header.index("typedef struct _client_event {")
    end = header.index("} client_event_t;", start) + len("} client_event_t;")
    (tmp / "homekit_client_event.inc").write_text(header[start:end])
    objects = []
    cflags = ["-std=c11" if flag == "-std=c++11" else flag for flag in flags if not flag.startswith("--ld-path=")]
    for name in ["cQueue.c", "accessories.c", "tlv.c"]:
        obj = tmp / (name + ".o")
        run(compiler + cflags + ["-x", "c", "-D_GNU_SOURCE", "-I", source / "src",
            "-include", ROOT / "test/host/allocator.h", "-c", source / "src" / name, "-o", obj])
        objects.append(obj)
    run(compiler + flags + ["-I", source / "src", "-I", tmp,
        ROOT / "test/host/homekit.cpp", *objects, "-o", tmp / "homekit"])
    run([tmp / "homekit"])


if __name__ == "__main__":
    main()
