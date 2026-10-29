# Composed places and local PNG editing

These opt-in extensions share the same planner for OT and custom clients. Exact
brush names belong to each client's profiles; the engine does not hard-code a
fictional setting or infer compatible artwork from item numbers. Existing recipes
without these fields retain their previous plan and OTBM output.

## Wilderness scenes

`objects_places.json` extends the ecology profile with a top-level `scenes` array.
The bundled 10.98 example includes wayside camps, overgrown ruins, stone shrines,
training clearings, snow shelters, crystal sanctuaries, dry waystations and orchard
rest areas. Camps include tested two-by-two tent composites. Scene props reuse
existing client items; the campsite hearth is unlit.

```json
{"id":"grove", "habitats":["forest"], "spacing":48, "chance":60,
 "radius":6, "rotate":true,
 "objects":[{"x":-3,"y":0,"brushes":["mossy rocks"]},
            {"x":3,"y":0,"brushes":["yellow flowers"]}]}
```

At most 32 scenes, each with 1..24 distinct anchors. Habitats are forest, desert,
ice, cave, coast and highland (1..6 unique names). Radius is 2..12; spacing is
16..128 and must be at least `2*radius+4`. Chance is 1..100 percent per spatial
cell, independent of scatter density. Decoration must still be enabled with
positive global density. Optional `min/maxElevation` and `min/maxMoisture` are
inclusive integers 0..10000. One brush is orientation-independent; four select
north/east/south/west variants as the scene rotates clockwise.

The independent object seed controls jitter, orientation and brush sampling.
Optional `settlements` lists unique one-based town ordinals (1..32). A scene then
belongs only to the closest generated settlement, using Manhattan distance and
the earliest ordinal on ties. This partitions wilderness cultural motifs between
regions without requiring those motifs inside the village. With no generated
settlements, a filtered scene is skipped.

Scenes precede ordinary scatter, preserve a clear square and its habitat, and
avoid reserved paths and floor travel. All sampled visual footprints must fit
within the radius without overlapping. The union passes the same clearance and
connectivity tests as ordinary objects. Either the entire composition is placed
or nothing is placed; there is no partially furnished scene. Invalid profile
fields fail the generation before the editor transaction. An incompatible sampled
brush skips the whole scene, retaining the existing safety checks.

`sceneUsage` counts placed compositions; `skippedScenes` counts eligible spatial
attempts rejected by footprint/sampling checks. `objectBrushUsage` and `doodads`
also include each successfully placed scene anchor. `scenePlacements` records the
first 4096 composition anchors with absolute map position, clockwise rotation and
object count; usage totals remain complete when that diagnostic list is capped. Changing scene order can
change its spatial seed. Scene records do not create spawns, scripts or quests.

## Settlement functions and regional architecture

Construction templates may declare `district`: `civic`, `market`, `residential`,
`training` or `rural`. Civic functions favor lots within 24 tiles of the plaza;
other functions favor distinct seed-rotated sectors, with rural lots also favoring
the outskirts. Preferred candidates receive eight times their ordinary weight.
This is a soft affinity, not an exclusion boundary. Hard biome/settlement filters,
construction minimums and maximums remain authoritative, so required functions can
still occupy a safe available lot when their preferred area cannot fit.

`plan.json` records each building's district. Use per-settlement catalog bindings
for different regional architecture. `composed_buildings.json` demonstrates a
townhouse, market hall, civic hall, courtyard training pavilion and rural lodge.
The pavilion has connected galleries and an open courtyard between floors.
Street variation and terrain costs remain responsible for the routes; districts
do not claim to implement an unrestricted road graph or a population simulation.

`roofRows` has the same rectangular dimensions as the base template. A space is
an opening; `.` uses the default `roof` material. `n/e/s/w` select slope materials,
and `h/v` select horizontal/vertical ridges:

```json
"roofMaterials": {"north":"brick roof, up", "east":"brick roof, right",
  "south":"brick roof, down", "west":"brick roof, left",
  "horizontal":"brick roof, horizontal", "vertical":"brick roof, vertical"}
```

