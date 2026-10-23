# Procedural generation: audit and next deliveries

## Server export continuation (2026-10-18)

The continuation based on the prefab branch adds optional residential OTBM house
tiles, native XML rental/town/exit records and per-house door IDs to isolated batch
exports. Verification restores house XML onto decoded OTBM tiles. Compact exports
can omit uncompressed PPM/plan diagnostics independently of PNG previews, preserving
identical server artifacts. See [server export](WORLD_SERVER_EXPORT.md).
A local x64 run verified the maximum 2048x2048 surface: 4,194,304 tiles, four towns,
52 houses, 2682.1 MiB peak working set and 33.43 MiB compact output. Extra populated
floors at that surface size would exceed the supported tile budget.
This remains a feature branch until its PR is integrated; spawns/NPCs, physical
stairs, arbitrary prefab rotation and real-client/server QA are still pending.

Audit: 2026-10-15. Tracking: https://github.com/mateusbellei/rme/issues/5

## OTBM prefab continuation (2026-10-17)

PRs #4, #6 and #7 are integrated into main. This continuation adds fixed-orientation
OTBM prefab catalog bindings, bounded loading, source fingerprint checks and copying
of authored multi-floor items into the existing world transaction. It is a separate
feature branch until its PR is integrated. See [the prefab guide](WORLD_OTBM_PREFABS.md).
Rotation/remapping, physical stairs and server entity generation remain open. The
sections below retain the earlier audit and delivery history.

## World planner update (2026-10-16, draft PR #7)

The original audit below describes the legacy v3 pipeline. The new opt-in
`source:world` path now has deterministic continents/biomes, urban lots and
furnished rotating catalogs, multi-floor structures, connected caves, distributed
entrances, preview/seed comparison and staged single undo/redo. Recipes support
relative paths and strict bounded UTF-8/BOM loading.

The latest draft also includes hard construction minimums, different floor plans
and client materials by building level, optional settlement distribution across
biomes, seed overrides with exact recipe replay, and zoom/tile inspection. Plan
exports can produce editable height/biome PNGs with a separate relative recipe.

