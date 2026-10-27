# Roofs, facades, physical stairs and terraced highlands

These are optional additions to version-4 world recipes. Existing recipes retain
their planning behavior when the new settings/catalog fields are omitted.
Try `recipes/example_highland_village_1098.json` with the public 10.98 OTB/XML.

## Architecture

Each generated template can declare `"roof":"brown bamboo roof"` and
`"windowChance":40`. Roof is an exact **ground brush** name. It covers the last
occupied silhouette on a separate layer, including its perimeter; void/courtyard
cells remain open. A three-floor house at z=7 occupies z=7..5 and has its roof
at z=4. Roof tiles are excluded from the planner's walking graph, floor links
and generated house ownership. They count toward the application tile budget.
They are decorative covers, not authored accessible terraces or gabled models.

Windows replace compatible straight exterior walls through their existing
wall brush window bindings, preserving orientation and blocking. `windowChance`
is 0..100; unsupported wall families keep their original wall. Prefabs retain
their authored contents and reject generated roof/window overrides.

Furniture can opt into `"placementRadius":2` (0..3). The editor tries the authored
anchor first, then nearest Manhattan-distance candidates within the same
building floor. The chosen brush variant stays fixed during fitting. Every
trial must satisfy the complete item/sprite footprint, one-tile clearance,
stairs/arrivals, wall and occupancy checks. Required furniture still aborts
generation if nothing fits. `relocatedFurniture` reports successful adjustments.
Omission/zero keeps strict authored placement.

## Physical directional stairs

`world.physicalStairs:true` replaces planned teleports with real directional
floor-change items. Optional `world.stairItems` is `[down,north,east,south,west]`;
zero entries resolve compatible active-client items, preferring stairs/ramps.
Resolution requires exactly the expected directional metadata, nonblocking,
nonmovable items and no teleport/container/door/editor-only substitutions.
Alternate/two-square floor changes and scripted ladders are not supported.

The movement model follows [TFS Tile::queryDestination](https://github.com/otland/forgottenserver/blob/master/src/tile.cpp):
a lower directional stair at p arrives upstairs at p+d; the upper downward item
at p arrives downstairs at p-d. The planner reserves opposite arrivals and may
move a landing by at most three tiles to fit its gallery. It rejects unavailable
landings rather than silently reverting to teleports. Staging verifies exact
travel items and rejects stray/double floor changes before applying any tile.
Navigation, undo and OTBM codecs are checked locally; playing through the stairs
on the consumer's actual server is a separate acceptance step.

## Streets and mountains

`world.streetVariation` is 0..60. With blocks, seeded extra block widths produce
different block sizes without reducing the safe minimum lot spacing; a coarse
spatial route cost varies new roads around natural terrain. Zero preserves the
regular layout. This is varied streets/frontages, not a full district simulation.

`world.mountainLevels` is 0..4. Natural terrain at elevation 7600, 8200, 8800 and
9400 produces nested upper masks. Small areas below 64 cells and unsupported
walkable slivers are omitted. Terrace edges are blocking cliffs; each walkable
component gets a validated connection to the floor below. Surface approaches
are carved when needed. Rounded settlement buffers and reserved routes stay
clear of overlying terrain. Building roofs and mountains can share upper layers
without sharing cells. Actual cliffs depend on authored height and available
space; asking for more levels does not manufacture taller peaks.

`world.highlandBrush` optionally binds an exact walkable ground for non-snowy
terraces. `objects_rich.json` includes a separate `highland` palette: rocks,
small grasses and stumps. Snowy terraces retain the snow habitat. The object
seed remains independent of planning. The batch report includes `roofTiles`,
`windows`, `stairEndpoints` and `relocatedFurniture` for auditing the result.

The scenario compiler accepts these settings and preserves their portable paths
and manifest digests. The editor exposes mountain floors, street irregularity
and the physical-stairs switch in World settings. Exact stair IDs, per-template
facades/roofs and fitting radii remain in authorable JSON profiles.
