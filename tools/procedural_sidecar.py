#!/usr/bin/env python3
"""
Procedural generation sidecar (v3 stub).

Reads request.json, writes response.json. Extend this script to call an LLM,
generate custom masks, or post-process prompts.

Usage (called by RME):
  python procedural_sidecar.py <request.json> <response.json>
"""

import json
import sys
from pathlib import Path


PRESET_KEYWORDS = {
    "cave": ["caverna", "cave", "dungeon", "masmorra", "gruta"],
    "city": ["cidade", "city", "town", "vila"],
    "forest": ["floresta", "forest", "jungle", "selva"],
    "desert": ["deserto", "desert", "areia"],
    "coast": ["costa", "coast", "beach", "praia"],
    "mountain": ["montanha", "mountain", "peak", "pico"],
    "ice": ["gelo", "ice", "neve", "snow", "winter", "tundra"],
}


def detect_preset(prompt: str) -> str:
    lower = prompt.lower()
    for preset, words in PRESET_KEYWORDS.items():
        if any(w in lower for w in words):
            return preset
    return "forest"


def main() -> int:
    if len(sys.argv) < 3:
        print("Usage: procedural_sidecar.py <request.json> <response.json>", file=sys.stderr)
        return 1

    request_path = Path(sys.argv[1])
    response_path = Path(sys.argv[2])

    with request_path.open(encoding="utf-8") as f:
        request = json.load(f)

    prompt = request.get("prompt", "")
    preset = request.get("preset", "auto")
    if preset == "auto" and prompt:
        preset = detect_preset(prompt)

    response = {
        "ok": True,
        "preset": preset,
        "depthLevels": 0,
        "notes": "Sidecar stub — override preset from prompt keywords. Plug LLM here.",
    }

    lower = prompt.lower()
    if preset == "cave" and any(w in lower for w in ("profunda", "deep", "depth", "multi")):
        response["depthLevels"] = max(2, int(request.get("elevation", 0)) or 3)

    if "montanha" in lower or "mountain" in lower:
        response["elevation"] = max(3, int(request.get("elevation", 0)) or 4)

    with response_path.open("w", encoding="utf-8") as f:
        json.dump(response, f, indent=2)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
