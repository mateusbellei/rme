#!/usr/bin/env python3
"""
Procedural generation sidecar (v3.3).

Reads request.json, optionally calls an OpenAI-compatible chat API (vision),
renders mask.png, writes response.json.

Usage (called by RME):
  python procedural_sidecar.py <request.json> <response.json>
"""

from __future__ import annotations

import base64
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

try:
    from PIL import Image
except ImportError:
    Image = None  # type: ignore

PRESET_KEYWORDS = {
    "cave": ["caverna", "cave", "dungeon", "masmorra", "gruta"],
    "city": ["cidade", "city", "town", "vila"],
    "forest": ["floresta", "forest", "jungle", "selva"],
    "desert": ["deserto", "desert", "areia"],
    "coast": ["costa", "coast", "beach", "praia"],
    "mountain": ["montanha", "mountain", "peak", "pico"],
    "ice": ["gelo", "ice", "neve", "snow", "winter", "tundra"],
}

LEGEND_HINT = """
Mask RGB legend (use these exact colors in maskRegions):
  (0,128,0) or (0,255,0) = grass/land
  (0,0,255) = water
  (128,128,128) = stone
  (192,192,192) = cave floor
  (255,215,0) = sand
  (150,75,0) = earth
  (101,67,33) = mountain
  (255,255,255) = snow
  (224,255,255) = ice
  (0,0,0) = void/transparent (skip painting)
"""

SYSTEM_PROMPT = """You assist a Tibia-style map editor procedural generator.
Return ONLY valid JSON (no markdown) with this schema:
{
  "preset": "forest|desert|cave|city|coast|mountain|ice",
  "elevation": 0-8,
  "depthLevels": 0-5,
  "doodadDensity": 0-30,
  "maskRegions": [{"x":0,"y":0,"w":32,"h":32,"r":0,"g":128,"b":0}],
  "maskBase64": "<optional PNG base64 without data-uri prefix>"
}
Use maskRegions to approximate layout on the requested map size when helpful.
Fewer large regions are better than per-pixel detail.
""" + LEGEND_HINT


def detect_preset(prompt: str) -> str:
    lower = prompt.lower()
    for preset, words in PRESET_KEYWORDS.items():
        if any(w in lower for w in words):
            return preset
    return "forest"


def stub_enhance(request: dict[str, Any]) -> dict[str, Any]:
    prompt = request.get("prompt", "")
    preset = request.get("preset", "auto")
    if preset == "auto" and prompt:
        preset = detect_preset(prompt)

    response: dict[str, Any] = {
        "ok": True,
        "preset": preset,
        "depthLevels": 0,
        "notes": "Sidecar stub — keyword preset/elevation. Enable llm in sidecar.json for HTTP.",
    }

    lower = prompt.lower()
    if preset == "cave" and any(w in lower for w in ("profunda", "deep", "depth", "multi")):
        response["depthLevels"] = max(2, int(request.get("elevation", 0)) or 3)

    if "montanha" in lower or "mountain" in lower:
        response["elevation"] = max(3, int(request.get("elevation", 0)) or 4)

    return response


def encode_image_data_url(path: str) -> str | None:
    image_path = Path(path)
    if not image_path.is_file():
        return None
    suffix = image_path.suffix.lower()
    mime = "image/png"
    if suffix in (".jpg", ".jpeg"):
        mime = "image/jpeg"
    elif suffix == ".webp":
        mime = "image/webp"
    raw = image_path.read_bytes()
    encoded = base64.b64encode(raw).decode("ascii")
    return f"data:{mime};base64,{encoded}"


def parse_json_content(content: str) -> dict[str, Any]:
    text = content.strip()
    if text.startswith("```"):
        lines = text.splitlines()
        if lines and lines[0].startswith("```"):
            lines = lines[1:]
        if lines and lines[-1].strip() == "```":
            lines = lines[:-1]
        text = "\n".join(lines).strip()
    parsed = json.loads(text)
    if not isinstance(parsed, dict):
        raise ValueError("LLM response JSON must be an object")
    return parsed


