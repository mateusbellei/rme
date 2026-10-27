# Ecology patches and object coverage

`profiles/1098/objects_ecology.json` is an optional version-1 object profile.
It maps 44 exact brush families into 19 patch palettes across six habitats.
`recipes/example_ecology_1098.json` demonstrates it with a roofed highland town.
It does not add client art, NPCs, spawns or server scripts.

The ordinary biome `brushes` and `density` remain the fallback. Optional fields:

```json
{
  "density": 130,
  "brushes": [{"name":"green trees", "weight":4}],
  "patchSize": 18,
  "baseWeight": 35,
  "patches": [{
    "id": "wet_bank", "weight": 60, "density": 90,
    "maxWaterDistance": 6,
    "minMoisture": 4500,
    "brushes": [{"name":"tall swamp grass", "weight":3}]
  }]
}
```

There are at most eight patches per biome. IDs are unique lowercase identifiers
of 1..40 bytes; `base` is reserved. `patchSize` is 8..64 tiles (default 16),
`baseWeight` 0..100 (default 100), patch weight 1..100. A patch's density is a
0..200 percent multiplier of its biome density; 100 inherits that density.
An empty palette or zero density can make a clearing. The source profile has
flower meadows, bamboo groves, riparian vegetation, orchards, open glades,
dry scrub, sand piles, oases, snow groves, reeds, rocky shores, cave formations,
crystal/mushroom pockets and sparse highland slopes.

Optional eligibility ranges `minElevation`, `maxElevation`, `minMoisture` and
`maxMoisture` are normalized integers in 0..10000. Minimums cannot exceed maxima.
`maxWaterDistance` is 0..32 tiles, measured from the anchor to ocean/lake/river
on the **same floor** using Manhattan distance. It ignores hydrological flow
and barriers. Cave climate values may be zero; profiles should not infer a cave
humidity simulation from these fields.

Eligible patch weights compete with the base weight using a coarse spatial hash
of the independent object seed. Nearby anchors usually share a palette;
eligibility can change at climate or shoreline boundaries. The existing density
noise, brush sampling and footprint/halo checks then apply. Objects are never
allowed to block reserved roads, stairs or arrivals. Patch constraints apply
to anchors, not every pixel of an overhanging sprite. This is a bounded scatter
system, not a simulation of plant growth. Profiles without patches retain their
previous placement and OTBM behavior.

Water distances use a bounded flood from shoreline cells only. Palettes are
resolved once per generation. Changing objectSeed/profile does not move towns
or alter terrain, building/roof layouts or their plan fingerprint.

## Safe client bindings and coverage

The stock `bamboo plant` brush mixes ordinary plants with z=-1 composites. The
new `world bamboo clump` binding in `1098/procedural_nature.xml` selects only
existing single-floor items 4025/4026. Cross-floor objects remain rejected;
no floor validation was relaxed. Sand dunes contain ground tiles and are not
part of this scatter palette; `sand pile` provides ordinary decorative items.
The original stock brushes remain available to manual editing.

`generation.json` now reports:

- `objectBrushUsage`: successful placements per exact brush name. The sum equals
  `doodads` (placements, not necessarily individual items in composites).
- `objectPatchUsage`: successful placements per `habitat/patch-id`; base palette
  placements are excluded.
- `objectBrushRejected`: sampling failures such as cross-floor composites,
  ground/travel/container/quest objects or unsupported footprints. Ordinary
  space/collision failures remain in `skippedDoodads` and are not listed here.

A configured brush with no recorded use may simply have lacked eligible space
or climate; this is not evidence of incompatibility. Validate a custom DAT/OTB
and inspect its actual sprites before claiming visual quality. Public CLI
regressions cover replay, seed isolation, usage totals, malformed ranges/IDs
and empty-vs-positive shoreline limits, in addition to navigation and OTBM checks.
