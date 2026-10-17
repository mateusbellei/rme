# World generation

Tracking: https://github.com/mateusbellei/rme/issues/5

## Quick start

1. Load a matching client and open an empty map of at least 512×512 at surface
   z=7. For the bundled furniture example, use the 10.98 materials/client.
2. Choose **File → Generate Map… → Load recipe…** and open
   `data/procedural/recipes/example_furnished_city_1098.json`. For terrain-first
   exploration with your own legend, try `example_ridged_continent.json`.
3. Choose **Preview world…**. Inspect the floor/terrain/climate/role views, compare
   seeds, and adjust **World settings…** from the main dialog as needed. Use
   **Fit**, **100%**, **200%** or **400%** and the scroll bars to inspect details.
   Point at a tile to see its map coordinates, terrain and normalized climate.
4. Choose **Apply displayed world**. One undo restores the previous map; save the
   result as OTBM normally. Existing tiles are protected by default.

For a complete batch bundle, use the command-line section below. The large
1024×1024 example requires an explicit larger tile budget and is intended for
batch export; the editor's default application limit remains one million tiles.
The client-independent planner can be built from `tests/` without game assets.

The preview reports the total non-void tile count across every floor. If it
exceeds the editor's 1,048,576-tile budget, **Apply displayed world** is disabled;
seed comparison and diagnostics export remain available. Reduce the region or
floor count, or use the explicit larger budget in the 64-bit batch exporter.

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
House tiles, spawns, creatures, zones, map flags, unique/action IDs on grounds or items, house exits,
waypoints and town exits remain protected. Void cells on upper floors are not
affected. Live-client generation is rejected to prevent visibility clipping.

The adapter builds a temporary map, processes borders/walls against final
neighbors, adds travel objects and checks material walkability before touching
the editor map. One undo batch replaces each affected tile exactly once. Ground
and wall brush randomness uses per-cell scoped seeds that restore the global
editor RNG. No partially prepared result is committed on validation failure.

Editor application defaults to 1,048,576 non-void tiles; larger semantic plans remain
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
matrix exercises 300 combinations of dimensions, climate, shape, floors and
both settlement-distribution modes, with required successful multi-town coverage.

Validation also cross-checks settlement ownership lists, source biomes,
construction dimensions/entrances/rotation/height against the catalog, footprint
setbacks, and one-to-one connection markers versus explicit travel endpoints.
Applying a plan with a different painting seed is rejected before staging, so
the displayed seed cannot silently produce different material variants.

## Construction catalogs

`data/procedural/buildings.json` contains weighted, reusable construction
blueprints. The same generic templates are compiled into the standalone
planner as a fallback. In World settings, select a custom construction catalog
to replace that set. Empty/default paths use the bundled catalog when available.

Each version-1 catalog contains a `templates` array. Templates declare a unique
`id`, `weight`, `maxLevels`, `rotate`, rectangular `rows`, optional `upperRows` or
`upperFloors`,
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
entrance. If both upper definitions are omitted, the base outline repeats with its outer
door closed and courtyard grass left as void. Doors are rotated toward the
settlement center where rotation is allowed; access paths route to the actual
oriented door. Per-template height limits can create varied building heights.

`upperRows` repeats a single upper layout. For a different layout at every
height, use `upperFloors`: an array of row arrays for levels 1 through
`maxLevels-1` (one to three entries). Supply every upper level and omit
`upperRows`. Each array has the same rectangular dimensions as the base; spaces
can inset a tower, leave a courtyard open or remove an entire corner from one
level. All rooms must remain connected and both `a`/`b` anchors must align with
the base. Furniture anchors are checked against their actual floor's layout.
Rotation transforms every floor and its furniture together.

`example_layered_town.json` uses `layered_buildings.json`: three-floor buildings
with interior partitions on the middle floor and a narrower 9×9 top inside a
13×13 footprint. The CLI regression exports and byte-verifies the OTBM, checks
all vertical routes, and compares planned per-floor tile counts with the actual
export. `plan.json` now records both raster `cells` and non-void `tiles` per
layer. These plans still use the configured floor/wall materials and teleport
bindings; they do not infer a client's roof or physical stair sprites.

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
and cave dressing; template interiors use the separate authored furniture pass.

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

### Export a world for PNG editing

