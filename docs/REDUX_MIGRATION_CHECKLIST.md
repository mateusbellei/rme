# Redux Migration Checklist

Manual regression checklist for each rendering/performance PR on `feat/redux`.

Record baseline and post-change numbers in the **Results** column (FPS average while panning, frame ms from status bar with **View → Show FPS**).

## Performance

| Test | Steps | Pass criteria | Results |
|------|-------|---------------|---------|
| Large map pan/zoom | Open a map with 10k+ visible tiles at zoom 100%, pan for 10s | No crashes; note FPS/ms | |
| Legacy renderer default | `Use Modern Renderer` off | Map identical to pre-migration | |
| Modern renderer | `Use Modern Renderer` on | Tiles, items, creatures visible; FPS ≥ legacy on large maps | |

## Zones (TFS)

| Test | Steps | Pass criteria | Results |
|------|-------|---------------|---------|
| Zone brush paint | Zone brush + ID, paint on ground tiles | IDs stored on tile | |
| Zone tint | View → Show zones | Ground tinted by zone ID | |
| Always show zones | View → Always show zones | Zone sprite on empty flagged tiles | |
| OTBM round-trip | Save map with multi-ID zones, reload | Zone IDs unchanged | |

## Data Editor

| Test | Steps | Pass criteria | Results |
|------|-------|---------------|---------|
| Border 5×5 grid | Data Editor → edit border, save | `borders.xml` updated; reload client data | |

## Procedural generation

| Test | Steps | Pass criteria | Results |
|------|-------|---------------|---------|
| Image mask | Select area, PNG + legend, generate | Tiles match legend | |
| Text prompt | Text prompt mode, generate | Map fills selection | |
| Sidecar off | `sidecar.json` enabled false | Generation works without Python | |
| Autoborder | Generate with borderize enabled | Borders applied on edges | |

## Headless API (Phase 7)

| Test | Steps | Pass criteria | Results |
|------|-------|---------------|---------|
| `ProceduralHeadless::Run` | Call from dialog path with same spec | Same result as `ProceduralGenerator::Run` | |

## Build

| Test | Steps | Pass criteria | Results |
|------|-------|---------------|---------|
| VS Release x64 | Build `Editor.vcxproj` Release x64 | Zero errors | Pass (2026-06-22) |
| CMake (optional) | Configure + build with vendored glad/glm | Zero errors | |
