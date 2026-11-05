# Improving an existing OTBM map

## Analysis and implementation plan

The world generator already has deterministic palettes/scenes, sampled client
footprints, connectivity checks and undo staging. Its original planner requires
the procedural geography/recipe. Reusing that planner directly on an authored map
would impose new continents, building ownership and floor links over authored work.

This delivery adds a separate existing-map interpretation and proposal stage:

1. Snapshot the selected floor/region, with a 32-tile context halo. Classify grounds
   through explicit client brush/ID bindings; report unmatched ground IDs.
2. Detect authored objects, walls, houses, zones, scripts, containers, travel,
   spawns/creatures and location metadata. Reserve those cells, visible sprite
   footprints and a configurable buffer. Keep holes in an irregular selection.
3. Recognize road components and propose short paths across bare, known natural
   ground. A bounded multi-source flood finds candidates, then a shortest-first
   spanning forest selects at most the configured connection budget.
4. Estimate ecological context from ground category, explicit tree families and
   proximity to water. Reuse the existing object/scenes engine additively, using
   the original client items and preserving every authored stack.
5. Preview the exact staged changes and export interpretation/proposal PNGs plus
   a JSON report. Refuse stale proposals; commit only affected tiles in one undo.

The client-independent analysis is `world_existing.*`. The editor adapter and
transaction are exposed by `world_existing_editor.h`; implementation reuses the
sampler in `world_editor.cpp`. No original procedural recipe or plan is required.
The World planner and existing-map workflow remain independent entry points.

## Using the editor

1. Open the OTBM with its matching client DAT/SPR/OTB/materials.
2. Select tiles on one floor, or configure origin/size/floor in the Procedural
   Generation dialog. The configured scope must lie inside the loaded map.
3. Click **Analyze / improve existing map...**.
4. Choose an analysis profile; enable **Add nature**, **Join short path gaps** or
   both. **Max gap** bounds the number of new path tiles (1..12). **Buffer** is
   the protection radius around authored/unknown content (1..8).
5. Click **Analyze current map**, inspect all three views and read the counts.
   Choose the seed, density and object profile in this dialog. The loaded world
   profile is prefilled when available; leaving it blank uses the analysis
   profile's palette. Legacy image/elevation and original world geography are ignored.
6. **Apply proposed improvements** commits the displayed changes. Undo/redo treats
   the complete intervention as one operation. Save the map normally afterward.

Analysis is limited to 1,048,576 cells including its context halo on one floor.
Analyze another floor separately. Empty maps/unknown-only maps produce no changes;
there is no terrain extrapolation into missing tiles. The selection captured at
analysis time defines the proposal, including irregular holes; a later selection
change does not redefine an already displayed proposal. Editing options requires
reanalysis. Live editing clients are refused.

Views: recognized terrain uses the world palette, with unknown ground in purple and protected travel anchors in magenta;
proposals show added-object tiles in cyan and replacement path ground in yellow;
eligibility shows natural editing candidates in green and protected cells in brown.
The context halo is included in the preview but is never an editing target.

## Client profiles

The bundled `data/procedural/profiles/1098/existing_map.json` maps public 10.98
materials. Each profile contains version 1 and 1..256 ground bindings:

```json
{
  "version": 1,
  "roadBrush": "cobblestone",
  "objectProfile": "objects_natural.json",
  "forestObjects": ["green trees", "trees"],
  "grounds": [
    {"brush": "grass", "terrain": "grass"},
    {"id": 231, "terrain": "desert"},
    {"brush": "cobblestone", "terrain": "road"},
    {"brush": "wooden floor", "terrain": "building"}
  ]
}
```

Each binding has exactly one `id` or exact ground `brush`. IDs must exist and be
grounds in the current client; numeric bindings override brush bindings. Duplicate
IDs/brushes and missing brushes fail analysis. `terrain` uses a world category:
`grass`, `forest`, `desert`, `beach`, `snow`, `cave_floor`, `road`, `plaza`, `building`,
`bridge`, `ocean`, `lake`, `river`, `mountain` or `cave_wall`. These classifications
are authored facts about the client; they are never guessed from numeric ranges.
Client-flagged ground floor changes/teleports are recognized as protected structural
travel even without a palette binding. Their destinations and items are retained.
No match means unknown and protected, not a default grass tile. An optional
`"habitat":"highland"` on a natural binding explicitly selects the highland palette;
the bundled profile uses it for `mountain top ground`. It does not infer altitude.

`roadBrush` is required only when accepted gap proposals exist. It must paint
walkable ground recognized as road/plaza by this profile. Existing item stacks
are never removed to make room for a path; only bare ground is eligible. Optional
bordering runs on newly painted path tiles only and must produce ordinary,
nonblocking borders. Existing neighboring ground/borders are preserved.