The preview's **Export plan / editable PNGs…** writes an additional grayscale
`<prefix>_heightmap.png`, exact-palette `<prefix>_biomes.png`, and relative
`<prefix>_editable-recipe.json` alongside the diagnostics. Edit the PNGs in an
image editor, then load the editable recipe to generate a new world.

Batch export offers the same workflow with `--editable-inputs`: the bundle gains
`inputs_heightmap.png`, `inputs_biomes.png` and `editable-recipe.json`, with their
filenames listed in `generation.json`. This works with `--no-previews`; the two
editable inputs are separate from the diagnostic views. Existing outputs remain
protected by the new-directory requirement.

The heightmap uses nearest 8-bit grayscale (at most 20 units of quantization
error on the normalized 0..10000 elevation scale). Biome hints retain the six
natural categories exactly; water, roads and construction cells become fully
transparent so the next run can plan those features normally. Do not paint with
partial alpha or antialias categorical edges.

These files are a starting point for authoring, not an exact replay: quantized
elevation, transparent infrastructure and recomputed hydrology can change the
next plan. Authored height pixels also take priority over procedural ridge
breaching. Keep the bundle's original `recipe.json` and unchanged original
inputs for deterministic replay. The editable recipe is deliberately separate.


## Progress and cancellation

Planning and editor preparation expose progress callbacks. The dialog shows
modal stage progress with cancellation while shaping terrain, routing roads,
creating caves, resolving client materials and placing props. Cancellation
before the final commit discards the temporary result without changing map
content, undo history or global brush RNG. The final transfer commits as one
undo operation. Automated checks cancel every planning/editor stage, including
the last checkpoint immediately before commit. Settings scroll and previews
scale to the current monitor's usable area.


## Villages and city blocks

World settings offers **Organic village lots** (radial growth and access paths)
and **City street blocks** (compact lots with frontage and streets toward the
central plaza). Block pitch adapts to the largest eligible template and road
width, preserving building setbacks. Lots belong to the nearest settlement, so
neighboring cities do not consume each other's territory. Terrain constraints
remain active: blocked or steep lots are skipped with a warning instead of
flattening water and mountains.

Template orientation is selected from the actual source entrance, including
custom templates whose original door faces north/east/west. Paths target the
real rotated door. Road routing uses deterministic A* and reuses stamped search
buffers across lots, avoiding a full-world buffer clear for every building.

The independent regression creates a city with at least 24 buildings on a dry
plain, validates every entrance and upper-floor connection, checks setbacks,
and repeats it with north-facing source templates. The diagnostic CLI also
accepts optional layout and building count:

```powershell
build/planner/Release/rme_world_plan.exe build/city 1337 512 blocks 32
```

`example_city_blocks.json` is an editor recipe. Counts are targets; constrained
terrain may fit fewer lots. These layouts do not yet assign civic/commercial
functions automatically. Interiors can be authored in the construction catalog.


## Authored interiors

A template may include `objects`, each anchored at an interior `.` cell:

```json
"objects": [
  {"x":3,"y":3,"floor":0,"brush":"Exact single/composite doodad brush name"},
  {"x":3,"y":3,"floor":1,"required":true,
   "brushes":["Facing north","Facing east","Facing south","Facing west"]}
]
```

Coordinates refer to the unrotated template. `floor:0` is the base, `floor:1`
is one floor above, and so on. Use either one orientation-independent `brush`
or four clockwise `brushes`. Positions and brush orientation rotate with the
building. Cyan preview pixels mark authored anchors, not resolved client sprites. Optional
placements may still be skipped when their client brushes do not fit.
Names refer exactly to doodad brushes loaded in the active client;
no generic item-ID rotation is guessed. Adjacent objects such as a bed or a
furniture set should be authored as one composite brush.

Furniture stays inside the building's walkable interior with one-tile clearance,
avoids walls, doors, portals and arrival tiles, and uses the same bounded
physical/visual footprint validation as outdoor props. Containers, quest items,
and movement-changing items are not accepted in this decoration stage. Optional
objects that cannot fit or whose brushes are missing are skipped with a warning.
A `required:true` object rejects the complete staged application if it cannot be
placed. Objects on floors above the chosen building height are omitted.

