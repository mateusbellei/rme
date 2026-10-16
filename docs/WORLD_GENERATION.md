# World generation

Tracking: https://github.com/mateusbellei/rme/issues/5

## Semantic planner

`WorldGen::Build` creates a client-independent `Plan` before any map editing.
The plan stores normalized elevation, temperature and moisture; terrain classes;
reserved footprints; building walls/entrances; and explicit vertical links.
Floor offsets follow Tibia convention: negative is above, positive is below.
Upper floors outside building footprints are void, never ocean tiles.

Stages use fixed integer hashes derived from the world seed instead of the
editor's global random generator. The terrain is sampled at multiple scales;
priority-flood drainage produces an acyclic graph with boundary/ocean outlets.
Runoff accumulation chooses river sources, with depressions classified as lakes.
Rivers continue through lake outlets. Climate and distance from water classify
coherent biomes. Settlement sites require dry, relatively flat ground.

Weighted pathfinding connects settlement plazas and building entrances while
avoiding mountains and ocean. Rivers crossed by roads become explicit bridge
categories. Building lots reserve setbacks and keep their entrances open.
Independent cellular-automata caves are joined using a multi-source frontier;
the enclosing wall stays sealed. Adjacent floors use reserved landings and
distinct connection tiles, including intermediate floors.

`WorldGen::Validate` checks layer sizes, cave connectivity, building entrances,
and vertical endpoint bounds, walkability and overlap. It validates the semantic
plan, not client item behavior. The editor integration must separately resolve
materials, collisions and actual travel items before painting any tiles.

Settings cap the surface at 4,194,304 cells and total layers at 8,388,608 cells.
This is a planning budget, not a recommendation to commit millions of editor
tiles in one operation. Preview and planning can be used without client assets.

## Standalone build and diagnostics

```powershell
cmake -S tests -B build/planner -DRME_TEST_EDITOR_INTEGRATION=OFF
cmake --build build/planner --config Release
ctest --test-dir build/planner -C Release --output-on-failure
build/planner/Release/rme_world_plan.exe build/demo 1337 256
```

The CLI writes one PPM image per floor and a JSON summary. It does not create an
OTBM or prove in-game navigation. Palette colors represent terrain, dark gray
marks walls, magenta marks doors and yellow marks vertical connections.

The independent tests verify drainage outlets, deterministic plans, multiple
seeds and land shapes, connected cave floors, landing validity and input limits.

## Delivery stages

1. Semantic world planning and standalone regression checks.
2. Editor integration, explicit travel objects, collision protection and atomic
   undo; world recipe settings and an inspectable preview.
3. Reusable construction templates, richer districts/props, diagnostics and
   practical client validation.

Existing legacy presets remain supported. This planner is a new opt-in path;
it does not silently replace image-mask or legacy cave generation.
