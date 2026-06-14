---
name: rme-procedural-generation
description: >-
  Desenvolve a feature de geração procedural de mapas no Remere's Map Editor (RME).
  Use ao trabalhar em ProceduralDialog, ProceduralGenerator, image mask, prompt-to-map,
  biomas, seleção de área, legend JSON, borderize, elevação, sidecar ou receitas JSON.
---

# RME Procedural Generation

## Documentação

| Versão | Spec |
|--------|------|
| v2 | `.agents/specs/procedural-generation-v2.md` |
| v3 | `.agents/specs/procedural-generation-v3.md` |

Branch: `feat/procedural-generation`

## Arquitetura v3

```
ProceduralDialog (recipe save/load, 3 modes)
  → ProceduralSidecar::TryEnhance (opcional)
  → ProceduralGenerator::Run
       ├─ ImageMask
       ├─ TextPrompt
       └─ PromptWithImage (blend reference + procedural)
            → preset router (Mountain/Ice/DeepCave/flat)
            → ScatterPresetDoodads (biome_doodads.json)
```

## Modos de geração

| Modo | source | Comportamento |
|------|--------|---------------|
| Image mask | `ImageMask` | PNG + legend |
| Text prompt | `TextPrompt` | preset procedural |
| Prompt + image | `PromptWithImage` | imagem ± blend % com procedural |

## Elevação vs profundidade

- **Mountain/Ice**: `elevation.maxLevels` → andares **acima** (z diminui: 7→6→5)
- **Cave profunda**: mesmo spin = andares **abaixo** (z aumenta: 7→8→9) + shafts
- Base ideal: **z=7** (`GROUND_LAYER`)

## Dados em `data/procedural/`

- `default_legend.json`, `ice_legend.json`
- `biome_doodads.json` — keywords por preset
- `sidecar.json` — script Python
- `recipes/` — receitas salvas

## Sidecar

Script: `tools/procedural_sidecar.py`  
Habilitar em `sidecar.json` + checkbox no dialog.  
Estende para LLM: script escreve `response.json` com `preset`, `elevation`, `depthLevels`, `maskPath`.

## Receitas

`ProceduralRecipe::Save` / `Load` — version 3 JSON.

## Teste manual v3

1. Load recipe `data/procedural/recipes/example_forest.json`
2. Prompt + image com blend 35% na seleção
3. "caverna profunda" + depth 3 → z 7–10
4. Forest + doodads → árvores/props
5. Sidecar enabled → preset refinado pelo script

## Próximo (v3.3)

HTTP LLM, vision mask PNG, templates OTBM cidade, rampas automáticas.
