---
name: rme-procedural-generation
description: >-
  Desenvolve a feature de geração procedural de mapas no Remere's Map Editor (RME).
  Use ao trabalhar em ProceduralDialog, ProceduralGenerator, image mask, prompt-to-map,
  biomas, seleção de área, legend JSON, borderize ou paredes automáticas.
---

# RME Procedural Generation

## Quando usar

- Alterar geração de mapas por imagem ou prompt
- Adicionar presets (caverna, cidade, floresta)
- Integrar com seleção do editor ou pipeline de brushes

## Leitura obrigatória

1. Spec: `.agents/specs/procedural-generation-v2.md`
2. Branch: `feat/procedural-generation`

## Arquitetura

```
ProceduralDialog → ProceduralGenerator::Run → backends
  ImageMask  → image_mask_generator.cpp → ProceduralCommon
  TextPrompt → prompt_generator.cpp       → ProceduralCommon
```

## Regras de implementação

### Undo / tiles

Sempre usar `ActionQueue`:

```cpp
BatchAction* batch = editor.actionQueue->createBatch(ACTION_DRAW);
Action* action = editor.actionQueue->createAction(batch);
// ... deepCopy tile, modify, action->addChange(newd Change(newTile))
batch->addAndCommitAction(action);
editor.addBatch(batch, 2);
```

### Região de geração

- `ProceduralCommon::ResolveRegion(editor, spec, error)` — preenche `spec.region`
- Com `useSelection`: `selection.minPosition()` / `maxPosition()`, single floor only
- Posição final: `originX + x`, `originY + y`, `region.z`

### Brushes

- Lookup: `ProceduralCommon::FindGroundBrush("grass")` — substring case-insensitive em `g_brushes`
- Paredes: `FindWallBrush("stone")` + `draw()` + `tile->wallize(&map)` se automagic

### Legend JSON

Formato em `data/procedural/default_legend.json`. Parser em `procedural_common.cpp`.

### Prompt

Keywords PT/EN em `PromptGenerator::DetectPreset()`. Não chamar APIs externas no v2.

### Estilo

- Tabs, `newd`, `wxString`, padrão RME existente
- Não alterar arquivos unrelated (zones, items.otb, etc.)

## Teste manual

1. Abrir mapa, selecionar retângulo no floor 7
2. File → Generate Map → Use selection → Image mask + legend
3. Prompt: "caverna escura" com borderize + walls
4. Ctrl+Z deve desfazer tudo

## Próximos passos (v3)

Ver roadmap em `.agents/specs/procedural-generation-v2.md`
