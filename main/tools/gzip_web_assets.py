#!/usr/bin/env python3
"""Create deterministic gzip copies of PiLab firmware web assets."""
from __future__ import annotations

import gzip
import shutil
import sys
from pathlib import Path


def gzip_asset(source: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    target = source.with_name(source.name + ".gz")
    tmp = target.with_suffix(target.suffix + ".tmp")
    with source.open("rb") as src, tmp.open("wb") as raw_out:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw_out, mtime=0, compresslevel=9) as gz_out:
            shutil.copyfileobj(src, gz_out)
    tmp.replace(target)
    original = source.stat().st_size
    compressed = target.stat().st_size
    ratio = 100.0 * compressed / original if original else 0.0
    print(f"{source.name}: {original} -> {compressed} bytes ({ratio:.1f}%)")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: gzip_web_assets.py <asset> [<asset> ...]", file=sys.stderr)
        return 2
    for arg in argv[1:]:
        gzip_asset(Path(arg))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
