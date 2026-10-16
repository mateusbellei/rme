# Procedural recipes (examples)

Generic example recipes for **OTServer map generation**. No game-specific theme.

| File | Preset | Use |
|------|--------|-----|
| `example_forest.json` | forest | Dense forest + river |
| `example_village_forest.json` | forest | Village in forest clearing |
| `example_desert_village.json` | desert | Sandy settlement |
| `example_training_ground.json` | forest | Open grass training area |
| `example_continent.json` | world | Continent, biomes, rivers and connected settlements |
| `example_world_layers.json` | world | Buildings above surface and connected cave floors below |

Load via **File → Generate Map → Load Recipe**.

World recipes open a floor preview before applying. Their default legend is
`data/procedural/world_legend.json`; adapt brush names/IDs to the active client.
The multi-floor example requires a non-blocking teleport type in the client.
ID 0 selects one automatically; travel arrives beside the return portal.
These connections are teleports, not physical stair sprites.

Project-specific recipes (private IP, themed prompts) belong in the **consumer's private assets repository**, not in this public RME fork.
