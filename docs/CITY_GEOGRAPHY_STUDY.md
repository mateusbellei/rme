# City geography: reference study and first executable profiles

Tracking: [issue #5](https://github.com/mateusbellei/rme/issues/5).
These are original procedural studies. They do not import a commercial map,
official city OTBM, image trace or canonical building coordinates.

## What makes the references recognizable

| Reference | Evidence | Procedural interpretation |
|---|---|---|
| Thais | The official regional description places the city beside a western bay, between northern/southern rivers, with an eastern defensive side and an island castle. | A coastal port needs a large dry urban basin, a sea-facing side, flanking water corridors, inland access and a distinct offshore landmark. |
| Venore | The official visit describes an eastern marsh settlement whose early buildings were raised on stilts. Its approaches include wooden bridges. | A wetland merchant city needs wet surroundings, dry or elevated circulation, restricted approaches and more vertical occupation. It cannot be reproduced merely by increasing forest density. |
| Shared civic pattern | The official manual describes temples and depots as central city services, with depots commonly occupying multiple floors. | Required civic/storage/commercial buildings should get central lots before the residential minimum is filled; residences then form the surrounding fabric. |

Sources: [Thais regional description](https://www.tibia.com/library/?region=thais&subtopic=maps),
[A Visit to Venore](https://www.tibia.com/news/?id=1783&subtopic=latestnews),
[official world manual](https://www.tibia.com/gameguides/?section=world&subtopic=manual).
Descriptions establish topology and functions, not measured tile dimensions or
an exact reconstruction. Building counts and dimensions below are design choices.

## Implemented first pass

`geography/coastal_port.json` describes an original bay/river/plain arrangement.
`geography/wetland_trade.json` describes dry urban ground surrounded by wetlands,
water pockets and controlled approaches. The compiler applies seeded smooth
domain warping to selected contours so variation is not limited to perfect ellipses.

Compiled scenarios live in `data/procedural/scenarios/`. The UI recipe list exposes
`example_coastal_port_city_1098.json` and `example_wetland_trade_city_1098.json`.
The public 10.98 catalog provides nine original templates: two housing types,
temple, depot hall, bank hall, market, inn, guard hall and guild/library. Their
room layouts, floor counts, entrances and furniture remain subject to the usual
navigation and sprite checks. A building's name does not create NPC behavior,
protection zones, bank accounts or functional depot lockers.

### Fixed geography, variable settlements

Optional v4 `world.settlementAnchors` is a complete ordered array of local tile
coordinates, e.g. `[{"x":302,"y":261}]`. It must contain exactly `settlements`
entries or be empty. Anchors retain their order and supersede automatic biome
diversity/site scoring. The normal separation rule still applies. Each center
must stay eight cells inside the target and have a dry buildable 9×9 plaza with
an elevation range no larger than 1300. Unsafe anchors fail explicitly; the
planner never silently moves the intended city to another region.

Changing the master seed varies lot/template selection while keeping these
centers and the PNG-defined geography fixed. Separate object/cave seeds continue
to work. World settings can uncheck **Keep recipe anchors** to return to automatic
sites. Changing image/region dimensions does not rescale anchor coordinates:
recompile the profile at the new size instead.

### Central services before housing

Catalog templates accept `minimumPriority` (integer 0..1000, default 0). While
construction minima are unmet, only the highest unmet priority group competes
for the next lot. Within the group, ordinary template weights still apply.
After all minima, normal weighted/max-count rules resume. The city catalog uses
priority 100 for required service buildings and 0 for housing. On block layouts,
lots are offered from the plaza outward. This creates a civic center without
hardcoding a particular game's building names or exact city layout.

Default recipes/catalogs without anchors or priorities preserve the previous
planning behavior and fingerprints. Non-default controls are fingerprinted.

## Recompile original geography

From the repository root, with Python 3:

```powershell
python tools/world_scenario.py data/procedural/geography/coastal_port.json build/my-port --size 512 --seed 197707 --legend data/procedural/world_legend.json --catalog data/procedural/profiles/1098/city_buildings.json --objects data/procedural/profiles/1098/objects.json
```

Output must be a new directory. The compiler emits grayscale height, categorical
biome hints, black/white land mask, a recipe with relative asset paths and a
manifest with seed/profile/asset/output hashes. It uses only the standard library
and reads no DAT/SPR or reference artwork. Supported zone shapes are ellipses,
rectangles, polygons and stroked paths, in normalized 0..1 coordinates. Optional
`warp` is 0..0.06; flat settlement terraces should remain unwarped. The water level
is fixed at 4200 in this compiler. At least Python 3.8 is required.

The compiled recipe can be loaded in the existing World planner or passed to
`rme --generate-world`. The bundled dense-city profiles are intended for 512×512;
smaller dimensions can legitimately fail their required construction minima.
All actual outputs must still pass the editor's material/navigation checks.

## Validation and limits

Regression covers anchor order/seed stability, unsafe/out-of-range/overlapping
anchors, required civic priority, recipe round-trip, malformed profiles,
categorical PNGs, deterministic compilation and refusal to overwrite edits.
The real CLI also builds both city profiles, checks all required building types,
furniture clearance and OTBM reload.

This first pass is not a finished Thais/Venore clone. Outstanding city systems:

- Elevated wetland decks/boardwalks on a separate floor, with safe stairs and supports.
- Perimeter walls, gatehouses, functional harbour access and a landmark castle.
- Semantic districts/landmark anchors beyond the shared city center.
- Client-specific civic assets, functional depots/shops/NPCs, spawns and server QA.
- Region-limited caves/sewers instead of allocating an entire underground rectangle.

The wetland profile currently uses dry ground amid water, not Venore-style
multi-floor stilts. Water-separated settlements require an explicit transport
system; the planner warns when no terrestrial route exists. Semantic PNG previews
show the plan's terrain categories, not the client's visual art.
