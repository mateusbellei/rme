#!/usr/bin/env python3
"""Lightweight code hygiene pass adapted from RME-Redux auditor."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "source"

REPLACEMENTS = [
    (re.compile(r"\bNULL\b"), "nullptr"),
]


def process_file(path: Path, dry_run: bool) -> bool:
    text = path.read_text(encoding="utf-8", errors="ignore")
    original = text
    for pattern, repl in REPLACEMENTS:
        text = pattern.sub(repl, text)
    if text != original:
        if not dry_run:
            path.write_text(text, encoding="utf-8")
        print(f"updated: {path.relative_to(ROOT.parent)}")
        return True
    return False


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    changed = 0
    for path in ROOT.rglob("*"):
        if path.suffix in {".cpp", ".h", ".hpp"}:
            if process_file(path, args.dry_run):
                changed += 1
    print(f"done ({changed} files)")


if __name__ == "__main__":
    main()
