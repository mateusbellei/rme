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

## Editor workflow

Open **File → Generate Map**, choose **World planner**, then **World settings**.
Set the land shape, sea level, river/settlement counts, building floors and cave
depth. Choose a region within the current map dimensions or use a single-floor
selection. The selection defines a bounding rectangle; irregular selection
shapes are not a clipping mask. **Preview world** displays terrain, elevation,
moisture and temperature, and allows inspecting each floor before applying.

The default `world_legend.json` maps every semantic category to generic brush
names. These must exist in the loaded client; customize names or use explicit
ground server IDs. Missing categories fail rather than guessing. Materials,
autoborders and walls must preserve the plan's walkable routes and solid barriers.

Multi-floor travel currently uses pairs of non-blocking teleport items with
explicit OTBM destinations. Arrival tiles are next to the reciprocal portal,
not on it, avoiding automatic teleport loops. The server ID can be configured;
0 selects the first suitable type in the loaded client. This is a functional
travel representation, not a claim that physical stairs/ramps are generated.
Actual server behavior still needs client-specific in-game QA.

**Protect existing tiles** defaults on and rejects the operation if any affected
tile is occupied. Turning it off allows replacing ordinary ground/items.
House tiles, spawns, creatures, zones, map flags, unique items, house exits,
waypoints and town exits remain protected. Void cells on upper floors are not
affected. Live-client generation is rejected to prevent visibility clipping.

The adapter builds a temporary map, processes borders/walls against final
neighbors, adds travel objects and checks material walkability before touching
the editor map. One undo batch replaces each affected tile exactly once. Ground
and wall brush randomness uses per-cell scoped seeds that restore the global
editor RNG. No partially prepared result is committed on validation failure.

Application is limited to 1,048,576 non-void tiles; larger semantic plans remain
available through the standalone planner. Recipe version 4 saves world options,
target origins and the complete uint32 seed; existing version-3 recipes remain
loadable. `example_continent.json` and `example_world_layers.json` are generic
starting points. The default world path currently does not scatter doodads;
that is a following delivery with placement and route constraints.

## Delivery stages

1. Semantic world planning and standalone regression checks.
2. Editor integration, explicit travel objects, collision protection and atomic
   undo; world recipe settings and an inspectable preview.
3. Reusable construction templates, richer districts/props, diagnostics and
   practical client validation.

Existing legacy presets remain supported. This planner is a new opt-in path;
it does not silently replace image-mask or legacy cave generation.
