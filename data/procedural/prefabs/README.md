# OTBM construction samples

`1098/stone_house.otbm` is a small generic, fixed-orientation three-floor chunk
assembled with public item IDs from the bundled 10.98 registry. It contains wood
floor 405, stone wall pieces 1049/1050/1053, open doorway 6254, chair 1652 and book
1950 with example text. Base origin is `[0,0,7]`; its footprint is 7x6 and upper
floors close the base doorway. No client artwork or external server entities are
included. This is a technical example, without real-client visual/game QA.

Bindings and an accompanying generated inn are in
`../profiles/1098/prefab_buildings.json`. Authoring contracts, restrictions and
commands are in `docs/WORLD_OTBM_PREFABS.md` at the repository root. Export your
own standalone chunks in the target client's registry; do not distribute private
consumer assets in this public directory.
