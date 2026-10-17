# Test maps for Redux migration

Place local `.otbm` reference maps here for manual regression (not committed if large).

Suggested fixtures:

1. **large_overworld.otbm** — 10k+ tiles visible at zoom 1 for FPS baseline
2. **zones_multi.otbm** — tiles with multiple TFS zone IDs per tile
3. **procedural_seed.otbm** — empty or flat selection area for procedural generation tests

Document your baseline FPS/ms in `docs/REDUX_MIGRATION_CHECKLIST.md` after opening each map.
