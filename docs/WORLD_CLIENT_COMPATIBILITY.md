# Testing procedural generation with custom clients

Use a matching set of DAT, SPR, OTFI, server-to-client mappings (`items.otb`), item
XML and material brushes. A 10.98 version label and matching file signatures do
not establish semantic compatibility. In particular, an OTB with identity mappings
can load against a DAT whose items occupy different client IDs, displaying an
unrelated sprite for the same map server ID. Do not replace a custom OTB with a
public example merely to bypass a loading error.

The supplied 10.98 examples are reference profiles. Their brush names, floor IDs,
furniture and prefab contents must be checked against the target client's actual
mapping and artwork. Physical and serialization verification alone cannot identify
whether a sprite visually represents grass, sand, a wall or a different object.
Keep private asset files and custom recipes outside the public repository.

## Two generation paths

`World planner` uses `source:world`, construction catalogs and geography settings.
The dialog disables the legacy prompt/preset controls and opens a world preview.
Apply using **Apply displayed world**. Changing seed or World settings changes the
plan; changing a text prompt is not an input to this path.

Legacy text/image presets use a separate pipeline. Its City preset produces a
regular grid with pixel-level courtyard patches; it is not the World settlement
planner. Recipe JSON, construction catalogs and legends are different documents:
select an example from `data/procedural/recipes` using **Load recipe**.

## DAT animation and prefab walls

Custom DAT files can contain unset `0/0` animation durations. The loader keeps the
animator's positive default (500 ms) for those frames and reports the count in a
warning. It never stores zero-duration frames in the runtime animator. Inverted
ranges, a zero minimum with a positive maximum and durations exceeding the signed
animation budget still fail; the message identifies the sprite and frame.
The original DAT is not rewritten.

Prefab walls may visually extend one tile north/west into the reserved building
margin. Ordinary floor/furniture graphics must remain within their footprint.
The exception applies only to wall items on declared wall cells and never adds
or overwrites outside map tiles. Larger overhangs remain invalid.

To isolate a report, record executable path, recipe, selected mode, seed, OTB
identity and DAT/SPR paths. Save a small generated OTBM and compare its server IDs
with the active OTB's client-ID mapping before changing the generation algorithm.