`objectProfile` resolves relative to the analysis profile. It uses the same
weighted ecology and atomic scenes as new-world generation. `forestObjects` is an
optional list of up to 32 exact doodad families: their observed client item brush
bindings estimate forest within six Manhattan tiles, without repainting grounds.
Humidity is estimated from ground category and bounded distance to water. Physical
altitude/relief is not reconstructed from a single OTBM floor; slope remains unknown
and is represented as zero for this first adapter. No NPC purpose, ownership or
building style is inferred from artwork.

OT and Ninja Saga share the engine. Custom clients should bind their actual
brushes/IDs and use their local object profiles. The original five-region example
can reuse its private nature profile through the loaded recipe. Regional scenes
use all registered towns on the analyzed floor ordered by town ID, even if a town
lies outside the editing scope; their one-based ordinals retain the same meaning
when the selection shrinks. With no registered towns, scenes requiring town
ordinals are skipped. Cultural region names are not inferred from arbitrary towns.
Private client artwork and themed bindings remain outside the public repository.

## Protection, determinism and export

All existing objects (including ordinary nonblocking decorations) are preserved.
Only non-scripted ordinary borders can share a tile with additive nature. Quest
attributes on ground or borders, complex items/containers, houses/exits, map flags,
zones, spawn/creature anchors, floor changes, teleports, doors and location metadata
are protected. The buffer extends around protected/unknown content. Existing
sprite extents are reserved using loaded client metadata; the inspected halo uses
the same supported 32-tile visual extent as the decoration sampler. Other floors
are not staged or committed. Spawn anchors are protected; an entire spawn radius
is not a blanket exclusion zone.

New decoration uses the existing clearance/connectivity checks. The sampler cannot
replace grounds, create scripted items, insert floor changes, overlap reservations
or disconnect a previously free corridor. Path repairs replace already walkable,
bare natural cells, preserve barriers and do not create bridges over water. The
algorithm fills short gaps between recognized road components; it does not infer
missing city streets or rebuild houses. Paths are currently one tile wide.

Same map, scope, seed, client and profiles produce the same proposal. The effective
object seed also includes the inspected origin and actual z, so matching patterns
on different floors do not receive identical object placement. Existing
world shapes, floors, travel destinations and metadata are retained. Repeated
analysis after applying considers the added objects authored content; it may find
further eligible space, so repeated enhancement is not promised to be idempotent.
Each application must have a fresh reviewed preview and connection limits.

A preview pins its owning editor, serialized ground/item attributes, protected
metadata, neighboring context, client blocking/sprite definitions and both profile
files. Changes during preparation or after preview cause reanalysis instead of
applying an old proposal. The fingerprint is a short-lived source revision guard,
not a portable recipe or a proof that two separate loaded sessions are identical.
When client DAT metadata is loaded, every ground/item on changed tiles must have
corresponding sprite metadata. Only changed tiles enter the undo action; untouched tiles retain object identity.

**Export analysis + PNGs...** writes a JSON report with the source guard, unknown
IDs and exact tile-change positions, plus three semantic PNGs. These are diagnostics,
not sprite renders and not replayable patch files. Save the improved OTBM through
the editor to preserve its normal sidecars. For automated review of a newly generated in-memory map, append
`--review-existing ANALYSIS_PROFILE.json` to the existing `--generate-world`
command. This exports `existing-analysis.json` and optional preview PNGs without
applying the proposal or changing the generated OTBM. It uses the same existing-map
interpreter and is useful for client/profile verification.

Batch enhancement of arbitrary OTBMs
is intentionally not exposed by `--generate-world`; imported house/spawn metadata
and unsupported map versions need a separate lossless batch contract first.

## Validation and remaining work

Core tests cover bounded road gaps, shortest-first deterministic selection,
protection buffers, water/unknown refusal, cancellation and budgets. Editor smoke
tests use a hand-authored map without any procedural plan: additive nature, path
repair, irregular selection holes, objects/scripts/containers/houses/other floors,
large sprites anchored outside the target, stale map/profile snapshots,
cancellation, preserved global RNG, one-step undo/redo and real OTBM round-trip.
Existing planner/CLI regression suites remain required.

Native pointer interaction and in-server traversal require manual acceptance.
Further structural work can build on this interpretation: explicit district/room
labels, authored landmark bindings, path widening, candidate bridges with physical
endpoints and per-building multi-floor renovation. Those require client-specific
contracts and broader previews; they are not inferred or applied in this version.
