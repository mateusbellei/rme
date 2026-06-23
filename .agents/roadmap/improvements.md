# RME — Melhorias planejadas (caminho das pedras)

Priorizado para **produtividade de mapas** (time: 1 dev + 1 artista).  
Arte/sprites: ver `assets/.agents/` no repo pai.

---

## P1 — Data Editor: tab Grounds

**Problema:** `grounds.xml` editado na mão; tab Grounds é placeholder (`data_editor_window.cpp`).

**Caminho das pedras:**

| # | Pedra | Arquivos |
|---|-------|----------|
| 1 | Ler spec em `docs/DATA_EDITOR_PLAN.md` §5 Grounds | — |
| 2 | Substituir `CreatePlaceholderPanel(..., "Grounds")` por `CreateGroundsPanel()` | `data_editor_window.cpp/.h` |
| 3 | Listar ground brushes de `g_brushes` (tipo ground) | `brush.h`, `brushes.cpp` |
| 4 | UI: LookId, Z-Order, lista Items, Border Refs | wx listbox + spinners |
| 5 | Assign item via RAW Palette (mesmo padrão Borders) | reutilizar timer/palette sync |
| 6 | Save → `grounds.xml` via pugixml | espelhar save Borders |
| 7 | Reload materials ou merge em `g_brushes` | `materials.cpp` |
| 8 | Teste manual: checklist REDUX → Data Editor | `docs/REDUX_MIGRATION_CHECKLIST.md` |

**Critério de aceite:** editar Grass/earth sem abrir XML; save persiste após reload client data.

---

## P2 — Data Editor: tab Tilesets

**Depende de:** P1 estável.

| # | Pedra | Notas |
|---|-------|-------|
| 1 | Spec `DATA_EDITOR_PLAN.md` §6 | |
| 2 | List tilesets, filter por palette type | |
| 3 | Add/remove Brush ou Item entries | |
| 4 | Save `tilesets.xml` | |

---

## P3 — Receitas + máscaras PNG Ninja Saga

**Problema:** geração por prompt é genérica; vilas precisam layout reconhecível.

| # | Pedra | Notas |
|---|-------|-------|
| 1 | Criar máscaras PNG em repo assets (`assets/art-concept/maps/`) | vermelho=chão, verde=árvore, etc. |
| 2 | Legend JSON dedicado ou extensão de `default_legend.json` | |
| 3 | Receitas `prompt_with_image` apontando máscara | `data/procedural/recipes/` |
| 4 | Testar borderize + doodads density por bioma | `biome_doodads.json` |

Receitas base já existem: `ninja_saga_konoha_village.json`, `suna_desert`, `training_ground`.

---

## P4 — Headless / automação mapa base

**Problema:** gerar mapa exige abrir UI.

| # | Pedra | Arquivos |
|---|-------|----------|
| 1 | Wrapper CLI ou script que chama `ProceduralHeadless::Run` | novo `tools/pg_headless.cpp` ou extensão |
| 2 | Input: recipe JSON + output OTBM path | |
| 3 | Documentar no SKILL procedural | `.agents/skills/...` |
| 4 | Paridade com dialog path (checklist Phase 7) | REDUX checklist |

---

## P5 — Procedural v3.4 — templates OTBM cidade

**Spec:** `procedural-generation-v3.md` Fase 3.4 (planejado).

| # | Pedra | Notas |
|---|-------|-------|
| 1 | Chunks OTBM pré-feitos (praça, rua, casa) | `data/procedural/chunks/` |
| 2 | Stamp em grid durante geração city/forest | `procedural_common.cpp` |
| 3 | Rampas automáticas entre andares montanha | elevação existente |

---

## P6 — Sidecar LLM (baixa prioridade)

**Estado:** stub funciona sem API; LLM em `tools/procedural_sidecar.py`.

Recomendação Ninja Saga: **não priorizar**. Prompt + imagem + receitas cobrem 90% dos casos.

Se ativar: `sidecar.json` → `enabled: true`, `OPENAI_API_KEY`, validar `maskPath` merge.

---

## P7 — Modern renderer (`feat/redux`)

| # | Pedra | Notas |
|---|-------|-------|
| 1 | Preencher coluna Results no REDUX checklist | |
| 2 | FPS em mapa 10k+ tiles legacy vs modern | |
| 3 | Só tornar default após paridade visual | `settings.cpp` |

---

## O que NÃO fazer no RME

- Pipeline de sprites / PixelLab / Pixellaso (repo assets)
- Substituir Object Builder
- Depender de LLM para mapas de produção sem fallback procedural

## Branch de referência

- Procedural: `feat/procedural-generation` (spec v3)
- Renderer/zones: `feat/redux`