Limits: 128 objects per template, distinct anchors, one or four brush names
of up to 128 bytes each. The production regression loads a JSON catalog with
required furniture on two floors and checks rotated server IDs, rollback for a
missing required brush, RNG isolation, undo/redo and OTBM persistence for both
village and city scenarios. The generic bundled catalog intentionally has no
client-specific furniture names.


## Coastlines, rivers and climate

Coast roughness (0..100) controls a deterministic domain warp of continent and
island outlines, making coves and peninsulas adjustable while keeping ocean
boundaries. Heightmap pixels override this elevation field where opaque.
Temperature and moisture adjustments (-5000..5000) shift climate before biome
classification, allowing colder, hotter, drier or wetter world profiles.
Explicit biome hints retain precedence on dry land.

River width uses odd maximum diameters 1, 3, 5 or 7. Runoff widens downstream
channels after centerline drainage is established. The drainage graph stays
acyclic; rivers remain connected to their centerlines and the ocean boundary
is preserved. City site scoring also considers dry land across the expected
district area, reducing cities placed on narrow peninsulas that cannot fit their
requested lots. Terrain constraints can still reduce the final building count.


## Reproduction and diagnostic export

**Export plan diagnostics** in the preview saves a versioned JSON manifest,
terrain PPMs and PNGs for terrain/elevation/moisture/temperature on every floor.
These are semantic diagnostics, not screenshots of client sprites or a substitute
for saving the generated map as OTBM. The standalone planner uses the same
manifest/PPM exporter without requiring client assets.

Manifests record algorithm version, complete settings, a 64-bit fingerprint,
terrain/component counts, settlement/building coordinates, template IDs,
rotations, travel endpoints/arrivals and warnings. Fingerprints include settings,
catalog definitions and furniture bindings, drainage, semantic cells and links.
The value is encoded as a string to avoid loss of precision in JSON consumers.
It is a diagnostic identity, not a cryptographic signature or an OTBM hash.

Keep the recipe, original PNG inputs, catalog, client material definitions and
matching application version to reproduce a world. Version-1 fingerprints are
being developed in this PR; changes during development can alter worlds. Future
released algorithm changes must bump the version. New recipes identify the
algorithm and unsupported versions are rejected. Legacy recipes without that
field remain supported.


## Recipe asset paths

The dialog saves recipes with `"pathBase":"recipe"`. Image, legend, heightmap,
biome-hint and catalog paths are written relative to the recipe directory when
possible and resolved from that directory on load. A folder containing a recipe
and neighboring assets can be moved together. Assets are referenced, not copied;
keep external relative paths valid when moving a larger folder tree. References
to another Windows volume remain absolute.

Recipes without `pathBase` retain their historical working-directory behavior;
`"pathBase":"workingDirectory"` can state it explicitly. The C++ save API keeps
that behavior by default and offers `PathMode::RelativeToRecipe` for portable
folder layouts. Saves use a temporary file and only replace an existing recipe
after the full JSON has been written. Inputs are capped at 4 MiB; malformed
field types, oversized integers, invalid densities and embedded-NUL paths are
rejected without changing the caller's configuration.


## Batch OTBM generation

The application accepts a batch command before loading the editor window, user
preferences or single-instance state:

```powershell
rme --generate-world recipe.json items.otb items.xml materials.xml NEW_OUTPUT_DIRECTORY --data-dir path/to/rme/data | Out-Host
```

Use a `source:world` recipe with an explicit region and `useSelection:false`.
The command loads the specified OTB item registry, item XML metadata and material
brushes, runs the same planner and staged editor adapter, and writes:

- `world.otbm`, `world-spawns.xml`, `world-houses.xml` using production serializers;
- `plan.json` and per-floor PPM diagnostics;
- `preview_z<map-z>_{terrain,elevation,moisture,temperature,roles}.png` diagnostics;
- `recipe.json` with references resolved relative to its new location;
- `generation.json` with client registry version, actual placement counts and warnings.

Use `--seed UINT32` to override the recipe seed without editing the input file.
The value must be decimal, between 0 and 4294967295, and may be specified once.
The exported `recipe.json`, `plan.json` and `generation.json` all record the
effective seed. Regenerating from the exported recipe uses that seed without
requiring the option again. With identical client data, materials, inputs and
options, the generated OTBM is reproducible; the CLI regression checks byte
equality after replaying the exported recipe, including the uint32 boundaries.

