# RME — Melhorias planejadas (caminho das pedras)

Priorizado para **produtividade de mapas OTServer**. Repo público — sem dependência de projeto de assets privado.

---

## P1 — Data Editor: tab Grounds

**Problema:** `grounds.xml` editado na mão; tab Grounds é placeholder.

| # | Pedra | Arquivos |
|---|-------|----------|
| 1 | Spec `docs/DATA_EDITOR_PLAN.md` §5 | — |
| 2 | `CreateGroundsPanel()` | `data_editor_window.cpp/.h` |
| 3 | Listar ground brushes | `brush.h`, `brushes.cpp` |
| 4 | UI LookId, Z-Order, Items, Border Refs | wx |
| 5 | Assign via RAW Palette | reutilizar Borders |
| 6 | Save `grounds.xml` | pugixml |
| 7 | Reload materials | `materials.cpp` |
| 8 | QA | `docs/REDUX_MIGRATION_CHECKLIST.md` |

---

## P2 — Data Editor: tab Tilesets

Depende de P1. Spec `DATA_EDITOR_PLAN.md` §6.

---

## P3 — Receitas + máscaras PNG (genérico)

**Problema:** prompts genéricos nem sempre refletem layout desejado.

| # | Pedra | Notas |
|---|-------|-------|
| 1 | Documentar formato de máscara PNG + legend | spec em `.agents/specs/` |
| 2 | Receitas `prompt_with_image` de exemplo | `data/procedural/recipes/` |
| 3 | Borderize + doodads por bioma | `biome_doodads.json` |

**Projeto privado:** máscaras e receitas temáticas vivem no repo de assets do consumidor — importar via path local no dialog, não commitar aqui.

---

## P4 — Headless / automação

| # | Pedra |
|---|-------|
| 1 | Wrapper CLI → `ProceduralHeadless::Run` |
| 2 | Input recipe JSON + output OTBM |
| 3 | Documentar no SKILL procedural |

---

## P5 — Procedural v3.4 — templates OTBM

Chunks cidade (praça, rua, casa) em `data/procedural/chunks/`.

---

## P6 — Sidecar LLM (baixa prioridade)

Preferir prompt + imagem + receitas. LLM opcional via `sidecar.json`.

---

## P7 — Modern renderer (`feat/redux`)

Preencher REDUX checklist; FPS em mapas grandes.

---

## O que NÃO fazer no RME (repo público)

- Pipeline de sprites / IA de arte
- Paths, nomes ou receitas de projetos privados
- Substituir Object Builder
