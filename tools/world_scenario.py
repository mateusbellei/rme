"""Compile original normalized geography profiles into editable World planner PNGs.

Uses the standard library only. No reference artwork, DAT or SPR is read.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import zlib

PALETTE = {"beach": (255, 215, 0), "grass": (0, 255, 0),
           "forest": (0, 128, 0), "desert": (232, 192, 96),
           "snow": (240, 248, 255), "mountain": (101, 67, 33)}


def digest(name, data):
    # Git/Windows may translate JSON line endings. Hash the same UTF-8/LF content.
    if str(name).endswith(".json"):
        data = data.decode("utf-8-sig").replace("\r\n", "\n").encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"Expected integer {low}..{high}")
    return value


def number(value, low, high):
    if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f"Expected finite number {low}..{high}")
    return value


def point(value):
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError("A normalized point needs [x,y]")
    return tuple(number(v, 0, 1) for v in value)


def read_profile(path):
    if path.stat().st_size > 1024 * 1024:
        raise ValueError("Scenario profile exceeds 1 MiB")
    profile = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(profile, dict) or type(profile.get("version")) is not int or profile["version"] != 1:
        raise ValueError("Scenario requires version 1")
    anchors = profile.get("settlements")
    if not isinstance(anchors, list) or not 1 <= len(anchors) <= 32:
        raise ValueError("Scenario needs 1..32 settlement centers")
    for anchor in anchors:
        point(anchor)
    zones = profile.get("zones")
    if not isinstance(zones, list) or len(zones) > 128:
        raise ValueError("Scenario needs at most 128 geography zones")
    for zone in [profile.get("background", {})] + zones:
        if not isinstance(zone, dict):
            raise ValueError("Geography zone must be an object")
        integer(zone.get("elevation"), 0, 10000)
        integer(zone.get("noise", 0), 0, 1500)
        if zone.get("biome") is not None and zone["biome"] not in PALETTE:
            raise ValueError("Unknown dry biome")
    for zone in zones:
        number(zone.get("warp", 0), 0, .06)
        shape = zone.get("shape")
        if shape in ("ellipse", "rect"):
            point(zone.get("center"))
            radius = point(zone.get("radius"))
            if not all(radius):
                raise ValueError("Zone radii must be positive")
            number(zone.get("feather", 0), 0, 1)
        elif shape in ("polygon", "path"):
            points = zone.get("points")
            if not isinstance(points, list) or not (3 if shape == "polygon" else 2) <= len(points) <= 64:
                raise ValueError("Invalid bounded zone polyline")
            for p in points:
                point(p)
            if shape == "path":
                number(zone.get("width"), .001, .25)
        else:
            raise ValueError("Zone shape must be ellipse, rect, polygon or path")
    if not isinstance(profile.get("world", {}), dict):
        raise ValueError("Scenario world settings must be an object")
    limits = {"buildings": (0,64), "buildingLevels": (1,4), "caveDepth": (0,8),
              "caveEntrances": (1,16), "caveFill": (20,65), "roadWidth": (1,4),
              "rivers": (0,64), "temperatureBias": (-5000,5000), "moistureBias": (-5000,5000),
              "biomeDetail": (0,100)}
    for key, value in profile.get("world", {}).items():
        if key == "layout":
            if value not in ("blocks", "organic"):
                raise ValueError("Unknown settlement layout")
        elif key in limits:
            integer(value, *limits[key])
        else:
            raise ValueError(f"Unsupported scenario world setting: {key}")
    integer(profile.get("doodadDensity", 8), 0, 100)
    return profile


def hash32(seed, x, y):
    n = (seed ^ (x * 0x9E3779B9) ^ (y * 0x85EBCA6B)) & 0xffffffff
    n = ((n ^ (n >> 16)) * 0x7FEB352D) & 0xffffffff
    n = ((n ^ (n >> 15)) * 0x846CA68B) & 0xffffffff
    return n ^ (n >> 16)


def noise(seed, x, y):
    gx, gy = x // 16, y // 16
    fx, fy = (x % 16) / 16, (y % 16) / 16
    sample = lambda dx, dy: hash32(seed, gx + dx, gy + dy) / 0xffffffff * 2 - 1
    return ((sample(0, 0) * (1 - fx) + sample(1, 0) * fx) * (1 - fy)
            + (sample(0, 1) * (1 - fx) + sample(1, 1) * fx) * fy)


def coverage(zone, x, y):
    shape = zone["shape"]
    if shape in ("ellipse", "rect"):
        cx, cy = zone["center"]
        rx, ry = zone["radius"]
        dx, dy = abs(x - cx) / rx, abs(y - cy) / ry
        distance = math.hypot(dx, dy) if shape == "ellipse" else max(dx, dy)
        feather = zone.get("feather", 0)
        return max(0., min(1., (1 - distance) / feather)) if feather else float(distance <= 1)
    points = zone["points"]
    if shape == "polygon":
        inside = False
        for (ax, ay), (bx, by) in zip(points, points[1:] + points[:1]):
            if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
                inside = not inside
        return float(inside)
    for (ax, ay), (bx, by) in zip(points, points[1:]):
        dx, dy = bx - ax, by - ay
        t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy))) if dx or dy else 0
        if math.hypot(x - ax - t * dx, y - ay - t * dy) <= zone["width"] / 2:
            return 1.
    return 0.


def png(width, height, channels, pixels):
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))
    stride = width * channels
    rows = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, {3: 2, 4: 6}[channels], 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def rasterize(profile, seed, size):
    heights, biomes, land = bytearray(), bytearray(), bytearray()
    warped = any(zone.get("warp", 0) for zone in profile["zones"])
    warp_seed_x, warp_seed_y = hash32(seed, 31, 71), hash32(seed, 47, 89)
    for y in range(size):
        for x in range(size):
            wx = noise(warp_seed_x, x // 4, y // 4) if warped else 0
            wy = noise(warp_seed_y, x // 4, y // 4) if warped else 0
            background = profile["background"]
            value = background["elevation"] + noise(seed, x, y) * background.get("noise", 0)
            biome = background.get("biome")
            for index, zone in enumerate(profile["zones"]):
                alpha = coverage(zone, (x + .5) / size + wx * zone.get("warp", 0), (y + .5) / size + wy * zone.get("warp", 0))
                if not alpha:
                    continue
                target = zone["elevation"]
                if zone.get("noise", 0):
                    target += noise(hash32(seed, index, 47), x, y) * zone["noise"]
                value = value * (1 - alpha) + target * alpha
                if alpha >= .5:
                    biome = zone.get("biome")
            if x == 0 or y == 0 or x == size - 1 or y == size - 1:
                value = 0
            gray = max(0, min(255, int(value * 255 / 10000 + .5)))
            # Match the C++ decoder's 8-bit quantization before classifying water.
            water = (gray * 10000 + 127) // 255 <= 4200
            heights.extend((gray, gray, gray))
            land.extend((0, 0, 0) if water else (255, 255, 255))
            biomes.extend((*PALETTE[biome], 255) if biome and not water else (0, 0, 0, 0))
    return {"height.png": png(size, size, 3, heights),
            "biomes.png": png(size, size, 4, biomes),
            "land.png": png(size, size, 3, land)}


def compile_scenario(profile_path, output, size, seed, legend, catalog, objects=None):
    integer(size, 128, 1024)
    integer(seed, 0, 0xffffffff)
    profile = read_profile(profile_path)
    if output.exists():
        raise ValueError("Scenario output must be a new directory; existing edits are preserved")
    for asset in (legend, catalog, objects):
        if asset and not asset.is_file():
            raise ValueError(f"Missing explicit scenario asset: {asset}")
    anchors = [{"x": int(x * (size - 1) + .5), "y": int(y * (size - 1) + .5)}
               for x, y in profile["settlements"]]
    for index, p in enumerate(anchors):
        if min(p["x"], p["y"]) < 8 or max(p["x"], p["y"]) >= size - 8:
            raise ValueError("Scenario settlement lies too close to the ocean boundary")
        if any(abs(p["x"] - q["x"]) + abs(p["y"] - q["y"]) < max(20, size // 4)
               for q in anchors[:index]):
            raise ValueError("Scenario settlements are too close for the planner")
    def relative(path):
        try:
            return Path(os.path.relpath(path.resolve(), output.resolve())).as_posix()
        except ValueError:  # An explicitly chosen asset on another Windows drive.
            return path.resolve().as_posix()
    world = dict(shape="continent", seaLevel=4200, rivers=0, settlements=len(anchors),
                 buildings=32, buildingLevels=2, caveDepth=0, layout="blocks", roadWidth=3,
                 protectExisting=True, wallBrush="stone", caveSeed=hash32(seed, 1, 1),
                 objectSeed=hash32(seed, 2, 2))
    world.update(profile.get("world", {}))
    # Generated assets and anchor count are authoritative, not arbitrary profile overrides.
    world.update(settlements=len(anchors), seaLevel=4200, settlementAnchors=anchors,
                 heightmap="height.png", biomeHints="biomes.png", landMask="land.png",
                 catalog=relative(catalog))
    if objects:
        world["objectProfile"] = relative(objects)
    recipe = dict(version=4, source="world", pathBase="recipe", seed=seed,
                  region=dict(originX=0, originY=0, width=size, height=size, z=7),
                  legend=relative(legend), world=world, useSelection=False,
                  pipeline=dict(borderize=True, randomize=True, walls=True),
                  doodads=dict(enabled=objects is not None, density=profile.get("doodadDensity", 8)))
    files = rasterize(profile, seed, size)
    files["recipe.json"] = (json.dumps(recipe, indent=2) + "\n").encode()
    manifest = dict(version=1, compiler="world_scenario/v1", seed=seed, size=size,
                    profileSHA256=digest(profile_path, profile_path.read_bytes()),
                    hashNormalization="UTF-8/LF JSON; binary PNG bytes",
                    settlements=anchors, regions=profile.get("regions", []),
                    assets={name: digest(path, path.read_bytes()) for name, path in (("legend", legend), ("catalog", catalog), ("objects", objects)) if path},
                    files={name: digest(name, data) for name, data in files.items()})
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    output.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        (output / name).write_bytes(data)
    return recipe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--size", type=int, default=512)
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--legend", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--objects", type=Path)
    args = parser.parse_args()
    try:
        compile_scenario(args.profile, args.output, args.size, args.seed,
                         args.legend, args.catalog, args.objects)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(2, f"Scenario generation failed: {error}\n")
    print(args.output / "recipe.json")


if __name__ == "__main__":
    main()
