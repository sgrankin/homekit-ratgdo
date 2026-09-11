#!/usr/bin/env python3
"""Apply the pinned HomeKit patch, refusing unknown or partially patched sources."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "patches/homekit/event-ownership.patch"
MANIFEST = json.loads((PATCH.parent / "manifest.json").read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


def apply(source):
    source = Path(source).resolve()
    observed = {name: digest(source / name) for name in MANIFEST["files"]}
    if all(observed[name] == spec["after"] for name, spec in MANIFEST["files"].items()):
        return
    if not all(observed[name] == spec["before"] for name, spec in MANIFEST["files"].items()):
        raise RuntimeError("HomeKit sources differ from the pinned patch baseline. "
                           "Review/rebase patches/homekit; do not silently build without the fix.")
    # Patch a temporary copy first. Hash validation prevents fuzzy application.
    with tempfile.TemporaryDirectory(prefix="ratgdo-homekit-patch-") as tmp:
        tmp = Path(tmp)
        for name in MANIFEST["files"]:
            if (source / name).exists():
                (tmp / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source / name, tmp / name)
        subprocess.run(["git", "apply", "--check", str(PATCH)], cwd=tmp, check=True)
        subprocess.run(["git", "apply", str(PATCH)], cwd=tmp, check=True)
        for name, spec in MANIFEST["files"].items():
            if digest(tmp / name) != spec["after"]:
                raise RuntimeError("Unexpected patched contents: " + name)
        for name in MANIFEST["files"]:
            destination = source / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            staging = destination.with_suffix(destination.suffix + ".patching")
            shutil.copy2(tmp / name, staging)
            staging.replace(destination)
    print("Applied checked HomeKit event ownership patch")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    apply(parser.parse_args().source)
