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
| `example_city_blocks.json` | world | Larger street blocks with multiple floors |
| `example_balanced_city.json` | world | Required houses, inn and courtyard hall in each settlement |
| `example_ridged_continent.json` | world | Mountain ridges, climate detail and distributed cave entrances |
| `example_furnished_city_1098.json` | world | Public 10.98 materials and required rotated furniture |
| `example_large_continent_1098.json` | world | 1024-square continent; batch tile budget required |

Load via **File → Generate Map → Load Recipe**.

World recipes open a floor preview before applying. Their default legend is
`data/procedural/world_legend.json`; adapt brush names/IDs to the active client.
The multi-floor example requires a non-blocking teleport type in the client.
ID 0 selects one automatically; travel arrives beside the return portal.
These connections are teleports, not physical stair sprites.

Project-specific recipes (private IP, themed prompts) belong in the **consumer's private assets repository**, not in this public RME fork.