Batch generation loads client OTB/XML/materials explicitly, exports OTBM/XML/PNG
bundles, optionally registers towns and verifies production read/re-encode byte
equality. The public 10.98 large profile was exported and verified at 3,161,744
tiles locally. This does not establish server/client visual QA. Physical
stairs/ramps, arbitrary OTBM prefab imports and house/spawn/NPC authoring remain
open. See [the current guide](WORLD_GENERATION.md) and
[PR #7](https://github.com/mateusbellei/rme/pull/7).

The world path has five core/integration suites plus three full-application
tests; the old one-test limitation below applies to the PNG foundation audit,
not the current world regression. Existing v3 presets remain supported.

## Repository state at audit

- Remote: `mateusbellei/rme`, default branch `main`.
- Procedural PR #1, zones PR #2 and Data Editor PR #3 were merged.
- Redux PR #4 is open against `main`, head `70d7e1a`.
- Local `feat/redux` was two commits ahead, at `dce1078`, with additional
  uncommitted Data Editor, client data and VS project changes. Those changes
  are outside this delivery.
- No standalone issues existed before #5. Existing plans were in `.agents/`.

## What exists, and its limits

| Feature | Code / current behavior | Remaining work |
| --- | --- | --- |
| PNG mask | `image_mask_generator.cpp`, `procedural_common.cpp`; RGB to ground brushes, selection rectangle | Now also imports converter IDs; see workflow below |
| Prompt | `prompt_generator.cpp`; keyword routing to one preset, generated color mask | Multi-biome composition, region constraints, roads and rivers |
| Props | `ScatterPresetDoodads`; client brush names matched by keywords | Biome-specific distributions, clearances, footprint and route checks |
| City | Flat block/street color grid plus wall heuristics | Actual buildings, doors, interiors and OTBM chunks |
| Mountains | Heightmap stacked on multiple floors, surface brushes | Navigable ramps and explicit connections |
| Deep cave | Independent cave mask per floor | Connected components and genuine vertical travel |
| Recipes | v3 JSON config, save/load dialog | Layered structure contract, portable paths, full reproducibility |
| Sidecar | Optional Python stub/HTTP enhancement | Optional only; does not replace deterministic generation |
| Headless | C++ facade still uses editor/GUI services | Real command-line setup, client loading and output OTBM |

The cave shaft loop reads `editor.map` before the terrain action is committed,
so it reads old tiles. It can also queue multiple replacements for the same
position. Painting ground at adjacent z values does not create a stair, hole,
ladder or ramp. Current multi-floor output must not be described as navigable.

The current pipeline creates several undo batches and uses global brush RNG;
the entire output is not yet guaranteed to be a single undo step or repeatable
from seed alone. The new categorical mask blend itself is deterministic.

## PNGtoOTBM-compatible workflow

1. Prepare a PNG with one color per terrain category. Transparent pixels skip
   ground painting; any alpha above zero is paintable, as in the converter.
2. In PNGtoOTBMConverter export **color mappings** (`color_mappings.json`).
3. In RME load the matching client/OTB. Open **Generate Map**, select the image
   and use the exported JSON in **Legend / color mappings (JSON)**.
4. Set the region/selection, floor and pipeline settings in RME. Converter
   `settings` (client, offsets, z, transparent tile ID) are not imported.
5. Generate, inspect and save through RME's existing OTBM writer.

Supported converter data:

```json
{
  "version": 1,
  "colors": [
    { "hex": "#00FF00", "tileId": 100 },
    { "hex": "#0000FF", "tileId": 101 },
    { "hex": "#000000", "tileId": 0 }
  ]
}
```

IDs above are schematic: use **ground server IDs from the loaded OTB**, not
sprite/client IDs. Every positive ID must exist and be a ground. `0` skips;
unmapped colors in converter exports skip too. Black can be explicitly mapped
to a positive ID. Legacy brush legends keep their default brush and historical
unmapped near-black skip behavior. An `entries` legend can also use `tileId`
instead of `brush` on an individual entry. Duplicate colors, malformed values
and invalid explicit paths fail instead of silently choosing a default legend.

Mask resizing samples the nearest source category without interpolation.
Prompt + image composition uses the generation seed to choose whole pixels
from either mask: the percentage is the **procedural** contribution. It never
invents intermediate RGB colors and copies the chosen pixel's alpha.

Exact IDs apply to image and flat prompt/reference masks. Algorithmic mountain
and deep-cave generation still uses named client brushes. Automatic borders,
randomization, walls and doodads remain optional editor operations; they can
affect existing tiles within the target rectangle, including skipped pixels.
For a raw color-to-ground import, disable these operations. Doodads may extend
beyond their anchor; footprint-safe placement is a following delivery.

## Ordered next deliveries

1. **Multi-biome recipes:** named regions, coherent masks for terrain/water/
   settlements, deterministic streams and palette coverage validation.
2. **Cave topology:** connect walkable components, select landings from generated
   data, place client-configured floor-change items and validate reachability.
3. **City chunks:** OTBM templates with bounded footprint, anchors, street/door
   connectors, collision policy, generic examples and protected existing tiles.
4. **Multi-level structures:** stack chunks using explicit relative floor
   offsets and stairs/ramps/holes. Validate entrances, landings and paths across
   floors before committing a single undo transaction.
5. **Production checks:** deterministic output, undo/redo, compatible OTBM reload
   and client-specific visual/in-game QA. LLM remains an optional planning aid.

Do not mark the wider roadmap complete when only the PNG foundation lands.

## Automated checks

The independent CMake test project exercises the production mask and legend
helpers and compiles the changed editor integration files. On Windows with
MSVC and a populated vcpkg installation:

```powershell
cmake -S tests -B build/procedural-tests -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build/procedural-tests --config Release --parallel 2
# Make the installed wxWidgets DLL directory available on PATH before ctest.
ctest --test-dir build/procedural-tests -C Release --output-on-failure
```

Tests cover converter exports, legacy legends, invalid/duplicate colors and
IDs, alpha, color keys, non-integer resizing, palette preservation, composition
weights and reproducibility. They do not launch the editor or verify saved
OTBM output, active-client painting, borders, undo/redo or in-game travel.

Local verification: MSVC 19.37 / wxWidgets 3.2.4, Release x64; integration
translation units compiled and CTest passed (1/1). A GitHub Actions workflow
also runs the standalone test project on Ubuntu for pull requests.
