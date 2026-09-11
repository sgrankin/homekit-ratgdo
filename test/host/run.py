#!/usr/bin/env python3
"""Compile real firmware paths against host fakes. Never contacts a device."""
import argparse
import os
from pathlib import Path
import platform
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
    parser.parse_args()
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


if __name__ == "__main__":
    main()
