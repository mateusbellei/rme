"""Regression for authored geography, portable recipes and non-destructive compilation."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import zlib


def main():
    source, build = (Path(p).resolve() for p in sys.argv[1:])
    spec = importlib.util.spec_from_file_location("world_scenario", source / "tools/world_scenario.py")
    scenario = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(scenario)
    # Bundled scenarios remain verifiable on LF and CRLF checkouts.
    for name in ("coastal_port", "wetland_trade"):
        folder = source / "data/procedural/scenarios" / name
        manifest = json.loads((folder / "manifest.json").read_text())
        recipe = json.loads((folder / "recipe.json").read_text())
        for file, checksum in manifest["files"].items():
            assert scenario.digest(file, (folder / file).read_bytes()) == checksum, (name, file)
        for asset, file in (("legend", recipe["legend"]), ("catalog", recipe["world"]["catalog"]), ("objects", recipe["world"]["objectProfile"])):
            assert scenario.digest(file, (folder / file).read_bytes()) == manifest["assets"][asset], (name, asset)
    assert scenario.digest("test.json", b"{}\r\n") == scenario.digest("test.json", b"{}\n")
    with tempfile.TemporaryDirectory(prefix="scenario-", dir=build) as directory:
        root = Path(directory).resolve()
        assert root.parent == build
        profile = source / "data/procedural/geography/coastal_port.json"
        legend = source / "data/procedural/world_legend.json"
        catalog = source / "data/procedural/profiles/1098/city_buildings.json"
        objects = source / "data/procedural/profiles/1098/objects.json"
        def compile(output, seed=123, file=profile):
            return scenario.compile_scenario(file, root / output, 128, seed, legend, catalog, objects)
        first = compile("first")
        compile("second")
        compile("changed", 456)
        for name in ("height.png", "biomes.png", "land.png", "recipe.json", "manifest.json"):
            assert (root / "first" / name).read_bytes() == (root / "second" / name).read_bytes(), name
        assert (root / "first/height.png").read_bytes() != (root / "changed/height.png").read_bytes()
        assert first["world"]["settlementAnchors"] == [{"x": 75, "y": 65}]
        assert first["world"]["settlements"] == 1 and first["pathBase"] == "recipe"
        assert (root / "first" / first["world"]["catalog"]).resolve() == catalog
        # Decode the categorical mask and verify no antialiased/unknown colors are emitted.
        data = (root / "first/land.png").read_bytes()
        at, compressed = 8, bytearray()
        while at < len(data):
            size = struct.unpack_from(">I", data, at)[0]
            kind, payload = data[at + 4:at + 8], data[at + 8:at + 8 + size]
            if kind == b"IDAT":
                compressed.extend(payload)
            at += size + 12
        raw = zlib.decompress(compressed)
        assert len(raw) == 128 * (128 * 3 + 1)
        assert set(raw) <= {0, 255}
        before = (root / "first/recipe.json").read_bytes()
        try:
            compile("first", 999)
            raise AssertionError("Existing scenario edits were overwritten")
        except ValueError:
            pass
        assert (root / "first/recipe.json").read_bytes() == before
        invalid = root / "invalid.json"
        original = json.loads(profile.read_text())
        for change in ({"version": True}, {"settlements": [[.1,.1],[.11,.1]]},
                       {"zones": [{"shape": "typo", "elevation": 6000}]},
                       {"world": {"buildings": 1000}}, {"doodadDensity": -1}):
            invalid.write_text(json.dumps(dict(original, **change)))
            try:
                compile("invalid-output", file=invalid)
                raise AssertionError(f"Malformed scenario accepted: {change}")
            except ValueError:
                pass
            assert not (root / "invalid-output").exists()
    print("PASS: scenario replay, seed variation, portable assets, categorical PNG and edit preservation")


if __name__ == "__main__":
    main()
