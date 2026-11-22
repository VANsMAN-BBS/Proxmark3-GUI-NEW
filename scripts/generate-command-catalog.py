#!/usr/bin/env python3
"""Regenerate resources/commands/commands.json from the bundled client.

The Proxmark3 client can dump every interactive command without hardware:

    proxmark3.exe -t --incognito

This helper parses that table (or a saved dump) and writes the JSON resource
used by the RawCommand command browser.  It intentionally does not invent
commands or descriptions: every row comes from the shipped client metadata.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROW_RE = re.compile(r"^(\S.*?)\s*\|([YN])\s*\|(.*)$")


def category_for(command: str) -> str:
    if command.startswith("hf 14a "):
        return "hf14a"
    if re.match(r"^hf (mf|mfp|mfdes|mfu|ntag424)\b", command):
        return "hfmf"
    if command.startswith("hf 15 "):
        return "hf15"
    if command.startswith("hf "):
        return "hf"
    if command.startswith("lf em "):
        return "lfem"
    if command.startswith("lf "):
        return "lf"
    return "other"


def stable_id(command: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", command.lower()).strip("_")
    return "cmd." + slug


def parse_dump(text: str):
    rows = []
    seen = set()
    for line in text.splitlines():
        match = ROW_RE.match(line)
        if not match:
            continue
        command = " ".join(match.group(1).split())
        if not command or command in seen:
            continue
        seen.add(command)
        rows.append(
            {
                "id": stable_id(command),
                "command": command,
                "category": category_for(command),
                "description": match.group(3).strip(),
                "offline": match.group(2) == "Y",
            }
        )
    return rows


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--client",
        default=str(root.parent / "pm3-256-iceman-v4.23346" / "proxmark3.exe"),
        help="path to the shipped proxmark3.exe",
    )
    parser.add_argument("--dump", help="read a previously saved -t dump instead")
    parser.add_argument(
        "--output",
        default=str(root / "resources" / "commands" / "commands.json"),
        help="JSON output path",
    )
    args = parser.parse_args()

    if args.dump:
        text = Path(args.dump).read_text(encoding="utf-8", errors="replace")
        source = f"saved dump: {args.dump}"
    else:
        client = Path(args.client)
        if not client.exists():
            print(f"client not found: {client}", file=sys.stderr)
            return 2
        proc = subprocess.run(
            [str(client), "-t", "--incognito"],
            cwd=str(client.parent),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        text = proc.stdout.decode("utf-8", errors="replace")
        source = f"{client.name} -t --incognito"

    rows = parse_dump(text)
    if not rows:
        print("no command rows found", file=sys.stderr)
        return 1

    ids = [row["id"] for row in rows]
    if len(ids) != len(set(ids)):
        print("stable id collision", file=sys.stderr)
        return 1

    payload = {
        "source": {
            "client": "Iceman v4.23346",
            "metadata": "proxmark3 -t --incognito",
            "dump": source,
        },
        "categories": ["all", "hf", "lf", "hf14a", "hfmf", "hf15", "lfem", "other"],
        "count": len(rows),
        "commands": rows,
    }

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    counts = {}
    for row in rows:
        counts[row["category"]] = counts.get(row["category"], 0) + 1
    print(f"wrote {output} ({len(rows)} commands)")
    print("categories: " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