Wall-name resolution prefers an exact match, then the shortest partial match,
matching ground-brush behavior and avoiding an earlier decorative name.

The output directory must be new and its parent must exist. Existing output is
rejected before client loading; repeated requests never replace a previous
world. A nonzero exit means the bundle did not finish; an I/O failure after
writing starts can leave partial artifacts in the newly created folder.
Referenced PNGs/catalogs/client files are not copied into the output.

`--data-dir` locates shared procedural defaults and `clients.xml`; conventional
`data/<version>/materials.xml` layouts can infer that data root. Optional
`--client-dat path/to/Tibia.dat` loads sprite dimensions after verifying its
signature against the OTB version in `clients.xml`. Without DAT metadata the
command checks physical blocking and composite coordinates and reports that
visual sprite extents were unavailable. No sprite images are required to encode
OTBM. This is not a game-server playtest.

`rme --generate-world --help` prints usage. On Linux the wx application still
needs a display, so use `xvfb-run -a rme ...` on a build server. Registry loading
receives the explicit client version, including modern item IDs; the maximum
16-bit server ID no longer wraps the XML range loop. The full editor regression
also exercises OTB/XML/material/relative-recipe loading, writes and reloads the
batch bundle, validates travel destinations, and rejects existing output.


## Furnished 10.98 example

`data/procedural/recipes/example_furnished_city_1098.json` uses the public
10.98 item registry and materials already included in this repository. Its
relative catalog supplies residences, inns and workshops with beds, chairs and
stone tables on three floors. The 10.98 materials load explicit clockwise
furniture aliases from `procedural_furniture.xml`; this recipe is therefore
usable both in the editor with that client loaded and through the batch command:

```powershell
rme --generate-world data/procedural/recipes/example_furnished_city_1098.json data/1098/items_original1098.otb data/1098/items_original1098.xml data/1098/materials.xml build/furnished-world --data-dir data | Out-Host
```

The default seed creates two settlements with 24 buildings and 216 required
furniture composites, including rotated beds and 2×2 tables. Three building
floors and two cave levels produce approximately 792,000 non-void tiles, within the application
limit. Furniture count means accepted composite placements, rather than item
count. These IDs and orientations are specific to the bundled 10.98 data;
other clients need their own bindings. No DAT/SPR assets are bundled with this
profile, and its physical/serialization regression does not establish in-game
or visual QA.

The external CLI regression runs from another working directory, checks the
relative recipe assets and populated output, and hashes every artifact before
verifying that a repeated request refuses to replace it.


Optional client metadata loading uses the explicit data directory's
`clients.xml`, even if another copy exists beside the executable. DAT parsing
rejects truncated records, missing flag terminators, zero/oversized sprite
products and invalid animation settings before using those values. Its sprite
ID loop handles the maximum 16-bit item ID without wrapping. Regression data
is synthetic and contains dimensions only; it requires no game sprites.


## Construction materials by template

Catalog templates can bind exact client ground/wall brushes in addition to their
furniture. For example, a desert-only house can use sandstone floors and an
Egyptian wall brush while a forest residence uses wooden floors and walls:

```json
{"id":"desert_house", "biomes":["desert"], "materials":{"floor":"sandstone","wall":"egypt stone wall"}}
```

Add these fields to a complete valid template with rows and landings. Omitted
floor/wall bindings inherit the world legend and wall setting. Explicit names
must match a loaded brush of the correct type exactly; missing names reject
preparation instead of silently changing style. Only templates actually used
by the plan need their materials available. The same bindings apply on upper
floors, preserve courtyard terrain, and pass the physical/navigation checks
before the map changes. A global wall setting may be empty if every placed
template supplies a wall. The 10.98 furnished profile demonstrates wooden
residences, marble-floored inns and stone-floored brick workshops.

To change materials by height, add `floorMaterials`, an array starting at the
base floor and containing at most `maxLevels` objects. Each object can override
`floor`, `wall` or both; `{}` and omitted keys inherit the template bindings,
then the world settings. For example:

```json
"materials": {"floor":"stone floor", "wall":"stone wall"},
"floorMaterials": [{}, {"floor":"wooden floor", "wall":"wooden wall"}]
```