def call_llm(request: dict[str, Any], llm_config: dict[str, Any]) -> dict[str, Any]:
    api_key_env = llm_config.get("api_key_env", "OPENAI_API_KEY")
    api_key = os.environ.get(api_key_env, "").strip()
    if not api_key:
        return {"ok": False, "error": f"Missing API key in environment variable {api_key_env}"}

    base_url = str(llm_config.get("base_url", "https://api.openai.com/v1")).rstrip("/")
    model = llm_config.get("model", "gpt-4o")
    timeout_sec = max(5, int(llm_config.get("timeout_sec", 60)))

    prompt = request.get("prompt", "")
    width = int(request.get("regionWidth", 0) or 0)
    height = int(request.get("regionHeight", 0) or 0)
    user_text = (
        f"User prompt: {prompt}\n"
        f"Current preset hint: {request.get('preset', 'auto')}\n"
        f"Map region size: {width}x{height} tiles\n"
        f"Requested elevation levels: {request.get('elevation', 0)}\n"
        "Return JSON with preset, elevation, depthLevels, doodadDensity, "
        "and optional maskRegions or maskBase64 for a terrain mask."
    )

    content: list[dict[str, Any]] = [{"type": "text", "text": user_text}]
    reference = request.get("referenceImage", "")
    use_vision = bool(llm_config.get("vision", True))
    if use_vision and reference:
        data_url = encode_image_data_url(reference)
        if data_url:
            content.append({"type": "image_url", "image_url": {"url": data_url}})

    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": content if len(content) > 1 else user_text},
        ],
        "response_format": {"type": "json_object"},
        "temperature": 0.4,
    }

    req = urllib.request.Request(
        f"{base_url}/chat/completions",
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=timeout_sec) as resp:
            body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        return {"ok": False, "error": f"LLM HTTP {exc.code}: {detail[:500]}"}
    except urllib.error.URLError as exc:
        return {"ok": False, "error": f"LLM request failed: {exc.reason}"}

    choices = body.get("choices") or []
    if not choices:
        return {"ok": False, "error": "LLM returned no choices"}

    message = choices[0].get("message") or {}
    llm_content = message.get("content", "")
    if not llm_content:
        return {"ok": False, "error": "LLM returned empty content"}

    try:
        parsed = parse_json_content(llm_content)
    except (json.JSONDecodeError, ValueError) as exc:
        return {"ok": False, "error": f"Could not parse LLM JSON: {exc}"}

    parsed["ok"] = True
    parsed["notes"] = f"LLM ({model}) enhanced generation spec"
    return parsed


def render_mask_regions(
    width: int,
    height: int,
    regions: list[dict[str, Any]],
    default_rgb: tuple[int, int, int] = (0, 128, 0),
) -> "Image.Image":
    if Image is None:
        raise RuntimeError("Pillow (PIL) is required to render maskRegions. pip install Pillow")

    img = Image.new("RGB", (width, height), default_rgb)
    pixels = img.load()
    for region in regions:
        x0 = max(0, int(region.get("x", 0)))
        y0 = max(0, int(region.get("y", 0)))
        x1 = min(width, x0 + max(1, int(region.get("w", 1))))
        y1 = min(height, y0 + max(1, int(region.get("h", 1))))
        rgb = (
            max(0, min(255, int(region.get("r", default_rgb[0])))),
            max(0, min(255, int(region.get("g", default_rgb[1])))),
            max(0, min(255, int(region.get("b", default_rgb[2])))),
        )
        if rgb == (0, 0, 0):
            continue
        for y in range(y0, y1):
            for x in range(x0, x1):
                pixels[x, y] = rgb
    return img


def save_mask_from_llm(llm_data: dict[str, Any], request: dict[str, Any]) -> str | None:
    output_dir = request.get("outputDir", "")
    width = int(request.get("regionWidth", 0) or 0)
    height = int(request.get("regionHeight", 0) or 0)
    if not output_dir or width <= 0 or height <= 0:
        return None

    out_path = Path(output_dir) / "rme_pg_mask.png"

    mask_b64 = llm_data.get("maskBase64")
    if mask_b64:
        if Image is None:
            raise RuntimeError("Pillow (PIL) is required for maskBase64. pip install Pillow")
        raw = base64.b64decode(mask_b64)
        img = Image.open(__import__("io").BytesIO(raw)).convert("RGB")
        if img.size != (width, height):
            img = img.resize((width, height), Image.Resampling.LANCZOS)
        img.save(out_path)
        return str(out_path)

    regions = llm_data.get("maskRegions")
    if isinstance(regions, list) and regions:
        img = render_mask_regions(width, height, regions)
        img.save(out_path)
        return str(out_path)

    return None


def merge_response(base: dict[str, Any], llm_data: dict[str, Any]) -> dict[str, Any]:
    merged = dict(base)
    for key in ("preset", "elevation", "depthLevels", "doodadDensity", "legendPath", "notes", "error"):
        if key in llm_data and llm_data[key] is not None:
            merged[key] = llm_data[key]
    if llm_data.get("ok") is False:
        merged["ok"] = False
        merged["error"] = llm_data.get("error", "LLM failed")
    return merged


def main() -> int:
    if len(sys.argv) < 3:
        print("Usage: procedural_sidecar.py <request.json> <response.json>", file=sys.stderr)
        return 1

    request_path = Path(sys.argv[1])
    response_path = Path(sys.argv[2])

    with request_path.open(encoding="utf-8-sig") as f:
        request = json.load(f)

    llm_config = request.get("llm") or {}
    llm_enabled = bool(llm_config.get("enabled"))

    response = stub_enhance(request)

    if llm_enabled:
        llm_result = call_llm(request, llm_config)
        if llm_result.get("ok") is False:
            response = llm_result
        else:
            response = merge_response(response, llm_result)
            try:
                mask_path = save_mask_from_llm(llm_result, request)
                if mask_path:
                    response["maskPath"] = mask_path
            except RuntimeError as exc:
                response["notes"] = str(exc)

    with response_path.open("w", encoding="utf-8") as f:
        json.dump(response, f, indent=2)

    return 0 if response.get("ok", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
