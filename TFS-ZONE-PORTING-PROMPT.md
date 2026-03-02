TFS Zone System – Porting Prompt (use with the TFS project)

Goal

- Port the “Zone System” (as implemented in RME) into the TFS codebase, matching the behavior from upstream TFS-ZONES (including the latest fix).
- Provide Lua/revscript APIs to query zones, register movement triggers by zoneId, and allow runtime creation of zones.

Primary reference (local upstream clone)

- Use this local upstream reference for diffs and exact behavior:
  C:\Users\Thor\Documents\NINJA SAGA ASSETS\assets\rme\upstream\TFS-ZONES
- Include the upstream fix: fridaii/TFS-ZONES commit 89cee8a8d28eb5cf1e313d3188759775b924d5bb

Context: What’s already done on the editor (RME) side

- Tiles can store multiple zoneIds. We added:
  - New tile flag: TILESTATE_ZONE_BRUSH (0x0040).
  - Per-tile vector of zoneIds with add/remove/clear helpers; deep copy support.
  - OTBM I/O: when OTBM_ATTR_TILE_FLAGS has TILESTATE_ZONE_BRUSH set, the file encodes a sequence of uint16 zoneIds terminated by 0. Saving writes the same sequence.
- Editor UX:
  - New Zone brush (FlagBrush with TILESTATE_ZONE_BRUSH), plus ZoneBrush panel with a zoneId spinner.
  - Rendering overlay that colorizes ground by zone IDs (toggled via “Show zones”).

What to implement in TFS

1. Data model: store zoneIds on server tiles

- Extend the server-side tile structure to store a list/vector of uint16 zoneIds per tile, with helpers:
  - addZoneId(uint16), removeZoneId(uint16), clearZoneId(), getZoneIds() const& (and getZoneId() for first/primary if needed).
- Ensure copies/moves/serialization preserve zoneIds.

2. Map loading (OTBM)

- In the map loader, after parsing OTBM_ATTR_TILE_FLAGS:
  - If TILESTATE_ZONE_BRUSH is set, read a sequence of uint16 zone ids until 0; for each non-zero id call addZoneId.
- This must exactly mirror how RME writes tiles (sequence of zoneIds with a trailing 0). Match upstream TFS-ZONES logic.

3. Lua/revscript API and movements integration

- Movement events should be able to trigger by zoneId; onStepIn should receive the zoneId that triggered it when registering multiple zoneIds in a script.
- Implement/query functions (align names, signatures, and return types with upstream TFS-ZONES; verify in the reference path):
  - Count helpers (return integer counts):
    - getZoneCreaturesCount(zoneId)
    - getZonePlayersCount(zoneId)
    - getZoneMonstersCount(zoneId)
    - getZoneNpcsCount(zoneId)
    - getZoneSizeCount(zoneId) // number of tiles covered by the zone (if supported in upstream)
  - Vector helpers (return arrays/tables):
    - getZoneCreaturesVector(zoneId)
    - getZonePlayersVector(zoneId)
    - getZoneMonstersVector(zoneId)
    - getZoneNpcsVector(zoneId)
  - Tile zone id extraction:
    - getZoneId(position) → returns an array/table with all zoneIds found on the tile at position (x,y,z)
  - Zone creation:
    - Game.addNewZone(id, range, x, y, z) → place a square-shaped zone of tiles centered at (x,y,z) with the given range, tagging tiles with zoneId id.
      - This is normally called in an onStartup event to mark areas at server start.
- Movement/revscript compatibility:
  - When registering onStepIn for multiple zones in one script, ensure onStepIn(creature, item, position, fromPosition, zoneId) includes the zoneId that fired it (match upstream signature order and types).
  - Support registering by explicit zoneId.

4. Runtime structures (performance)

- Maintain a mapping to efficiently answer “which creatures / players / monsters / npcs are inside zoneId?”
  - On map load, when zoneIds are attached to tiles, populate zoneId → tile set (or zoneId → bounding map) so counts/queries are O(tiles) but fast.
  - For count/vector queries, iterate tiles of that zoneId and filter occupants by type.
  - Consider caching or lazy recompute if upstream TFS-ZONES does so; follow its approach.

5. Map saving (optional)

- If your TFS build saves modified maps back to OTBM, mirror the editor format:
  - When saving a tile with TILESTATE_ZONE_BRUSH, write all tile.getZoneIds() followed by a terminating 0 (uint16) after the flags u32.

6. Tests / acceptance

- Load a map with zone areas saved by the new RME and verify:
  - Tile flags include TILESTATE_ZONE_BRUSH where expected.
  - getZoneId returns all ids for a tile (including overlapping zones).
  - Count and Vector helpers return plausible values when players/monsters are inside the area.
  - onStepIn passes the correct zoneId to the script callback.
  - Game.addNewZone creates the zone; counts and getZoneId reflect it after onStartup.
- Confirm the upstream fix 89cee8a is applied or obsolete due to your code path.

How to diff/port from the upstream reference

- Search patterns to locate all relevant changes in the reference path:
  - “TILESTATE_ZONE_BRUSH”, “Zone”, “zoneId”, “getZoneId(”,
  - “addNewZone(”, “getZonePlayersCount”, “Vector” endings,
  - onStepIn signature changes in movements/revscripts.
- Copy or adapt logic to your TFS code structure. Preserve the file/module boundaries used by your codebase (filenames/namespaces may differ from upstream).
- Keep ABI compatibility where required; add overloads if needed instead of breaking existing public APIs.

Coding guidelines

- Match existing formatting and error handling.
- Keep per-tile zoneId storage lightweight (vector of uint16 is fine).
- Avoid quadratic scans; prefer pre-indexing zoneId → tile references at load time.

Deliverables / done checklist

- Tile model supports multiple zoneIds.
- OTBM loader reads zoneIds sequence (terminated by 0) when TILESTATE_ZONE_BRUSH is set.
- Lua/revscript:
  - Movement onStepIn receives zoneId.
  - Count and Vector helpers work.
  - getZoneId(position) returns all ids on the tile.
  - Game.addNewZone works and is safe to call onStartup.
- If applicable, OTBM saver writes the zoneIds sequence after tile flags.
- All changes compile and basic tests pass.

Notes

- Use the reference at:
  C:\Users\Thor\Documents\NINJA SAGA ASSETS\assets\rme\upstream\TFS-ZONES
  and include the fix:
  89cee8a8d28eb5cf1e313d3188759775b924d5bb
- Names listed above are indicative; match exactly what upstream TFS-ZONES uses (function names/param orders) to ease maintenance and cross-compatibility with shared scripts.
