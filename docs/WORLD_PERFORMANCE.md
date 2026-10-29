# Procedural generation performance

Optimizations preserve the seeded planner and object-selection algorithms. The
same profiles are used for OT and custom clients; no artwork, density or scene
weights need to change to benefit.

## Local nature staging

A masked edit copies the existing world into private context once. It rebuilds
grounds and borders only for the effective natural selection, keeps original
windows/travel items elsewhere, and validates that combined context before the
single undo operation. Untouched cells are no longer regenerated and subsequently
copied a second time. Scenery grids are omitted for other floors or layers without
eligible natural cells. Full planning, context copying and final connectivity
validation still cover the world; memory/time are not proportional only to the
painted area.

`ApplicationStats` and batch `generation.json` expose:

- `preparedTiles`: selected non-void cells prepared during staging, including
  authored cells in full generation. This equals committed tiles for a successful
  local transaction.
- `contextCopies`: existing editor tiles copied into private scratch context,
  including the outer ring. This is normally zero for a new empty batch map.

The object profile is read/parsed once for ecology and scenes. Invalid fields
still fail atomically. Scene rejection counters describe attempts actually run;
unselected/ineligible floors are no longer sampled solely to reject them.

## Decoration clearance

After checking bounds, reservations, habitat and the entire clear halo, a blocker
whose footprint is at most two cells wide or high cannot enclose a free cell:
every free cell directly touches the clear halo. This common case needs no flood.
Larger compositions still run the complete connectivity flood, using bounded
34-square stack storage instead of per-call vectors and a deque. The existing
32-cell span and 256-cell footprint limits are unchanged.

Regression tests compare all 2x4, 4x2 and 3x3 blocker combinations with an
independent whole-map flood. Maximum-size clearance also exercises both an open
shape and an enclosed pocket. Existing route/biome/occupancy rejections remain.

## Measurements

Local Windows/MSVC Release measurements on 2026-10-29 compared the implementation
before these changes with the optimized version:

| Editor fixture | Median before | Median after | Reduction |
|---|---:|---:|---:|
| 192-square surface mask | 205 ms | 66 ms | 68% |
| 192-square cave mask | 207 ms | 73 ms | 65% |
| 256-square surface mask | 370 ms | 114 ms | 69% |
| 256-square cave mask | 367 ms | 123 ms | 67% |

These are two samples per fixture, timing `ApplyPlan` on already-planned synthetic
editor worlds. All four map hashes matched the prior implementation. They are
not a promise about complete continent generation, PNG decoding, server loading,
or other hardware. A separate mixed 400,000-call clearance benchmark, four samples
per version, improved by approximately 4.7x with identical acceptance totals.

For repeated editor measurements, build with `RME_ENABLE_WORLD_SELF_TEST=ON`, set
`RME_WORLD_PERF=1` and run `rme --world-self-test OUTPUT_DIRECTORY`. `METRIC` lines
record surface/cave, organic/block layout, elapsed milliseconds and resulting map
hash. Timing is opt-in and has no generation side effects. Pair elapsed-time
comparisons with output checks, not time-based test assertions.
