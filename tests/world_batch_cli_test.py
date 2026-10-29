"""Exercise the public 10.98 profile through the installed application entry point."""
import hashlib
import json
import struct
import zlib
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def rgb_png(path, rows):
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))
    height, width = len(rows), len(rows[0])
    scanlines = b"".join(b"\0" + b"".join(bytes(rgb) for rgb in row) for row in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


def flat_biome_png(path, rgb):
    rgb_png(path, [[rgb] * 8] * 8)


def otbm_nodes(data):
    """Yield decoded node payloads and ancestor headers from a writer fixture."""
    assert data[:4] == b"OTBM"
    stack = []
    cursor = 4
    while cursor < len(data):
        value = data[cursor]
        cursor += 1
        if value == 0xfe:
            stack.append(bytearray())
        elif value == 0xff:
            node = bytes(stack.pop())
            yield node, stack
        else:
            if value == 0xfd:
                value = data[cursor]
                cursor += 1
            stack[-1].append(value)
    assert not stack


def main():
    executable, source, build = map(Path, sys.argv[1:])
    source = source.resolve()
    executable = executable.resolve()
    with tempfile.TemporaryDirectory(prefix="rme-world-cli-", dir=build) as temporary:
        output = Path(temporary) / "world"
        command = [str(executable), "--generate-world",
                   str(source / "data/procedural/recipes/example_furnished_city_1098.json"),
                   str(source / "data/1098/items_original1098.otb"),
                   str(source / "data/1098/items_original1098.xml"),
                   str(source / "data/1098/materials.xml"), str(output),
                   "--data-dir", str(source / "data"), "--town-prefix", "Frontier", "--verify",
                   "--house-prefix", "Home", "--house-rent", "250"]
        # Run outside the checkout: only explicit arguments / recipe-relative paths apply.
        generated = subprocess.run(command, cwd=temporary, capture_output=True, text=True,
                                   errors="replace", timeout=60)
        if generated.returncode:
            raise AssertionError(f"CLI returned {generated.returncode}: {generated.stdout} {generated.stderr}")
        report = json.loads((output / "generation.json").read_text(encoding="utf-8"))
        plan = json.loads((output / "plan.json").read_text(encoding="utf-8"))
        assert report["client"]["clientVersion"] == 57
        assert report["tileBudget"] == 1048576
        assert report["seed"] == plan["settings"]["seed"]
        assert report["verifiedOTBM"] is True
        assert report["diagnosticsExported"] is True
        assert report["editableInputs"] == {}
        assert report["townPrefix"] == "Frontier" and len(report["towns"]) == 2
        assert [town["name"] for town in report["towns"]] == ["Frontier 1", "Frontier 2"]
        assert all(town["templePosition"][2] == 7 for town in report["towns"])
        assert report["planFingerprint"] == plan["fingerprint"]
        assert plan["counts"]["plannedTiles"] == report["tiles"]
        assert len(plan["settlements"]) == 2 and len(plan["buildings"]) == 24
        for town in range(2):
            roles = [b["role"] for b in plan["buildings"] if b["settlement"] == town]
            assert roles.count("commercial") <= 1 and roles.count("industrial") <= 2
            assert roles.count("residential") >= 9
        assert {layer["offsetZ"] for layer in plan["layers"]} == {-2, -1, 0, 1, 2}
        assert report["tiles"] > 750000 and report["doodads"] > 0
        assert report["furniture"] == sum(building["levels"] * 3 for building in plan["buildings"])
        assert report["skippedFurniture"] == 0
        assert len(report["previews"]) == len(plan["layers"]) * 5
        for filename in report["previews"]:
            assert Path(filename).name == filename
            assert (output / filename).read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
        assert "preview_z7_roles.png" in report["previews"]
        residences = [i for i, b in enumerate(plan["buildings"]) if b["role"] == "residential"]
        assert [h["building"] for h in report["houses"]] == residences
        assert all(h["townId"] == plan["buildings"][h["building"]]["settlement"] + 1
                   and h["rent"] == 250 and h["size"] > 0 for h in report["houses"])
        assert any("No client DAT supplied" in w for w in report["warnings"])
        assert any("no door items" in w for w in report["warnings"])
        house_nodes = ET.parse(output / "world-houses.xml").getroot().findall("house")
        assert len(house_nodes) == len(residences)
        for node, house in zip(house_nodes, report["houses"]):
            assert int(node.attrib["houseid"]) == house["id"]
            assert int(node.attrib["townid"]) == house["townId"]
            assert int(node.attrib["size"]) == house["size"]
            assert int(node.attrib["rent"]) == 250
            assert [int(node.attrib[a]) for a in ("entryx", "entryy", "entryz")] == house["entryPosition"]
        mapfile = output / "world.otbm"
        assert mapfile.read_bytes()[:4] == b"OTBM"
        before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.iterdir()}
        repeated = subprocess.run(command, cwd=temporary, capture_output=True, text=True,
                                  errors="replace", timeout=10)
        assert repeated.returncode == 1
        assert "already exists" in repeated.stderr
        assert before == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.iterdir()}

        ridged_recipe = json.loads((source / "data/procedural/recipes/example_furnished_city_1098.json").read_text(encoding="utf-8"))
        ridged_recipe["legend"] = str(source / "data/procedural/world_legend.json")
        ridged_recipe["world"].update(catalog=str(source / "data/procedural/profiles/1098/buildings.json"),
                                      relief="ridges", biomeDetail=75, settlements=1, buildings=8,
                                      buildingLevels=2, caveDepth=1, caveEntrances=4)
        ridged_recipe["region"].update(width=256, height=256)
        recipe_path = Path(temporary) / "ridged-recipe.json"
        recipe_path.write_text(json.dumps(ridged_recipe), encoding="utf-8")
        ridged_output = Path(temporary) / "ridged-world"
        ridged_command = command.copy()
        ridged_command[2] = str(recipe_path)
        ridged_command[6] = str(ridged_output)
        ridged_command.extend(["--no-previews", "--tile-budget", "200000"])
        generated = subprocess.run(ridged_command, cwd=temporary, capture_output=True, text=True,
                                   errors="replace", timeout=60)
        if generated.returncode:
            raise AssertionError(f"Ridged CLI returned {generated.returncode}: {generated.stdout} {generated.stderr}")
        ridged_report = json.loads((ridged_output / "generation.json").read_text(encoding="utf-8"))
        ridged_plan = json.loads((ridged_output / "plan.json").read_text(encoding="utf-8"))
        assert ridged_report["tileBudget"] == 200000
        assert ridged_report["verifiedOTBM"] is True
        assert ridged_plan["settings"]["relief"] == "ridges" and ridged_plan["settings"]["biomeDetail"] == 75
        assert len(ridged_plan["buildings"]) >= 4 and ridged_report["furniture"] > 0
        assert ridged_report["skippedFurniture"] == 0 and ridged_report["previews"] == []
        assert sum(link["upper"][2] == 0 and link["lower"][2] == 1 for link in ridged_plan["verticalLinks"]) == 4
        assert not list(ridged_output.glob("preview_*.png"))

        diversity_output = Path(temporary) / "diverse-world"
        habitats = [(0, 255, 0), (0, 128, 0), (232, 192, 96), (240, 248, 255)]
        hints = Path(temporary) / "habitats.png"
        rgb_png(hints, [[habitats[(2 if y >= 4 else 0) + (1 if x >= 4 else 0)]
                         for x in range(8)] for y in range(8)])
        flat = Path(temporary) / "flat.png"
        flat_biome_png(flat, (153, 153, 153))
        diverse_recipe = dict(ridged_recipe)
        diverse_recipe["world"] = dict(ridged_recipe["world"])
        diverse_recipe["world"].update(diverseSettlements=True, settlements=4, buildings=4,
                                       heightmap=str(flat), biomeHints=str(hints), rivers=0)
        diverse_path = Path(temporary) / "diverse-recipe.json"
        diverse_path.write_text(json.dumps(diverse_recipe), encoding="utf-8")
        diverse_command = ridged_command.copy()
        diverse_command[2], diverse_command[6] = str(diverse_path), str(diversity_output)
        diverse_command.append("--editable-inputs")
        generated = subprocess.run(diverse_command, cwd=temporary, capture_output=True,
                                   text=True, errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        diverse = json.loads((diversity_output / "plan.json").read_text(encoding="utf-8"))
        assert diverse["settings"]["diverseSettlements"] is True
        assert {town["biome"] for town in diverse["settlements"]} == {"grass", "forest", "desert", "snow"}
        for index, town in enumerate(diverse["settlements"]):
            residences = [b for b in diverse["buildings"] if b["settlement"] == index and b["role"] == "residential"]
            expected = town["biome"] + "_residence" if town["biome"] in ("desert", "snow") else "residence"
            assert residences and all(b["blueprint"] == expected for b in residences)
        diversity_report = json.loads((diversity_output / "generation.json").read_text(encoding="utf-8"))
        assert diversity_report["previews"] == []
        assert diversity_report["editableInputs"] == {"heightmap": "inputs_heightmap.png",
                                                       "biomeHints": "inputs_biomes.png",
                                                       "landMask": "inputs_land.png",
                                                       "natureMaskTemplate": "inputs_nature.png",
                                                       "recipe": "editable-recipe.json"}
        editable = json.loads((diversity_output / "editable-recipe.json").read_text(encoding="utf-8"))
        assert editable["world"]["heightmap"] == "inputs_heightmap.png"
        assert editable["world"]["biomeHints"] == "inputs_biomes.png"
        assert editable["pathBase"] == "recipe"
        # The editable starting point must be accepted by the real PNG import path.
        diverse_command[2] = str(diversity_output / "editable-recipe.json")
        diverse_command[6] = str(Path(temporary) / "diverse-edited")
        generated = subprocess.run(diverse_command, cwd=temporary, capture_output=True,
                                   text=True, errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr

        balanced_output = Path(temporary) / "balanced-city"
        balanced_command = command.copy()
        balanced_command[2] = str(source / "data/procedural/recipes/example_balanced_city.json")
        balanced_command[6] = str(balanced_output)
        balanced_command.append("--no-previews")
        generated = subprocess.run(balanced_command, cwd=temporary, capture_output=True,
                                   text=True, errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        balanced = json.loads((balanced_output / "plan.json").read_text(encoding="utf-8"))
        assert len(balanced["settlements"]) == 2
        for town in balanced["settlements"]:
            mix = {entry["blueprint"]: entry for entry in town["constructionMix"]}
            assert mix["house"]["count"] >= mix["house"]["min"] == 4
            assert mix["inn"]["count"] == mix["inn"]["min"] == mix["inn"]["max"] == 1
            assert mix["courtyard_hall"]["count"] == 1
            assert sum(entry["count"] for entry in mix.values()) == len(town["buildings"])
        impossible_recipe = json.loads((balanced_output / "recipe.json").read_text(encoding="utf-8"))
        # Move the recipe while keeping its inputs explicit, then oversubscribe its lots.
        impossible_recipe["legend"] = str(source / "data/procedural/world_legend.json")
        impossible_recipe["world"]["catalog"] = str(source / "data/procedural/civic_buildings.json")
        impossible_recipe["world"]["buildings"] = 1
        impossible_path = Path(temporary) / "impossible-city.json"
        impossible_path.write_text(json.dumps(impossible_recipe), encoding="utf-8")
        balanced_command[2] = str(impossible_path)
        balanced_command[6] = str(Path(temporary) / "impossible-city")
        failed = subprocess.run(balanced_command, cwd=temporary, capture_output=True,
                                text=True, errors="replace", timeout=60)
        assert failed.returncode == 1 and "minimums exceed" in failed.stderr
        assert not Path(balanced_command[6]).exists()

        layered_output = Path(temporary) / "layered-town"
        layered_command = command.copy()
        layered_command[2] = str(source / "data/procedural/recipes/example_layered_town.json")
        layered_command[6] = str(layered_output)
        layered_command.append("--no-previews")
        generated = subprocess.run(layered_command, cwd=temporary, capture_output=True,
                                   text=True, errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        layered = json.loads((layered_output / "plan.json").read_text(encoding="utf-8"))
        layered_report = json.loads((layered_output / "generation.json").read_text(encoding="utf-8"))
        layer_tiles = {layer["offsetZ"]: layer["tiles"] for layer in layered["layers"]}
        assert layered["buildings"] and all(building["levels"] == 3 for building in layered["buildings"])
        assert layer_tiles[-1] == len(layered["buildings"]) * 13 * 13
        assert layer_tiles[-2] == len(layered["buildings"]) * 9 * 9
        assert sum(layer_tiles.values()) == layered_report["tiles"]
        assert layered_report["verifiedOTBM"] is True
        styled_output = Path(temporary) / "layered-styles"
        layered_command[2] = str(source / "data/procedural/recipes/example_layered_town_1098.json")
        layered_command[6] = str(styled_output)
        generated = subprocess.run(layered_command, cwd=temporary, capture_output=True,
                                   text=True, errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        styled = json.loads((styled_output / "generation.json").read_text(encoding="utf-8"))
        assert styled["verifiedOTBM"] is True and styled["tiles"] == layered_report["tiles"]
        assert styled["planFingerprint"] != layered_report["planFingerprint"]

        for biome, rgb in [("desert", (232, 192, 96)), ("snow", (240, 248, 255))]:
            hint = Path(temporary) / (biome + ".png")
            flat_biome_png(hint, rgb)
            ridged_recipe["world"]["biomeHints"] = str(hint)
            recipe_path = Path(temporary) / (biome + "-recipe.json")
            recipe_path.write_text(json.dumps(ridged_recipe), encoding="utf-8")
            regional_output = Path(temporary) / (biome + "-world")
            regional_command = ridged_command.copy()
            regional_command[2], regional_command[6] = str(recipe_path), str(regional_output)
            generated = subprocess.run(regional_command, cwd=temporary, capture_output=True,
                                       text=True, errors="replace", timeout=60)
            if generated.returncode:
                raise AssertionError(f"{biome} CLI returned {generated.returncode}: {generated.stdout} {generated.stderr}")
            regional = json.loads((regional_output / "plan.json").read_text(encoding="utf-8"))
            placements = json.loads((regional_output / "generation.json").read_text(encoding="utf-8"))
            residences = [b for b in regional["buildings"] if b["role"] == "residential"]
            assert residences and all(b["blueprint"] == biome + "_residence" for b in residences)
            assert regional["settlements"][0]["biome"] == biome
            assert placements["furniture"] > 0 and placements["skippedFurniture"] == 0
        # Town ordinals must survive the JSON adapter and filter construction diagnostics.
        regional_recipe = json.loads((source / "data/procedural/recipes/example_furnished_city_1098.json").read_text(encoding="utf-8"))
        regional_catalog = {"version": 1, "templates": []}
        residence = json.loads((source / "data/procedural/profiles/1098/buildings.json").read_text(encoding="utf-8"))["templates"][0]
        for ordinal in (1, 2):
            template = dict(residence, id=f"town_{ordinal}_residence", settlements=[ordinal], minPerSettlement=4)
            regional_catalog["templates"].append(template)
        regional_catalog_path = Path(temporary) / "regional-catalog.json"
        regional_catalog_path.write_text(json.dumps(regional_catalog), encoding="utf-8")
        regional_recipe["legend"] = str(source / "data/procedural/world_legend.json")
        regional_recipe["world"].update(catalog=str(regional_catalog_path), buildings=4, caveDepth=0)
        regional_path = Path(temporary) / "regional-recipe.json"
        regional_path.write_text(json.dumps(regional_recipe), encoding="utf-8")
        regional_command = command.copy()
        regional_command[2], regional_command[6] = str(regional_path), str(Path(temporary) / "regional-towns")
        generated = subprocess.run(regional_command, cwd=temporary, capture_output=True, text=True, errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        regional_plan = json.loads((Path(regional_command[6]) / "plan.json").read_text(encoding="utf-8"))
        for town, settlement in enumerate(regional_plan["settlements"], 1):
            assert [entry["blueprint"] for entry in settlement["constructionMix"]] == [f"town_{town}_residence"]
        assert len(regional_plan["buildings"]) == 8
        assert all(building["blueprint"] == f"town_{building['settlement'] + 1}_residence" for building in regional_plan["buildings"])
        # Complete roofed village: directional stairs, irregular streets and mountain layers.
        highland_command = command.copy()
        highland_command[2] = str(source / "data/procedural/recipes/example_highland_village_1098.json")
        highland_command.extend(["--no-previews"])
        highland_bytes = []
        for attempt in range(2):
            highland_command[6] = str(Path(temporary) / f"highland-{attempt}")
            generated = subprocess.run(highland_command, cwd=temporary, capture_output=True, text=True, errors="replace", timeout=60)
            assert generated.returncode == 0, generated.stdout + generated.stderr
            folder = Path(highland_command[6])
            result = json.loads((folder / "generation.json").read_text(encoding="utf-8"))
            highland_plan = json.loads((folder / "plan.json").read_text(encoding="utf-8"))
            assert result["roofTiles"] == 4 * 13 * 13 and result["windows"] > 0
            assert result["stairEndpoints"] == len(highland_plan["verticalLinks"]) * 2
            assert result["skippedFurniture"] == 0 and result["furniture"] == 36
            assert len(highland_plan["layers"]) == 4 and result["verifiedOTBM"]
            assert highland_plan["settings"]["streetVariation"] == 35
            assert highland_plan["settings"]["physicalStairs"] is True
            assert any(link["upper"][2] == -3 for link in highland_plan["verticalLinks"])
            data = (folder / "world.otbm").read_bytes()
            highland_bytes.append(data)
            # Count real floor-change objects and reject teleport metadata in the encoded map.
            floor_changes = 0
            known_stairs = set()
            items = ET.parse(source / "data/1098/items_original1098.xml").getroot()
            for item in items.findall("item"):
                if any(a.attrib.get("key") == "floorchange" for a in item.findall("attribute")):
                    known_stairs.update(range(int(item.attrib.get("id", item.attrib.get("fromid"))), int(item.attrib.get("id", item.attrib.get("toid"))) + 1))
            for node, ancestors in otbm_nodes(data):
                if node[0] in (5, 14):
                    offset = 3 if node[0] == 5 else 7
                    while offset < len(node):
                        attr = node[offset]; offset += 1
                        if attr == 3: offset += 4
                        elif attr == 9:
                            floor_changes += struct.unpack_from("<H", node, offset)[0] in known_stairs
                            offset += 2
                        else: raise AssertionError(f"Unexpected tile attribute {attr}")
                elif node[0] == 6:
                    floor_changes += struct.unpack_from("<H", node, 1)[0] in known_stairs
            assert floor_changes == result["stairEndpoints"]
        assert highland_bytes[0] == highland_bytes[1]
        # Wrong client travel IDs must fail before creating any output.
        bad_stairs = json.loads((source / "data/procedural/recipes/example_highland_village_1098.json").read_text())
        for key in ("heightmap", "biomeHints", "landMask", "catalog", "objectProfile"):
            bad_stairs["world"][key] = str((source / "data/procedural/recipes" / bad_stairs["world"][key]).resolve())
        bad_stairs["legend"] = str(source / "data/procedural/world_legend.json")
        bad_stairs["world"]["stairItems"] = [1387, 1385, 1388, 1392, 1390]
        bad_path = Path(temporary) / "bad-stairs.json"
        bad_path.write_text(json.dumps(bad_stairs), encoding="utf-8")
        highland_command[2], highland_command[6] = str(bad_path), str(Path(temporary) / "invalid-stairs")
        generated = subprocess.run(highland_command, cwd=temporary, capture_output=True, text=True, errors="replace", timeout=60)
        assert generated.returncode == 1 and not Path(highland_command[6]).exists()
        # Bounded fitting keeps required furniture when a stair reservation occupies its halo.
        fit_catalog = json.loads((source / "data/procedural/profiles/1098/roofed_buildings.json").read_text())
        for template in fit_catalog["templates"]:
            for prop in template["objects"]:
                if "bed" in prop["brushes"][0]:
                    prop.update(x=4, y=4, placementRadius=2)
        fit_catalog_path = Path(temporary) / "fitting-catalog.json"
        fit_catalog_path.write_text(json.dumps(fit_catalog), encoding="utf-8")
        bad_stairs["world"].update(stairItems=[0, 0, 0, 0, 0], catalog=str(fit_catalog_path))
        fit_recipe = Path(temporary) / "fitting-recipe.json"
        fit_recipe.write_text(json.dumps(bad_stairs), encoding="utf-8")
        highland_command[2], highland_command[6] = str(fit_recipe), str(Path(temporary) / "fitting-world")
        generated = subprocess.run(highland_command, cwd=temporary, capture_output=True, text=True, errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        fitting = json.loads((Path(highland_command[6]) / "generation.json").read_text())
        assert fitting["relocatedFurniture"] > 0 and fitting["skippedFurniture"] == 0 and fitting["furniture"] == 36
        for template in fit_catalog["templates"]:
            for prop in template["objects"]: prop["placementRadius"] = 0
        fit_catalog_path.write_text(json.dumps(fit_catalog), encoding="utf-8")
        highland_command[6] = str(Path(temporary) / "unfitted-world")
        generated = subprocess.run(highland_command, cwd=temporary, capture_output=True, text=True, errors="replace", timeout=60)
        assert generated.returncode == 1 and not Path(highland_command[6]).exists()
        print("PASS: separate roofs, client windows, physical stair items, mountain layers, invalid travel rejection and byte-identical replay")
        # Exact profiles vary only objects; terrain/city planning keeps its master seed.
        object_recipe = json.loads(Path(ridged_command[2]).read_text(encoding="utf-8"))
        object_recipe["world"].update(objectProfile=str(source / "data/procedural/profiles/1098/objects_ecology.json"))
        object_path = Path(temporary) / "objects-recipe.json"
        object_command = ridged_command.copy()
        object_command[2] = str(object_path)
        object_reports, object_bytes = [], []
        for seed in (123, 456, 123):
            object_recipe["world"]["objectSeed"] = seed
            object_path.write_text(json.dumps(object_recipe), encoding="utf-8")
            object_command[6] = str(Path(temporary) / f"objects-{len(object_reports)}")
            generated = subprocess.run(object_command, cwd=temporary, capture_output=True,
                                       text=True, errors="replace", timeout=60)
            assert generated.returncode == 0, generated.stdout + generated.stderr
            folder = Path(object_command[6])
            result = json.loads((folder / "generation.json").read_text(encoding="utf-8"))
            assert result["objectSeed"] == seed and result["doodads"] > 0
            assert sum(result["objectBrushUsage"].values()) == result["doodads"]
            assert result["objectPatchUsage"] and sum(result["objectPatchUsage"].values()) <= result["doodads"]
            assert result["objectBrushRejected"] == {}
            assert result["planFingerprint"] == ridged_report["planFingerprint"]
            object_reports.append(result)
            object_bytes.append((folder / "world.otbm").read_bytes())
        assert object_bytes[0] == object_bytes[2] and object_bytes[0] != object_bytes[1]
        # Scene groups commit all their anchors together, retain seed isolation and reject malformed data.
        scene_path = Path(temporary) / "scenes.json"
        scene = dict(id="test_grove", habitats=["forest"], spacing=16, radius=3, chance=100,
                     settlements=[1],objects=[dict(x=-1,y=0,brushes=["grass tufts"]),dict(x=1,y=0,brushes=["grass tufts"])])
        scene_profile = dict(version=1, biomes={"forest":dict(brushes=[])}, scenes=[scene])
        scene_bytes=[]
        for attempt, seed in enumerate((123,456,123)):
            scene_path.write_text(json.dumps(scene_profile),encoding="utf-8")
            object_recipe["world"].update(objectProfile=str(scene_path),objectSeed=seed)
            object_path.write_text(json.dumps(object_recipe),encoding="utf-8")
            object_command[6]=str(Path(temporary)/f"scene-{attempt}")
            generated=subprocess.run(object_command,cwd=temporary,capture_output=True,text=True,errors="replace",timeout=60)
            assert generated.returncode==0,generated.stdout+generated.stderr
            folder=Path(object_command[6]);result=json.loads((folder/"generation.json").read_text())
            assert result["sceneUsage"]["test_grove"]>0 and result["doodads"]==result["sceneUsage"]["test_grove"]*2
            assert len(result["scenePlacements"])==result["sceneUsage"]["test_grove"] and all(s["objects"]==2 and len(s["position"])==3 and 0<=s["rotation"]<=3 for s in result["scenePlacements"])
            assert result["objectBrushRejected"]=={} and result["planFingerprint"]==ridged_report["planFingerprint"]
            scene_bytes.append((folder/"world.otbm").read_bytes())
        assert scene_bytes[0]==scene_bytes[2] and scene_bytes[0]!=scene_bytes[1]
        other_region=json.loads(json.dumps(scene_profile));other_region["scenes"][0]["settlements"]=[2]
        scene_path.write_text(json.dumps(other_region),encoding="utf-8");object_command[6]=str(Path(temporary)/"scene-other-region")
        generated=subprocess.run(object_command,cwd=temporary,capture_output=True,text=True,errors="replace",timeout=60)
        assert generated.returncode==0,generated.stdout+generated.stderr
        result=json.loads((Path(object_command[6])/"generation.json").read_text())
        assert result["sceneUsage"]=={} and result["doodads"]==0
        # Distinct anchors whose composite footprints overlap must leave no partial objects.
        overlap=json.loads(json.dumps(scene_profile))
        overlap["scenes"][0]["objects"]=[dict(x=0,y=0,brushes=["world table 0"]),dict(x=1,y=0,brushes=["world table 0"])]
        scene_path.write_text(json.dumps(overlap),encoding="utf-8")
        object_command[6]=str(Path(temporary)/"scene-overlap")
        generated=subprocess.run(object_command,cwd=temporary,capture_output=True,text=True,errors="replace",timeout=60)
        assert generated.returncode==0,generated.stdout+generated.stderr
        result=json.loads((Path(object_command[6])/"generation.json").read_text())
        assert result["sceneUsage"]=={} and result["doodads"]==0 and result["skippedScenes"]>0
        for index,patch in enumerate([dict(radius=1),dict(spacing=16,radius=12),dict(minMoisture=9000,maxMoisture=1000),dict(rotate=1),dict(settlements=[0]),dict(habitats=["unknown"]),dict(objects=[dict(x=0,y=0,brushes=["missing scene brush"])])]):
            invalid=json.loads(json.dumps(scene_profile));invalid["scenes"][0].update(patch)
            scene_path.write_text(json.dumps(invalid),encoding="utf-8")
            object_command[6]=str(Path(temporary)/f"scene-invalid-{index}")
            generated=subprocess.run(object_command,cwd=temporary,capture_output=True,text=True,errors="replace",timeout=30)
            assert generated.returncode==1 and not Path(object_command[6]).exists(),generated.stdout+generated.stderr
        print("PASS: atomic scene compositions, no partial overlap placement, seeded replay, climate and malformed scene rejection")
        # Public composed village exercises actual directional roof IDs, districts and cave rooms.
        composed_command=command.copy();composed_command[2]=str(source/"data/procedural/recipes/example_composed_town_1098.json")
        composed_command[6]=str(Path(temporary)/"composed-town");composed_command.extend(["--no-previews"])
        generated=subprocess.run(composed_command,cwd=temporary,capture_output=True,text=True,errors="replace",timeout=60)
        assert generated.returncode==0,generated.stdout+generated.stderr
        folder=Path(composed_command[6]);result=json.loads((folder/"generation.json").read_text());composed_plan=json.loads((folder/"plan.json").read_text())
        assert result["verifiedOTBM"] and result["sceneUsage"] and not result["objectBrushRejected"] and not result["skippedFurniture"]
        assert len(composed_plan["layers"])==6 and any(b["district"]=="civic" for b in composed_plan["buildings"])
        roof_ids=set()
        for node, ancestors in otbm_nodes((folder/"world.otbm").read_bytes()):
            if node[0]==5:
                offset=3
                while offset<len(node):
                    attr=node[offset];offset+=1
                    if attr==3:offset+=4
                    elif attr==9:roof_ids.add(struct.unpack_from("<H",node,offset)[0]);offset+=2
                    else:raise AssertionError(f"Unexpected tile attribute {attr}")
            elif node[0]==6:roof_ids.add(struct.unpack_from("<H",node,1)[0])
        assert len(roof_ids & {926,927,929,936,937,939,946,947,948,956,957,958})>=3
        print("PASS: composed village, role districts, directional roof materials and connected multi-floor caverns")
        # Patch constraints must affect actual placement, including shoreline distance.
        constraint_profile = Path(temporary) / "constrained-objects.json"
        for limit in (0, 5):
            constraint_profile.write_text(json.dumps({"version":1,"biomes":{"forest":{"density":150,"brushes":[],"baseWeight":0,"patches":[{"id":"bank","maxWaterDistance":limit,"brushes":[{"name":"grass tufts"}]}]}}}), encoding="utf-8")
            object_recipe["world"]["objectProfile"] = str(constraint_profile)
            object_path.write_text(json.dumps(object_recipe), encoding="utf-8")
            object_command[6] = str(Path(temporary) / f"shore-{limit}")
            generated = subprocess.run(object_command, cwd=temporary, capture_output=True, text=True, errors="replace", timeout=60)
            assert generated.returncode == 0, generated.stdout + generated.stderr
            constrained = json.loads((Path(object_command[6]) / "generation.json").read_text())
            assert constrained["planFingerprint"] == ridged_report["planFingerprint"]
            if limit == 0:
                assert constrained["doodads"] == 0 and constrained["objectPatchUsage"] == {}
            else:
                assert constrained["doodads"] > 0 and set(constrained["objectBrushUsage"]) == {"grass tufts"}
        # Malformed patch groups fail atomically, even when another palette is usable.
        for invalid in ({"id":"bad","weight":0}, {"id":"bad","minElevation":9000,"maxElevation":1000},
                        {"id":"bad","maxWaterDistance":33}, {"id":"bad","minMoisture":False}, {"id":"base"}):
            invalid["brushes"] = [{"name":"grass tufts"}]
            constraint_profile.write_text(json.dumps({"version":1,"biomes":{"forest":{"brushes":[{"name":"green trees"}],"patches":[invalid]}}}), encoding="utf-8")
            object_command[6] = str(Path(temporary) / "invalid-patch")
            generated = subprocess.run(object_command, cwd=temporary, capture_output=True, text=True, errors="replace", timeout=60)
            assert generated.returncode == 1 and not Path(object_command[6]).exists()
        # A typo in an explicit palette must fail without exporting a partial world.
        bad_profile = Path(temporary) / "bad-objects.json"
        for brushes in ([{"name": "missing exact brush"}], [{"name": "green trees", "weight": 0}]):
            bad_profile.write_text(json.dumps({"version": 1, "biomes": {"forest": {"brushes": brushes}}}), encoding="utf-8")
            object_recipe["world"]["objectProfile"] = str(bad_profile)
            object_path.write_text(json.dumps(object_recipe), encoding="utf-8")
            object_command[6] = str(Path(temporary) / "invalid-objects")
            generated = subprocess.run(object_command, cwd=temporary, capture_output=True,
                                       text=True, errors="replace", timeout=60)
            assert generated.returncode != 0 and not Path(object_command[6]).exists()

        # Original geography studies must realize their complete civic/residential mix.
        for scenario, buildings in (("coastal_port", 48), ("wetland_trade", 40)):
            city_command = command.copy()
            city_command[2] = str(source / "data/procedural/scenarios" / scenario / "recipe.json")
            city_command[6] = str(Path(temporary) / scenario)
            city_command.append("--no-previews")
            generated = subprocess.run(city_command, cwd=temporary, capture_output=True,
                                       text=True, errors="replace", timeout=60)
            assert generated.returncode == 0, generated.stdout + generated.stderr
            folder = Path(city_command[6])
            city = json.loads((folder / "plan.json").read_text(encoding="utf-8"))
            result = json.loads((folder / "generation.json").read_text(encoding="utf-8"))
            recipe = json.loads((folder / "recipe.json").read_text(encoding="utf-8"))
            assert len(city["buildings"]) == buildings and result["verifiedOTBM"]
            anchor = recipe["world"]["settlementAnchors"][0]
            assert city["settlements"][0]["center"][:2] == [anchor["x"], anchor["y"]]
            assert {"temple", "depot_hall", "bank_hall", "market_hall", "tavern_inn",
                    "guard_hall", "guild_library", "town_house"} <= {b["blueprint"] for b in city["buildings"]}
            assert result["furniture"] > 250 and result["skippedFurniture"] == 0

        # Seed overrides must survive recipe export, including both uint32 boundaries.
        seed_recipe = ridged_recipe.copy()
        seed_recipe["world"] = dict(ridged_recipe["world"])
        seed_recipe["world"].pop("biomeHints", None)
        seed_recipe["world"].update(relief="smooth", buildings=4, caveEntrances=1)
        seed_recipe["region"] = dict(width=128, height=128, originX=0, originY=0, z=7)
        seed_path = Path(temporary) / "seed-recipe.json"
        seed_path.write_text(json.dumps(seed_recipe), encoding="utf-8")
        recipe_hash = hashlib.sha256(seed_path.read_bytes()).digest()
        seed_command = ridged_command.copy()
        for option in ("--house-prefix", "--house-rent"):
            at = seed_command.index(option)
            del seed_command[at:at + 2]
        seed_command[2] = str(seed_path)
        fingerprints = []
        for seed in (0, 4294967295):
            seed_output = Path(temporary) / f"seed-{seed}"
            seed_command[6] = str(seed_output)
            generated = subprocess.run(seed_command + ["--seed", str(seed)], cwd=temporary,
                                       capture_output=True, text=True, errors="replace", timeout=60)
            assert generated.returncode == 0, generated.stdout + generated.stderr
            seeded = json.loads((seed_output / "generation.json").read_text(encoding="utf-8"))
            saved_recipe = json.loads((seed_output / "recipe.json").read_text(encoding="utf-8"))
            seeded_plan = json.loads((seed_output / "plan.json").read_text(encoding="utf-8"))
            assert seeded["seed"] == saved_recipe["seed"] == seeded_plan["settings"]["seed"] == seed
            fingerprints.append(seeded["planFingerprint"])
        assert fingerprints[0] != fingerprints[1]
        assert hashlib.sha256(seed_path.read_bytes()).digest() == recipe_hash
        replay = seed_command.copy()
        replay[2] = str(seed_output / "recipe.json")
        replay[6] = str(Path(temporary) / "seed-replay")
        generated = subprocess.run(replay, cwd=temporary, capture_output=True, text=True,
                                   errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        assert (Path(replay[6]) / "world.otbm").read_bytes() == (seed_output / "world.otbm").read_bytes()
        invalid_output = Path(temporary) / "invalid-seed"
        seed_command[6] = str(invalid_output)
        for arguments in (["--seed", "-1"], ["--seed", "4294967296"], ["--seed", "+1"],
                          ["--seed", "1.5"], ["--seed", " 1"], ["--seed", ""],
                          ["--seed", "1", "--seed", "2"], ["--seed"]):
            failed = subprocess.run(seed_command + arguments, cwd=temporary, capture_output=True,
                                    text=True, errors="replace", timeout=10)
            assert failed.returncode == 2, (arguments, failed.stdout, failed.stderr)
            assert not invalid_output.exists()
        prefab_output = Path(temporary) / "prefab-world"
        prefab_command = command.copy()
        prefab_command[2] = str(source / "data/procedural/recipes/example_prefab_town_1098.json")
        prefab_command[6] = str(prefab_output)
        generated = subprocess.run(prefab_command, cwd=temporary, capture_output=True,
                                   text=True, errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        authored = json.loads((prefab_output / "generation.json").read_text(encoding="utf-8"))
        authored_plan = json.loads((prefab_output / "plan.json").read_text(encoding="utf-8"))
        assert authored["verifiedOTBM"] and authored["prefabBuildings"] == 3
        assert authored["prefabTiles"] == 378 and authored["furniture"] == 9
        assert len([b for b in authored_plan["buildings"] if "prefabFingerprint" in b]) == 3
        assert len(authored["houses"]) == 3
        assert all(h["tiles"] == 126 and h["doors"] == 1 and h["rent"] == 250 for h in authored["houses"])
        assert not any("no door items" in w for w in authored["warnings"])
        house_tiles, house_doors = {}, {}
        for node, parents in otbm_nodes((prefab_output / "world.otbm").read_bytes()):
            if node[0] == 14:  # OTBM_HOUSETILE: local x/y, uint32 house ID.
                house_id = struct.unpack_from("<I", node, 3)[0]
                house_tiles[house_id] = house_tiles.get(house_id, 0) + 1
            if node[:3] == b"\x06\x6e\x18":  # Public open door, server ID 6254.
                assert parents[-1][0] == 14
                house_id = struct.unpack_from("<I", parents[-1], 3)[0]
                assert node[3:] == b"\x0e\x01"  # House-door attribute, per-house ID 1.
                house_doors[house_id] = house_doors.get(house_id, 0) + 1
        assert house_tiles == {h["id"]: h["tiles"] for h in authored["houses"]}
        assert house_doors == {h["id"]: 1 for h in authored["houses"]}
        replay = prefab_command.copy()
        replay[2] = str(prefab_output / "recipe.json")
        replay[6] = str(Path(temporary) / "prefab-replay")
        generated = subprocess.run(replay, cwd=temporary, capture_output=True,
                                   text=True, errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        assert (Path(replay[6]) / "world.otbm").read_bytes() == (prefab_output / "world.otbm").read_bytes()
        assert (Path(replay[6]) / "world-houses.xml").read_bytes() == (prefab_output / "world-houses.xml").read_bytes()
        compact = prefab_command + ["--no-diagnostics", "--no-previews"]
        compact[6] = str(Path(temporary) / "compact-server-world")
        generated = subprocess.run(compact, cwd=temporary, capture_output=True, text=True,
                                   errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        compact_dir = Path(compact[6])
        compact_report = json.loads((compact_dir / "generation.json").read_text(encoding="utf-8"))
        assert compact_report["diagnosticsExported"] is False and compact_report["previews"] == []
        assert not (compact_dir / "plan.json").exists()
        assert not list(compact_dir.glob("*.ppm")) and not list(compact_dir.glob("*.png"))
        for artifact in ("world.otbm", "world-houses.xml", "world-spawns.xml", "recipe.json"):
            # Recipe-relative paths differ between folders; compare only server bytes.
            if artifact != "recipe.json":
                assert (compact_dir / artifact).read_bytes() == (prefab_output / artifact).read_bytes()
            else:
                assert (compact_dir / artifact).exists()
        full_bytes = sum(p.stat().st_size for p in prefab_output.iterdir())
        compact_bytes = sum(p.stat().st_size for p in compact_dir.iterdir())
        assert compact_bytes < full_bytes
        print(f"PASS: compact server bundle {compact_bytes} vs {full_bytes} bytes, identical OTBM/XML")
        # Metadata is opt-in: the same plan without housing must retain ordinary tiles.
        plain = prefab_command.copy()
        plain[6] = str(Path(temporary) / "prefab-without-houses")
        del plain[-4:]
        generated = subprocess.run(plain, cwd=temporary, capture_output=True, text=True,
                                   errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        plain_report = json.loads((Path(plain[6]) / "generation.json").read_text(encoding="utf-8"))
        assert plain_report["houses"] == [] and plain_report["planFingerprint"] == authored["planFingerprint"]
        assert ET.parse(Path(plain[6]) / "world-houses.xml").getroot().findall("house") == []
        assert (Path(plain[6]) / "world.otbm").read_bytes() != (prefab_output / "world.otbm").read_bytes()
        # Ground-level houses must restore XML exits even when z == 0.
        ground_recipe = json.loads((source / "data/procedural/recipes/example_furnished_city_1098.json").read_text(encoding="utf-8"))
        ground_recipe["legend"] = str(source / "data/procedural/world_legend.json")
        ground_recipe["world"].update(catalog=str(source / "data/procedural/profiles/1098/buildings.json"),
                                       settlements=1, buildings=4, buildingLevels=1, caveDepth=0)
        ground_recipe["region"].update(width=256, height=256, z=0)
        ground_path = Path(temporary) / "ground-houses.json"
        ground_path.write_text(json.dumps(ground_recipe), encoding="utf-8")
        ground = prefab_command.copy()
        ground[2], ground[6] = str(ground_path), str(Path(temporary) / "ground-houses")
        generated = subprocess.run(ground, cwd=temporary, capture_output=True, text=True,
                                   errors="replace", timeout=60)
        assert generated.returncode == 0, generated.stdout + generated.stderr
        ground_report = json.loads((Path(ground[6]) / "generation.json").read_text(encoding="utf-8"))
        assert ground_report["houses"] and all(h["entryPosition"][2] == 0 for h in ground_report["houses"])
        # Reject invalid options without creating an output directory.
        invalid = plain.copy()
        invalid[6] = str(Path(temporary) / "invalid-houses")
        for extra, code in [(["--house-rent", "-1"], 2), (["--house-rent", "2147483648"], 2),
                            (["--house-rent", "0"], 2), (["--house-prefix", ""], 2),
                            (["--house-prefix", "Home", "--house-rent", "1.5"], 2),
                            (["--house-prefix", "Home", "--town-prefix", ""], 1),
                            (["--house-prefix", "  "], 1),
                            (["--house-prefix", "Home", "--house-prefix", "Other"], 2)]:
            generated = subprocess.run(invalid + extra, cwd=temporary, capture_output=True,
                                       text=True, errors="replace", timeout=10)
            assert generated.returncode == code, generated.stdout + generated.stderr
            assert not Path(invalid[6]).exists()
        print("PASS: residential house ownership, town IDs, rent, door IDs, XML readback, opt-in and z=0 exits")
        print("PASS: real 10.98 authored OTBM prefabs, mixed catalogs and byte-identical CLI replay")
        print(f"PASS: external CLI, recipe-relative assets, bundled 10.98 materials, "
              f"{report['tiles']} tiles, {report['furniture']} furniture, no-overwrite; "
              "ridged cave entrances, desert/snow architecture, construction minimums and seeded OTBM replay")


if __name__ == "__main__":
    main()
