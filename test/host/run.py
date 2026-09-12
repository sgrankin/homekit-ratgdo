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
        (tmp / "close_timeout.inc").write_text(function((ROOT / "src/comms.cpp").read_text(), "void close_completion_timeout()"))
        run(compiler + flags + ["-I", ROOT / "src", "-I", tmp, ROOT / "test/host/door_alerts.cpp", "-o", tmp / "door_alerts"])
        run([tmp / "door_alerts"])
        source = (ROOT / "src/web.cpp").read_text()
        handlers = function(source, "void announce_mdns()")
        handlers += "\n" + source[source.index("#ifdef ESP8266\nstatic bool uploadTimeoutLogged"):]
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
    write_signature = "void write(client_context_t *context, byte *data, int data_size)"
    write_code = code.replace(write_signature + " {", write_signature + "\n{")
    encrypted_signature = "int client_send_encrypted_(client_context_t *context,\n\t\tbyte *payload, size_t size)"
    write_code = write_code.replace(encrypted_signature + " {", encrypted_signature + "\n{")
    (tmp / "homekit_write.inc").write_text(function(write_code, write_signature) + "\n" + function(write_code, encrypted_signature))
    run(compiler + flags + ["-I", tmp, ROOT / "test/host/homekit_write.cpp", "-o", tmp / "homekit_write"])
    run([tmp / "homekit_write"])
    teardown = "void client_context_free(client_context_t *c)"
    teardown_code = code.replace(teardown + " {", teardown + "\n{")
    (tmp / "homekit_teardown.inc").write_text(function(teardown_code, teardown))
    run(compiler + flags + ["-I", tmp, ROOT / "test/host/homekit_teardown.cpp", "-o", tmp / "homekit_teardown"])
    run([tmp / "homekit_teardown"])
    firmware_code = (ROOT / "src/homekit.cpp").read_text()
    start = firmware_code.index("    homekit_service_t **services = config.accessories[0]->services;")
    end = firmware_code.index("    services[index] = NULL;", start) + len("    services[index] = NULL;")
    (tmp / "homekit_service_list.inc").write_text(firmware_code[start:end])
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
    declarations = tmp / "homekit_decl.o"
    run(compiler + cflags + ["-x", "c", '-DAUTO_VERSION="test"', "-I", source / "src", "-I", ROOT / "src",
        "-c", ROOT / "src/homekit_decl.c", "-o", declarations])
    run(compiler + flags + ["-I", source / "src", "-I", ROOT / "src", "-I", tmp,
        ROOT / "test/host/homekit_layout.cpp", declarations, *objects, "-o", tmp / "homekit_layout"])
    for mask in range(4):
        run([tmp / "homekit_layout", str(mask)])



if __name__ == "__main__":
    main()