The default roof binding remains required. Rotatable templates require all four
slope bindings when using slopes, and both ridge bindings when using a ridge.
Rotation changes both the silhouette and its orientation symbols. Exact ground
brushes resolve once before staging. The roof stays on its own non-accessible
layer above the last occupied floor; it is not a traversable attic. Client brush
border rules and artwork determine the final visual seam. Roofs stay within their
owned building rectangle. OTBM prefabs continue to prohibit generated roofs.

## Terrain connections and cave rooms

`world.mountainPasses: true` permits roads to cut through unreserved mountains at
a higher routing cost (120 versus 10 for ordinary terrain). The cut becomes
reserved road ground. Ocean/lake barriers, walls and reserved mountain cells stay
blocked. Connected terraces remain governed by their existing vertical links.

`world.caveChambers: 0..64` adds seed-derived elliptical rooms to each cellular
cave floor before connectivity carving. Room moisture varies for ecological
palettes. `world.wildernessCaveEntry: true` chooses the first cave entry on a clear
natural site outside settlements rather than the first plaza. It prefers higher
terrain and fails explicitly if no safe site exists. Additional entrances retain
their separated site selection. Cave rooms and all floors still require access
to the surface; physical stairs retain directional client-item validation.

Use `example_composed_town_1098.json` for a 256-square town, three upper mountain
levels and two cave floors. Native server movement still needs gameplay acceptance;
planner connectivity, OTBM readback and floor-change flags do not prove a server's
movement implementation.

## Two PNG editing workflows

**Shape and geography:** export editable inputs from the preview or batch
`--editable-inputs`. Edit the land PNG (white land / black ocean), grayscale
heightmap and exact-palette biome PNG. Transparent input pixels retain procedural
geography. Load the exported editable recipe to rebuild the world. Drainage and
settlement planning may propagate a geography edit beyond the painted pixels;
this workflow does not promise to preserve manually edited OTBM tiles.

**Local nature in an existing world:** paint white in the exported blank
`inputs_nature.png` (or `_nature.png` for preview exports), keep black/transparent
elsewhere, then select it in World settings as the local nature mask. The PNG
must exactly match the target dimensions; it is never rescaled. Partial alpha or
other colors are rejected. Set `natureFloor` to the relative offset: 0 surface,
-1 one floor above, +1 first cave floor. The chosen floor must exist.

Open the complete world generated from the same geography, master seed, catalog,
origin and dimensions. Keep those settings and change only the object seed/profile.
Disable `protectExisting` to explicitly replace ordinary tiles in the selection.
Selected natural cells receive newly generated ground and ordinary objects;
planned reservations, buildings, roofs, roads and travel cells are excluded even
inside white pixels. House/spawn/creature/script/zone/waypoint metadata still
blocks replacement. This is replacement, not additive decoration of user props.

The preview dims preserved pixels and reports the effective tile count. Selected
tiles are generated inside a private copy of the original context and validated
for physical travel/navigation before any changes are committed. Every tile
outside the effective selection retains its original editor object; no outside
borders are committed. A single undo/redo covers the local change. Empty effective
masks, incomplete original worlds, protected metadata and incompatible interfaces
fail atomically. The batch generator creates a new empty map, so it cannot use
this existing-world mode as a partial OTBM patch command.

Replacement is per tile: an existing wilderness scene or multi-item assembly is
not recovered as an editable group from OTBM. Paint the complete old assembly if
you want to remove all its parts; outside parts remain untouched. Atomic scene
placement applies to the new composition. Local application still plans and
validates the complete world, so memory/time are not proportional only to the
painted area.

Staging avoids regenerating preserved tiles or copying them a second time.
See [WORLD_PERFORMANCE.md](WORLD_PERFORMANCE.md) for counters and measurement scope.

## Custom settings such as Ninja Saga

The same fields apply to custom profiles. Bind residential clans, markets,
academies/dojo and administrative buildings to their regional settlement ordinals;
choose roof palettes and court layouts per region, and author wilderness scenes
with the custom client's own validated props. Keep themed recipes and private
client assets outside the public repository. The local package demonstrates this
separately from the generic OT example; no DAT/SPR is bundled or modified.

Natural transitions, scene variants, terrain-sensitive lots, the preview painter and
regional review reports are documented in [WORLD_NATURAL_PLACES.md](WORLD_NATURAL_PLACES.md).
