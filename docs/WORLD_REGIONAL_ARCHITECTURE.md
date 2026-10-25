# Regional architecture and wilderness

A version-1 construction template can specify `"settlements":[1,3]` for distinct
towns in the same biome. These are **one-based ordinals** in `settlementAnchors`
order (or generated town order without anchors), not server town IDs.
Omitted/empty lists allow every town. Lists contain at most 32 distinct integers
in 1..32; biome restrictions still apply. Minimums, caps, lot spacing and
construction-mix diagnostics consider only templates allowed in that town.

Use fixed anchors when regional identity must survive changes to the master
seed. Changing the town count/order requires reviewing the bindings. Rotation
preserves these filters. Plan validation rejects mismatched buildings; the
fingerprint includes nonempty bindings. Existing unrestricted catalogs retain
their behavior and fingerprints.

Combine this filter with `upperFloors` and `floorMaterials` to author towers,
setbacks and regional materials. The recipe's `world.buildingLevels` caps height;
each template's `maxLevels` permits a different skyline, up to four floors.
Keep aligned `a`/`b` landings on every occupied floor, clear arrival cells and
enough room for the complete rotated furniture footprint. Floor travel still
uses paired teleports; this does not provide physical stairs or finished roofs.

`profiles/1098/objects_rich.json` is an opt-in wilderness palette with weighted
trees, stumps, shrubs, flowers, mushrooms, grasses and rocks. Bind it through
`world.objectProfile`; `world.objectSeed` changes decoration without moving towns
or changing geography. Existing footprint, route reservation and walkability
checks apply to every sampled object. Weights control relative frequency;
density scales each biome's placement probability, not the percentage of tiles
that will survive collision checks. Mountain walls and water remain undecorated.

Exact brush names require matching 10.98 materials. Custom clients should verify
their DAT/OTB mapping. This profile does not introduce or distribute client art.
