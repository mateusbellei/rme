"""Exercise the public 10.98 profile through the installed application entry point."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


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
                   "--data-dir", str(source / "data")]
        # Run outside the checkout: only explicit arguments / recipe-relative paths apply.
        generated = subprocess.run(command, cwd=temporary, capture_output=True, text=True,
                                   errors="replace", timeout=60)
        if generated.returncode:
            raise AssertionError(f"CLI returned {generated.returncode}: {generated.stdout} {generated.stderr}")
        report = json.loads((output / "generation.json").read_text(encoding="utf-8"))
        plan = json.loads((output / "plan.json").read_text(encoding="utf-8"))
        assert report["client"]["clientVersion"] == 57
        assert report["planFingerprint"] == plan["fingerprint"]
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
        assert len(report["warnings"]) == 1 and "No client DAT supplied" in report["warnings"][0]
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
        ridged_command.append("--no-previews")
        generated = subprocess.run(ridged_command, cwd=temporary, capture_output=True, text=True,
                                   errors="replace", timeout=60)
        if generated.returncode:
            raise AssertionError(f"Ridged CLI returned {generated.returncode}: {generated.stdout} {generated.stderr}")
        ridged_report = json.loads((ridged_output / "generation.json").read_text(encoding="utf-8"))
        ridged_plan = json.loads((ridged_output / "plan.json").read_text(encoding="utf-8"))
        assert ridged_plan["settings"]["relief"] == "ridges" and ridged_plan["settings"]["biomeDetail"] == 75
        assert len(ridged_plan["buildings"]) >= 4 and ridged_report["furniture"] > 0
        assert ridged_report["skippedFurniture"] == 0 and ridged_report["previews"] == []
        assert sum(link["upper"][2] == 0 and link["lower"][2] == 1 for link in ridged_plan["verticalLinks"]) == 4
        assert not list(ridged_output.glob("preview_*.png"))
        print(f"PASS: external CLI, recipe-relative assets, bundled 10.98 materials, "
              f"{report['tiles']} tiles, {report['furniture']} furniture, no-overwrite")


if __name__ == "__main__":
    main()
