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
| Roadmap | `.agents/roadmap/improvements.md` — melhorias planejadas (Data Editor, headless, v3.4) |
| Índice | `.agents/README.md` |

**Arte/sprites:** repositório privado de assets do consumidor — não implementar pipeline de sprites neste repo público.

Branch legada: `feat/procedural-generation`. Para novas entregas, usar a branch da issue/PR correspondente e preservar alterações preexistentes.

## World planner v4

A nova opção `source:world` usa `world_plan`/`world_catalog` (núcleo C++ independente)
e `world_editor` (staging/brushes/validação/undo). O fluxo v3 abaixo permanece legado;
não rotear World planner pelo sidecar ou pelos presets de máscara.

Catálogos aceitam `minPerSettlement`/`maxPerSettlement`, `upperFloors` por nível
e `floorMaterials` com herança. Preservar a seed do plano ao aplicar; validar
ownership, footprints e endpoints antes de staging. `--editable-inputs` exporta
PNGs autoráveis e receita separada; a quantização não promete replay idêntico.

Contrato e exemplos: `docs/WORLD_GENERATION.md`. Receitas v4 preservam seed uint32,
referências relativas (`pathBase:recipe`), clima/relevo/biomas, catálogo e andares.
Viagens atuais são pares de teleports com chegada adjacente segura; escadas/rampas
físicas ainda não foram validadas em servidor. Não descrever o roadmap inteiro
como concluído apenas porque o plano e o OTBM são válidos.

`rme --generate-world` carrega OTB/XML/materials explicitamente e exige uma pasta
de saída nova. `--verify` usa os codecs reais para releitura/recodificação bit a
bit. `--town-prefix` é opção do exportador, não adiciona metadados de cidades à
transação de undo do editor. Limite padrão: 1.048.576 tiles; lote de 64 bits pode
pedir até 4.194.304 com `--tile-budget`. PNGs exportados são diagnósticos semânticos,
não renders dos sprites do cliente. DAT é opcional; se fornecido, deve cobrir os
itens usados. Assets privados continuam fora deste repositório público.

Validação: projeto `tests/` cobre cinco suítes; build principal com
`RME_ENABLE_WORLD_SELF_TEST=ON` cobre Editor/ActionQueue/OTBM e CLI real com os dados
públicos 10.98. No Linux, rodar o CTest do app com Xvfb. No Windows, disponibilizar
as DLLs do wx/vcpkg no PATH; usar `| Out-Host` para aguardar o CLI no PowerShell.

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

## Sidecar (v3.3)

Script: `tools/procedural_sidecar.py`  
Habilitar em `sidecar.json` (`enabled: true`) + checkbox no dialog.

| Modo | Config | Comportamento |
|------|--------|---------------|
| Stub | `llm.enabled: false` | Keywords PT/EN → preset, elevation, depth |
| LLM | `llm.enabled: true` + env `OPENAI_API_KEY` | Chat vision → JSON spec + optional `maskPath` |

Resposta merge: `preset`, `elevation`, `depthLevels`, `doodadDensity`, `maskPath`, `legendPath`.

Dependência Python opcional: `pip install Pillow` (render `maskRegions` / `maskBase64`).

## Receitas

`ProceduralRecipe::Save` / `Load` — grava v4 JSON e mantém compatibilidade com v3.

## Teste manual v3

1. Load recipe `data/procedural/recipes/example_forest.json`
2. Prompt + image com blend 35% na seleção
3. "caverna profunda" + depth 3 → z 7–10
4. Forest + doodads → árvores/props
5. Sidecar stub: `sidecar.json` enabled, `llm.enabled` false → keywords refinam preset
6. Sidecar LLM: `llm.enabled` true + `OPENAI_API_KEY` → mask/spec via API

## Próximo (v3.4)

Templates OTBM cidade, rampas automáticas entre andares de montanha.