This keeps the base and later unspecified floors in stone while making the
first upper floor wooden. Explicit names are resolved only for floors that the
plan actually uses; missing/wrong-type bindings on a used floor fail before any
editor changes. Errors include the template ID and relative floor (0 is base).
`example_layered_town_1098.json` combines independent floor layouts with stone,
wood and marble floors plus different wall brushes from the public 10.98 data.
Per-floor styles participate in the plan fingerprint, undo and OTBM verification.


## Settlement composition

`world.diverseSettlements:true` (World settings: **Prefer different biomes**)
prefers suitable sites in different habitats before repeating one. The first
pass takes the fittest separated site for each available biome; the second
fills remaining settlements in the normal fitness order. Both passes retain
dry/flat-site and spacing requirements. This is a preference rather than a
promise that every biome or requested town will fit. With only one suitable
habitat, remaining towns still use it.

The default is false, preserving the previous placement and fingerprint. The
setting is saved in recipes/diagnostics and works with PNG biome hints and
biome-specific construction templates. Try
`example_diverse_continent_1098.json` for a ridged continent with four settlements
and regional furniture/material bindings. Regression tests use a flat PNG and
four habitat quadrants to verify grass, forest, desert and snow towns together,
including their actual regional buildings in a byte-verified OTBM.

The bundled diverse profile's default seed was also exported with the public
10.98 client data: 794,572 tiles, 32 buildings and 288 furniture placements, with
one settlement in each of grass, desert, forest and snow. Its OTBM passed
read/re-encode verification, and the bundle included editable PNGs.

![Semantic terrain preview of the diverse continent](images/world-diverse-continent.png)

Semantic preview exported by that recipe with seed 1337; colors identify terrain
and construction footprints. This is a planner diagnostic, not a client-sprite
render. Ocean is blue, lakes/rivers lighter blue, ridges brown, desert tan and
snow white; four settlement clusters occupy different habitats.

A blueprint's optional `role` is a short identifier such as `residential`,
`commercial`, `industrial` or `civic`. It labels the plan and role preview; it
does not create NPCs, house ownership or server scripts. `maxPerSettlement`
limits how often that template can appear in each settlement (0 means unlimited,
1–64 are hard caps). Selection retains biome filtering and weighted random
choice among templates that have not reached their caps. Limits reset per town.
If all eligible templates are exhausted or terrain does not fit, the planner
reports fewer buildings instead of breaking a cap.

`minPerSettlement` (default 0, range 0..64) reserves a required construction mix.
For example, `"minPerSettlement":1,"maxPerSettlement":1` requests exactly one
inn per generated settlement in that template's allowed biomes. Templates with
unmet minimums receive the first lots; weighted optional selection resumes once
all minimums are met. Minimums may not exceed a nonzero maximum. Their sum must
fit the recipe's per-settlement building budget.

These are hard requirements for each settlement actually generated: if terrain
or access roads prevent a minimum from fitting, planning fails with the template
ID and achieved/required counts. No partial plan is applied or exported. This
does not promise that every requested settlement site will fit. The independent
validator checks minimums again, and `plan.json` records each settlement's
`constructionMix` with actual counts, minimums and maximums (0 means unlimited).
Catalogs without minimums retain the existing placement and fingerprints.

Try `example_balanced_city.json` with `civic_buildings.json`: each settlement
requires four houses, one inn and one courtyard hall before filling the remaining
lots. It uses the normal world legend/client materials; it does not create NPCs,
house ownership or server-side functions for those roles.

The furnished 10.98 catalog permits at most one inn and two workshops per town,
leaving the remaining lots for residences. The **Construction roles** preview
colors interiors by role, retains wall/entrance/travel markers, and lists role
counts and colors below the image. Diagnostic building records include the role;
PNG export includes an additional roles view per floor. Both role and quota
bindings contribute to the plan fingerprint.


Recipe, construction-catalog, legend and biome-decoration JSON files accept UTF-8
with or without a byte-order marker. Loading consumes one complete document;
trailing documents/text and invalid raw UTF-8 fail. Input limits are 4 MiB for
recipes/legends, 2 MiB for catalogs and 1 MiB for biome decoration settings.
Invalid recipe/catalog loads preserve the caller's configuration. Wall brush
names also reject embedded NUL instead of being silently truncated on saving.


