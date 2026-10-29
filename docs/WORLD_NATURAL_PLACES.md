# Natural transitions, scene variants and review tools

The extensions are optional. Existing object profiles and recipes retain their
seeded OTBM output. Use `example_natural_town_1098.json` for the generic 10.98
example; custom clients bind their own exact brushes through the same contract.

## Environmental transitions

A version-1 object profile may include:

```json
"transitions": {
  "forestEdgeWidth": 7, "forestEdgeDensity": 45,
  "pathWidth": 4, "pathDensity": 25,
  "slopeThreshold": 800, "slopeDensity": 45
}
```

Widths are integer tile distances 0..32; zero disables that transition. Densities
are 0..100 percent of ordinary scatter density. Density rises linearly from the
specified edge density to 100 percent across the width. Forest edges are the
cardinal boundaries between forest and grass, with both boundary cells at zero.
The forest edge reduction applies only to forest terrain. Paths are road, bridge
and plaza cells. Slope is the maximum cardinal elevation difference (0..10000);
above `slopeThreshold`, density declines linearly toward `slopeDensity` at the
maximum slope. Zero threshold disables slope reduction. Multiple reductions use
the minimum, rather than multiplying them. They affect scatter, not scene chance.

Distances are bounded Manhattan distance through the raster, not a walkable route
or hydrological simulation. No source within 32 tiles is represented by 33. They
are computed from the complete immutable floor before scattering, so changing a
local edit mask cannot create artificial shorelines or forest margins.

Ecology patches additionally support `minWaterDistance` (0..32),
`maxPathDistance` and `maxEdgeDistance` (0..32), and `minSlope`/`maxSlope`
(0..10000). Ranges are inclusive and ordered. Missing bounds are unrestricted;
minimum water distance with no maximum includes cells beyond the bounded radius.
The original `maxWaterDistance`, elevation and moisture filters remain available.
`objects_natural.json` demonstrates grassy margins, path verges and exposed slopes.
This changes decorations; it does not erode terrain or paint new ground types.

## Scene variants

A scene declares either its original `objects` array or a `variants` array,
never both. Each scene allows 1..8 variants with unique bounded IDs, integer
`weight` 1..100 and an `objects` array using the existing 1..24-anchor contract:

```json
"variants": [
  {"id":"occupied", "weight":4, "objects":[
    {"x":-3,"y":0,"brushes":["world chair east"]},
    {"x":3,"y":0,"brushes":["world chair west"]}]},
  {"id":"remnant", "weight":2, "objects":[
    {"x":-3,"y":0,"brushes":["small rocks"]}]}
]
```

The object seed selects one weighted variant per attempted scene. The selected
variant must pass complete sampled footprint, rotation, clearance and topology
checks. A failed variant skips that attempt; it does not retry other variants or
leave partial objects. Scene radius and habitat/region filters apply to every
variant. The bundled natural profile provides complete, weathered and remnant
compositions by thinning existing arrangements, using the already mapped brushes.
These labels are visual arrangements, not occupation/NPC states.

`sceneVariantUsage` counts `scene-id/variant-id`. `scenePlacements[].variant`
identifies the variant (empty for legacy scenes). `sceneUsage` remains the total
per family. Counts include all placements even when anchor diagnostics reach
their 4096-entry cap.

## Terrain-sensitive lots

`world.terrainAwareLots: true` sorts available lots before placing each town.
The score is Manhattan distance to the center plus a 5x5 sample of local elevation
range divided by 40 and 30 points per water/void/outside sample. Sample spacing is
two tiles. This favors flatter dry patches while keeping towns compact. Stable
ordering preserves ties; footprint, road access, districts and required template
quotas remain authoritative. It is a site preference, not contour-following road
construction, a bridge planner or a guarantee that every chosen site is flat.
Defaults are false and opt-in policy enters the plan fingerprint. The World
settings dialog exposes **Prefer flat dry sites**.

## Painting a local nature selection

In the world preview choose **Paint nature replacement** or **Paint preservation**.
The first stroke starts a black mask on the selected floor, unless an existing
mask was loaded. White cells are eligible for nature replacement; black cells are
preserved. Circular strokes interpolate between pointer samples and clip at the
map boundary. Radius is 1..32 tiles. The preview dims the preserved cells and shows
the effective tile count. Painting switches to replacement of ordinary existing
tiles; protected metadata, construction and travel reservations stay protected.

**Save mask + recipe...** writes a black/white PNG beside a portable JSON recipe.
Use it with the same complete generated world and geography plan. The selection
belongs to one floor; switching floors does not move it. Close/reopen the preview
to start a different scope. Changing geography seed is disabled while a local
mask exists. No paint operation edits the map until Apply, which keeps the existing
single undo transaction and complete-world validation. Rebuilding only buildings
or terrain is not supported by this painter. Shape/geography PNGs are still edited
externally and can propagate changes beyond the painted region when replanned.

## Review diagnostics

The preview and diagnostic export add **Path distance (32 tiles)**: green is near
roads/bridges/plazas, warmer colors indicate greater Manhattan distance and purple
means no path within 32 tiles. Non-walkable cells retain their terrain color. This
is not actual travel distance or a claim of disconnected terrain.

`generation.json.decorationRegions` includes every 32x32 region of each decorated
floor (edge regions are smaller): absolute `position`, dimensions, `naturalTiles`,
`objectAnchors`, and `scenes`. Natural tiles are unreserved, physically walkable
eligible cells before scene placement, restricted to the selected nature mask.
Object counts are brush anchors, not occupied sprite area. Scene object anchors
are counted in their own regions; the scene itself is counted at its center.
The object counts sum to `doodads`; scene counts sum to `sceneUsage`. Floors with
no eligible nature and runs with decoration disabled produce no region records.

Generate a self-contained offline HTML report:

```text
python tools/world_quality.py path/to/generation.json path/to/quality.html
```

It displays the floor grid, per-region hover inspection, density/object/scene
views and usage tables for brushes, patches and variants. It does not assign an
aesthetic score. Low density can be intentional, and object size varies. Applied
counts are not inferred from the planning preview. Architecture repetition,
server travel times and NPC/services remain outside this report.

## Validation limits

Core tests compare environmental transforms with independent Manhattan formulas,
check clipped continuous paint strokes, and retain exhaustive topology regressions.
Real CLI tests verify variant replay, object seed isolation, atomic rejection,
region count reconciliation and script-safe offline report data. Windows editor
smoke tests cover masked replacement, protected tiles, floor travel, undo/redo and
OTBM readback. Native GUI pointer interaction and server movement need interactive
acceptance with the actual client; compilation and offline sprite renders do not
substitute for those checks.
