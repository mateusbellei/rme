# Authored OTBM construction prefabs

A World planner catalog can place a bounded OTBM construction alongside generated
buildings. Decoded ground IDs, item stacks, ordinary container contents and item
attributes are copied into staging. Existing collision checks, physical navigation,
protected tiles, cancellation, and the single World undo/redo transaction apply.

This first version uses fixed orientation. It does not rotate sprite IDs or infer
entrances from artwork. The catalog supplies the same semantic footprint used by
procedural buildings, so roads and generated floor links have explicit connectors.

## Author a standalone chunk

1. Use the same item registry/client as the target world. Save a separate OTBM
   containing only the construction, including ground under every declared wall
   and floor cell. Source coordinates can be nonzero.
2. Describe its footprint with the existing catalog symbols: `#` is blocking wall,
   `.` is interior, `+` is the single street entrance on the base boundary, and
   aligned `a`/`b` cells are vertical landings. Keep the landings and their adjacent
   arrivals clear. Blocking furniture is allowed where world connectivity survives.
3. Add `rotate:false` and a `prefab` binding. `origin` is the source top-left x/y and
   base z. Higher floors use the same x/y and decreasing z. With multiple floors,
   explicitly supply `upperRows` or `upperFloors`; upper rows use `#` or space
   instead of base-only `+`/`,` symbols. Space means no source tile at that level.
4. Load the catalog through World settings or a relative world recipe. Preview,
   apply and inspect the map. Prefab contents bypass generated ground randomization,
   borderization, wallization and furniture; surrounding procedural terrain still
   uses its normal rules.

For example, a three-floor chunk at source `(0,0,7)`:

```json
{
  "version": 1,
  "templates": [{
    "id": "authored_house",
    "role": "residential",
    "weight": 4,
    "maxLevels": 3,
    "rotate": false,
    "minPerSettlement": 2,
    "rows": [
      "#######", "#a...b#", "#.....#",
      "#.....#", "#.....#", "###+###"
    ],
    "upperRows": [
      "#######", "#a...b#", "#.....#",
      "#.....#", "#.....#", "#######"
    ],
    "prefab": {"file": "house.otbm", "origin": [0, 0, 7]}
  }]
}
```

`file` resolves relative to the catalog, independently of the working directory.
Every declared non-space cell must contain ground. Extra source tiles outside the
footprint/floor range fail; export a standalone chunk instead of a whole city map.
The requested world height may use fewer than `maxLevels`; the source still defines
and validates every declared floor. Generated prefab and procedural instances can
share roles, biome constraints and minimum/maximum counts in one catalog.

## Compatibility and data rules

The OTBM header must exactly match the loaded OTB major/minor versions and use a
supported map format. No cross-client item remapping is implied. Plain doors and
containers are supported; closed doors cannot occupy a declared walkable entrance
or landing. House door IDs, Unique/Action IDs (including nested contents), depots,
editor-only items and authored teleports/floor-change items are rejected.

House tiles, town/waypoint/spawn nodes, zone IDs and map flags are rejected.
External XML sidecar references are not followed or imported. Server entities and
quest scripting are separate authoring steps. The planner inserts its paired
teleports on the annotated landings; physical stairs remain a separate roadmap.

When DAT metadata is loaded, top-level sprite extents must stay inside the declared
footprint. Without DAT, physical blocking and coordinates are validated, while
sprite extents and visual appearance remain unverified. Preview PNGs show semantic
categories, not a render of the authored sprites.

Catalogs capture a fingerprint of the source bytes. Changing a source file after
preview causes apply to fail before map changes; rebuild the preview to accept it.
Identical source bytes and coordinates preserve the plan fingerprint after moving
the catalog/prefab together, including Unicode paths. Exported recipes reference
the catalog; output bundles do not duplicate its OTBM source assets.

The batch report adds `prefabBuildings` and `prefabTiles`; `furniture` continues to
count newly generated furniture, excluding items already inside prefabs. Plan
building records include `prefabFingerprint` for authored instances, without
exporting absolute source filenames.

## Bounds

Footprints remain 5..32 cells per side and 1..4 floors. Each source is limited to
2 MiB, 4096 tiles, 32768 nodes and 4096 payload bytes per node. Tree depth is checked
before the legacy decoder runs; containers additionally have at most 64 entries
and 16 nested levels. Tile stacks allow 64 items. Used source files total at most
16 MiB, and all placed prefabs together allow at most 2,097,152 expanded grounds/
items. Existing world tile budgets still apply. Invalid files, unknown IDs,
missing ground, coordinate overflow or incompatible metadata fail before staging.

## Reproducible public 10.98 example

`data/procedural/recipes/example_prefab_town_1098.json` combines three authored
houses with one generated inn. The generic sample OTBM uses public 10.98 server
IDs for wood flooring, stone walls, an open doorway, a chair and a readable book.
No private DAT/SPR is included. The example produced 131,610 tiles, 378 copied
prefab tiles across three houses and nine generated furniture placements, and
passed OTBM read/re-encode verification. These checks do not establish visual
quality or server gameplay.

```powershell
rme --generate-world data/procedural/recipes/example_prefab_town_1098.json data/1098/items_original1098.otb data/1098/items_original1098.xml data/1098/materials.xml build/prefab-world --data-dir data --town-prefix Prefab --verify --editable-inputs | Out-Host
```

Choose a new output directory. On Linux use `xvfb-run -a rme ...` without the
PowerShell `Out-Host` suffix. The external CLI regression runs this profile outside
the checkout and requires byte-identical replay from its exported recipe.