## Comparing seeds in the editor

The world preview provides **Previous seed**, **Next seed** and **Preview seed**
controls. Adjacent seeds wrap across the full uint32 range. Regeneration retains
the current settings, target region, selected floor when available and preview
view. It replaces the displayed plan only when planning succeeds; cancellation
or input failure leaves the previous plan inspectable. The summary identifies
the displayed seed and fingerprint, and **Apply displayed world** always applies
that exact plan. Successful previews update the main dialog's seed so a saved
recipe can reproduce the chosen candidate.


Batch output includes the same five semantic preview views as the editor, with
actual target z values in PNG filenames. `generation.json` lists their relative
filenames. Transparent cells preserve absent upper-floor terrain. These images
show the plan and authored furniture anchors, rather than client sprite renders.
Pass `--no-previews` to omit PNGs while retaining OTBM, XML and PPM/JSON outputs.
Each PNG is written through a temporary file and committed after encoding.


## Mountain ridges and local biome variation

`world.relief:"ridges"` adds warped mountain chains inside the continental
envelope, lowering broad highlands into valleys while retaining the existing
coastline envelope. A bounded basin-breaching pass lowers spill channels of deep,
high-runoff basins, then recomputes the acyclic drainage/flow graph before lakes
and rivers. It is a deterministic terrain heuristic, not a physical erosion
simulation. Supplied heightmap elevations retain priority: breaching is disabled
when any authored height pixel is present; an entirely transparent heightmap
still uses procedural relief normally.

`world.biomeDetail` (0–100) adds coherent local humidity/temperature variation on
top of regional climate. It creates smaller biome patches without randomizing
individual tiles. Defaults `relief:"smooth"` and `biomeDetail:0` preserve existing
recipes. Both settings are editable in World settings, recorded in recipes and
diagnostics, and included in the plan fingerprint. Try the generic
`example_ridged_continent.json`; the standalone planner also accepts optional
`[smooth|ridges] [biome detail 0-100]` arguments after its building count.


## Distributed cave entrances

`world.caveEntrances` requests 1–16 separated surface access sites when cave
depth is nonzero. The first remains at the main settlement (or nearest suitable
central dry landing). Additional sites prefer mountain foothills and require a
clear natural surface patch outside roads, buildings and existing reservations.
The first cave floor receives corresponding safe landing rooms; its connector
joins any newly carved room to the underground component. Every link has a
walkable arrival beside its return portal. The setting uses the same paired
teleport travel as other world levels; physical holes/stairs need client-specific
travel support.

Sites and arrivals are reserved before decoration. If the requested separation
cannot fit, the plan reports fewer entrances. Default 1 preserves earlier
recipes. `example_ridged_continent.json` requests six entrances; the world
settings dialog, recipes and diagnostic manifest expose the count. Editor tests
exercise three entrances through cancellation, undo/redo and actual OTBM reload;
the stock 10.98 CLI also exercises a ridged world with four entrances.


The 10.98 catalog also demonstrates regional architecture. Temperate/coastal
settlements use wooden residences; desert residences bind sandstone floors and
Egyptian stone walls, while snowy residences use stone floors/walls. All variants
share explicit rotated furniture and the residential role. Selection uses the
settlement's center biome, keeping a town's style coherent across its lots.
External regressions supply real categorical biome PNGs to force desert/snow
settlements and validate those client materials, furniture and cave links.


## Optional server town records in batch output

Add `--town-prefix "Frontier"` to the batch command to register a town per planned
settlement, named `Frontier 1`, `Frontier 2`, etc., with deterministic IDs. Temple
positions use a physically walkable plaza cell and avoid the central cave portal
and movement-changing items. The OTBM contains real town nodes; `generation.json`
records the names, IDs, prefix and target coordinates. A blank/omitted prefix
keeps town registration disabled. Prefixes support printable UTF-8 up to 64 bytes.

This is an export option for the newly generated batch map. The saved recipe
still describes geometry/materials; repeat the prefix option to reproduce town
metadata. The editor preview/apply path keeps its existing single tile undo
transaction. A town record provides a server arrival position; temple artwork,
NPCs, houses, ownership and server scripts are separate authoring tasks.


## Explicit batch budgets for larger continents

