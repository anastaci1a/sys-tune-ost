"""Verify the Makefile's install ZIP before naming it for publication."""

import hashlib
import os
from pathlib import Path
from zipfile import ZipFile


def package_release(version: str, commit: str) -> Path:
    source = Path(f"dist/sys-tune-{version}-{commit}.zip")
    destination = Path(f"dist/sys-tune-ost-{version}.zip")
    payloads = {
        "switch/.overlays/sys-tune-overlay.ovl": Path("overlay/sys-tune-overlay.ovl"),
        "atmosphere/contents/4200000000000000/exefs.nsp": Path("sys-tune/sys-tune.nsp"),
        "atmosphere/contents/4200000000000000/toolbox.json": Path("sys-tune/toolbox.json"),
    }
    boot_flag = "atmosphere/contents/4200000000000000/flags/boot2.flag"

    with ZipFile(source) as archive:
        if archive.testzip() is not None:
            raise ValueError("Install ZIP failed its CRC check")
        files = [entry.filename for entry in archive.infolist() if not entry.is_dir()]
        if len(files) != 4 or set(files) != set(payloads) | {boot_flag}:
            raise ValueError(f"Unexpected install ZIP contents: {files}")
        for name, build_path in payloads.items():
            built = build_path.read_bytes()
            if not built or archive.read(name) != built:
                raise ValueError(f"Empty or mismatched build payload: {name}")
        if archive.read(boot_flag) != b"":
            raise ValueError("boot2.flag must be empty")

    if destination.exists():
        raise FileExistsError(destination)
    source.rename(destination)
    print(f"{hashlib.sha256(destination.read_bytes()).hexdigest()}  {destination}")
    return destination


if __name__ == "__main__":
    package_release(os.environ["BUILD_VERSION"], os.environ["BUILD_COMMIT"])
