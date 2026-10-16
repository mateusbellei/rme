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

World buildings always require a blocking wall brush, independently of the
legacy wall-placement toggle. Set buildings per settlement to zero to generate
terrain without buildings. Unplanned floor-change items from material/border
rules are rejected before application.

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
starting points. Enable **Doodads** to decorate terrain using brushes from the
active client and keyword groups in `biome_doodads.json`.

## Delivery stages

1. Semantic world planning and standalone regression checks.
2. Editor integration, explicit travel objects, collision protection and atomic
   undo; world recipe settings and an inspectable preview.
3. Reusable construction templates, richer districts/props, diagnostics and
   practical client validation.

Existing legacy presets remain supported. This planner is a new opt-in path;
it does not silently replace image-mask or legacy cave generation.

## Actual editor/OTBM regression

The optional `RME_ENABLE_WORLD_SELF_TEST` CMake build enables
`rme --world-self-test <output-directory>` and a CTest entry. This path starts
without the main window, asset discovery, user settings writes or private client
files. It supplies synthetic item/wall definitions and uses the production
`Editor`, action queue, map, wall brushes and OTBM reader/writer.

The regression applies a world with buildings and two underground floors,
checks portal destinations and adjacent walkable arrivals, verifies one undo
and exact redo, tests collision/material rejection without mutation, preserves
the global brush RNG, and compares the reloaded OTBM content to the generated
map. It also retains an unrelated tile outside the generated area.

```powershell
cmake -S . -B build/editor -DRME_ENABLE_WORLD_SELF_TEST=ON
cmake --build build/editor --config Release
ctest --test-dir build/editor -C Release --output-on-failure --timeout 90
```

The full editor requires wxWidgets, Boost thread/system, OpenGL/GLUT, zlib,
libarchive and GLM. On Windows, use the existing dependency toolchain and put
its runtime DLL directory on PATH. On Linux, run the editor test under Xvfb;
the application creates no main window in self-test mode. The test command is
disabled in normal builds. Passing it does not replace actual game-server QA
or visual verification with a consumer's client assets.

The semantic validator also checks the component graph across all floors:
every upper/underground component must have a route to a surface component,
and building doors must connect to their own settlement plaza. A settings
matrix exercises 150 combinations of dimensions, climate, shape and floors.

## Construction catalogs

`data/procedural/buildings.json` contains weighted, reusable construction
blueprints. The same generic templates are compiled into the standalone
planner as a fallback. In World settings, select a custom construction catalog
to replace that set. Empty/default paths use the bundled catalog when available.

Each version-1 catalog contains a `templates` array. Templates declare a unique
`id`, `weight`, `maxLevels`, `rotate`, rectangular `rows`, optional `upperRows`,
and optional allowed `biomes` (grass, forest, desert, snow or beach). Symbols:

| Symbol | Meaning |
| --- | --- |
| `#` | Wall |
| `.` | Interior floor |
| `+` | Ground-floor entrance facing the street |
| `a`, `b` | Alternating travel anchors for multi-level buildings |
| `,` | Courtyard grass on the base floor |
| space | Void/no construction tile |

Templates must be 5..32 cells per dimension, contain one street entrance,
connect all rooms to that entrance, and provide safe, aligned `a`/`b` landings
for multiple floors. Upper floors must have matching dimensions and no outer
entrance. If `upperRows` is omitted, the base outline repeats with its outer
door closed and courtyard grass left as void. Doors are rotated toward the
settlement center where rotation is allowed; access paths route to the actual
oriented door. Per-template height limits can create varied building heights.

Invalid catalogs fail before map editing. Limits are 64 templates and 2 MiB
of JSON. The catalog is a semantic footprint format; it does not yet import
arbitrary OTBM chunks or rotate client item IDs. Client materials still come
from the world legend and the chosen wall brush. Custom paths in recipes and
JSON inputs support Unicode filenames.


## Biome decoration

World decoration uses deterministic patch densities and the active client's
single/composite doodad brushes. Forests are denser than grassland; desert,
snow, beach and caves use separate keyword groups. Missing brush groups are
reported in the generation status instead of silently substituting items.

Every placement checks all composite tiles and sprite dimensions, plus a
one-tile clearance. Decorations stay within one habitat, avoid existing props,
roads, building setbacks, doors and travel landings, and cannot block a narrow
passage or enclose a walkable pocket. A final validation checks actual blocking
props against the complete multi-floor navigation graph before committing.
Grounds, movement-changing items, containers, walls, quest attributes and
multi-floor composites are excluded from the decoration pass. This is outdoor
and cave dressing; furnished room templates are a separate feature.

The pure decoration suite checks bounds, footprints, route protection and
adversarial enclosing layouts. The production editor regression exercises
blocking/nonblocking single brushes and multi-tile composites, including
repeatability, one undo/redo and OTBM round-trip.


## PNG-guided worlds

World settings accepts two optional PNGs. **Heightmap** uses grayscale 0..255
as normalized elevation 0..10000, where sea level controls the shoreline.
Fully transparent pixels use the seeded procedural elevation. The outermost
region boundary always remains ocean, providing drainage outlets. The planner
then computes drainage, climate, settlements and caves on that authored shape.

**Biome hints** uses exact colors from the table below. Transparent pixels keep
the automatic biome. Hints replace dry natural biomes after hydrology; ocean,
lakes and rivers take precedence, and towns/roads can subsequently use dry land.
Use a heightmap to author coastline and water placement; a biome hint does not
raise land from the sea. Both inputs resample with nearest-neighbor to keep
categorical boundaries and allow a small painted map to guide a larger world.

Partial transparency, colored heightmaps and unknown hint colors fail with the
source pixel location. Validation covers every source pixel, including those
that would disappear when downsampling. PNG headers are checked before decode;
inputs are capped at 16 million source pixels and 64 MiB. Recipe fields are
`world.heightmap` and `world.biomeHints`. Empty paths retain fully seeded worlds.


| Biome | Exact RGB |
| --- | --- |
| Beach | `#FFD700` |
| Grass | `#00FF00` |
| Forest | `#008000` |
| Desert | `#E8C060` |
| Snow | `#F0F8FF` |
| Mountain | `#654321` |


## Progress and cancellation

Planning and editor preparation expose progress callbacks. The dialog shows
modal stage progress with cancellation while shaping terrain, routing roads,
creating caves, resolving client materials and placing props. Cancellation
before the final commit discards the temporary result without changing map
content, undo history or global brush RNG. The final transfer commits as one
undo operation. Automated checks cancel every planning/editor stage, including
the last checkpoint immediately before commit. Settings scroll and previews
scale to the current monitor's usable area.
