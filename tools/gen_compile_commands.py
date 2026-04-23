#!/usr/bin/env python3
"""Generate compile_commands.json from the Makefile build settings."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--cppflags", default="")
    parser.add_argument("--cflags", default="")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--out", default="compile_commands.json")
    parser.add_argument("sources", nargs="+")
    args = parser.parse_args()

    entries = []
    for src in args.sources:
        src_path = Path(src)
        obj_name = src_path.with_suffix(".o").name
        command = " ".join(
            part for part in [
                args.cc,
                args.cppflags,
                args.cflags,
                "-c",
                "-o",
                str(Path(args.build_dir) / obj_name),
                str(src_path),
            ]
            if part
        )
        entries.append(
            {
                "directory": ".",
                "command": command,
                "file": str(src_path),
            }
        )

    out_path = Path(args.out)
    out_path.write_text(json.dumps(entries, indent=4) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
