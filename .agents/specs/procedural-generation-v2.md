# Procedural Generation v2 — Especificação

## Contexto (branch `feat/procedural-generation`)

Commit base: `d68a3d9` — *feat(procedural-generation): Add PG features with image mask and text prompt support*.

### Estado atual (MVP v1)

| Componente | Arquivo | Status |
|-----------|---------|--------|
| UI dialog | `source/procedural_dialog.cpp` | Image mask + text prompt, tamanho manual |
| Façade | `source/procedural_generator.cpp` | Roteia por `GenerationSource` |
| Image mask | `source/image_mask_generator.cpp` | Heurística blue=água, resto=grass; origem fixa (0,0) |
| Text prompt | `image_mask_generator.cpp` | **Stub** — popup apenas |
| Legend JSON | UI existe | **Não implementado** |
| Seleção no mapa | — | **Não implementado** |
| Paredes / doodads | — | **Não implementado** |
| Menu | `data/menubar.xml` → `GENERATE_MAP` | File → Generate Map |

### Infraestrutura RME reutilizável

- **Undo/redo**: `ActionQueue`, `BatchAction`, `Change(newTile)`
- **Seleção**: `Editor::selection`, `minPosition()` / `maxPosition()`
- **Automagic**: `Config::USE_AUTOMAGIC`, `Tile::borderize()`, `Editor::borderizeSelection()`
- **Brushes**: `g_brushes.getMap()`, `GroundBrush::draw()`, `WallBrush::draw()` + `Tile::wallize()`
- **JSON**: `json/json_spirit` já linkado no CMake
- **RNG**: `mt_seed()` / `mt_randi()` em `mt_rand.h`

---

## Objetivo v2

Automatizar criação de mapas **perfeccionista** dentro de uma **área selecionada** (ou retângulo manual), combinando:

1. Terreno / biomas (ground brushes + borderize)
2. Máscaras por imagem com legend JSON
3. Prompt textual → máscara procedural (caverna, cidade, floresta, deserto, costa)
4. Pipeline pós-geração (borderize, randomize, paredes)

---

## Arquitetura v2

```
ProceduralDialog
       │
       ▼
ProceduralGenerator::Run(spec)
       │
       ├── ImageMask ──► ProceduralBackends::GenerateFromImage
       │                      │
       │                      ▼
       │                 ProceduralCommon (resolve region, apply mask, post-process)
       │
       └── TextPrompt ──► ProceduralBackends::GenerateFromPrompt
                              │
                              ▼
                         PromptGenerator (interpret prompt → wxImage mask)
                              │
                              ▼
                         ProceduralCommon
```

### Novos módulos

| Módulo | Responsabilidade |
|--------|------------------|
| `procedural_common.h/cpp` | Região, lookup de brushes, legend JSON, pintura de tiles, pós-processamento |
| `prompt_generator.h/cpp` | Detecção de preset por keywords, algoritmos de máscara (CA cave, grid city, noise forest) |

### `GenerationSpec` estendido

```cpp
struct GenerationRegion { originX, originY, z, width, height };
enum class GenerationPreset { Auto, Forest, Desert, Cave, City, Coast };
struct GenerationPipeline { borderizeAfter, randomizeGround, placeWalls };
struct GenerationSpec {
    GenerationRegion region;
    bool useSelection;
    uint32_t seed;
    GenerationSource source;
    GenerationPreset preset;
    GenerationPipeline pipeline;
    ImageMaskRequest imageMask;
    TextPromptRequest textPrompt;
};
```

---

## Legend JSON (formato)

Arquivo exemplo: `data/procedural/default_legend.json`

```json
{
  "default": "grass",
  "entries": [
    { "color": "#00FF00", "brush": "grass" },
    { "color": "#0000FF", "brush": "water" },
    { "color": "#808080", "brush": "stone" },
    { "color": "#C0C0C0", "brush": "cave" },
    { "color": "#FFD700", "brush": "sand" },
    { "color": "#964B00", "brush": "earth" },
    { "color": "#654321", "brush": "mountain" }
  ]
}
```

- Cor `#000000` = tile ignorado (transparente)
- Matching: exato RGB → fallback heurístico se sem legend

---

## Prompt → preset (keywords)

| Preset | Keywords (PT/EN) |
|--------|------------------|
| Cave | caverna, cave, dungeon, masmorra |
| City | cidade, city, town, vila |
| Forest | floresta, forest, jungle, selva |
| Desert | deserto, desert, sand |
| Coast | costa, coast, beach, praia |
| Auto | inferir do texto; default Forest |

### Algoritmos de máscara

- **Cave**: cellular automata (45% fill → 5 iterações, vizinhos ≥4 = parede)
- **City**: grid de quarteirões + ruas (cobblestone) + blocos (stone/brick)
- **Forest**: ruído por célula + manchas de dirt/water
- **Desert**: sand dominante + patches earth
- **Coast**: gradiente horizontal water ↔ sand ↔ grass

---

## Pipeline pós-geração

Ordem de execução após pintura:

1. **borderizeAfter** (default ON): borderize em todos os tiles da região
2. **randomizeGround**: re-draw ground re-randomizável (como `randomizeSelection`)
3. **placeWalls**: Cave/City — `WallBrush` em células de parede adjacentes a chão

---

## Roadmap v3+ (fora do escopo v2)

- Integração LLM / Python sidecar (`GenerateFromPrompt` com API externa)
- Anexo de imagem de referência no prompt (vision → mask)
- Doodads automáticos (árvores, props) por bioma
- Geração multi-floor (caverna + túneis verticais)
- Templates OTBM de cidade pré-fabricados

---

## Critérios de aceite v2

- [ ] Gerar dentro da seleção retangular (single floor)
- [ ] Legend JSON mapeia cores → ground brushes
- [ ] Prompt "caverna escura" gera layout procedural com borderize
- [ ] Prompt "cidade" gera grid com ruas e opcionalmente paredes
- [ ] Undo funciona (single batch)
- [ ] Dialog reflete seleção atual (auto-fill w×h)