`--tile-budget MAX_TILES` changes the allowed number of non-void tiles for a batch
export. The editor and default batch limit remain 1,048,576. A 64-bit build can
explicitly allow up to 4,194,304 tiles, covering a 1024×1024 surface with two full
cave levels plus building floors. A 32-bit build retains the default maximum.
The semantic planner's separate 8,388,608-cell total limit still applies.

The count includes ocean and every populated underground/upper-floor cell.
Preflight rejects over-budget worlds before staging tiles or changing undo state.
`generation.json` records the chosen budget. Larger exports retain the same
material, collision, navigation and serialization checks and require more memory
because the staged world and undo transaction coexist until export completes.


A local Windows x64 Release run with the bundled 10.98 registry/materials exported
an optional-ridged 1024×1024 continent, two cave levels and three-floor buildings:
3,161,744 tiles, 64 buildings, four registered towns, 576 furniture placements,
79,193 biome doodads and 25 preview PNGs. With `--verify`, observed elapsed time
was 16.4 seconds, peak working set 1,989.9 MiB and OTBM size 27.13 MiB. These are one machine's
measurements, not a CI performance guarantee. No real DAT/SPR or game server was
used; physical/navigation checks and production serialization ran normally.

A repeat on the current branch with `--editable-inputs` retained the same tile,
building and furniture counts, verified the OTBM, and wrote 27 PNGs in 12.9
seconds with a 1,985.6 MiB peak working set. Timing varies with machine load;
the editable files add authoring inputs rather than a new visual client test.


## Verifying written bundles

`--verify` reads the committed OTBM bytes, reopens them through the production
map reader, re-encodes the resulting map, and requires byte-for-byte equality.
It also reopens both XML sidecars and checks their expected roots. Success sets
`generation.json.verifiedOTBM:true`; without the option the field is false.
Verification adds another map allocation and parsing pass. It validates format
round-trip integrity rather than game-server behavior or client sprite visuals.
A verification failure returns nonzero and retains the new folder for inspection.
A differing re-encoding is saved as `verification-readback.otbm`; the error reports
the first differing byte offset and both sizes.


Readback verification exposed an existing inline-ground-border bug: borders with
`ground_equivalent` were not marked as ground borders when loaded. Sandstone
could consequently export both its base ground and an equivalent ground item,
then lose/reclassify one on reload. Loading now activates that equivalence,
allowing the existing writer/reader to preserve the intended ground-plus-border
editor representation. Synthetic editor tests and the real desert profile cover
this round-trip behavior.


Reproduce the larger verified example with the public profile:

```powershell
rme --generate-world data/procedural/recipes/example_large_continent_1098.json data/1098/items_original1098.otb data/1098/items_original1098.xml data/1098/materials.xml build/large-world --data-dir data --tile-budget 4194304 --town-prefix "Frontier" --verify | Out-Host
```

Equivalent-ground objects are excluded from composite decorations/furniture,
and material validation rejects additional ground items inside a tile's item
stack before changing the editor map.


When `--client-dat` is supplied, batch export also requires metadata for every
item actually present in the generated map. A valid header with an incomplete
sprite table fails before creating output; unused registry IDs do not need
sprites. Without DAT, the existing explicit report warning and physical checks
remain available. Editor-only meta items are rejected as grounds/materials or
required furniture because the OTBM writer intentionally omits them.


On Windows, the application is a GUI-subsystem executable. In interactive
PowerShell, piping the batch command to `Out-Host` waits for completion and shows
progress/errors; inspect `$LASTEXITCODE` afterward. The same pipeline was tested
with the stock 10.98 profile and verification enabled. The `Out-Host` suffix is
PowerShell syntax; on Linux use `xvfb-run -a rme ...` as described above.


## Authored OTBM constructions

Catalog templates can now bind fixed-orientation authored OTBM chunks via
`prefab:{file,origin}`. The editor copies their validated ground/items directly,
while the world planner supplies streets, protected footprints and floor links.
Use `example_prefab_town_1098.json` for a mixed authored/procedural town.
See [the prefab guide](WORLD_OTBM_PREFABS.md) for the complete manifest, data limits,
source fingerprint rules and unsupported metadata. Physical stairs and server
entities remain separate; this path keeps the existing single undo transaction.
