# RME Fork — `.agents` (repositório público)

Documentação para agentes e devs no **Remere's Map Editor** fork — ferramenta genérica para mapas **OTServer / OTClient**.

Este repositório é **público**. Não incluir IPs, temas, receitas ou paths de projetos privados de assets.

## Índice

| Caminho | Conteúdo |
|---------|----------|
| [roadmap/improvements.md](roadmap/improvements.md) | Melhorias planejadas |
| [specs/procedural-generation-v3.md](specs/procedural-generation-v3.md) | Spec geração procedural |
| [specs/procedural-generation-v2.md](specs/procedural-generation-v2.md) | Spec v2 (histórico) |
| [skills/rme-procedural-generation/SKILL.md](skills/rme-procedural-generation/SKILL.md) | Skill Cursor |
| `docs/DATA_EDITOR_PLAN.md` | Plano Data Editor |
| `docs/REDUX_MIGRATION_CHECKLIST.md` | QA renderer / zones |

## Estado atual

| Feature | Status |
|---------|--------|
| Procedural v3 (image/prompt/recipe) | Beta legado |
| World planner v4 (continentes, cidades, cavernas) | Opt-in (PR #7) — `docs/WORLD_GENERATION.md` |
| Exportação OTBM em lote / verificação | Opt-in (PR #7) — `--generate-world`, `--verify` |
| Authored OTBM prefabs (orientação fixa) | Opt-in — `docs/WORLD_OTBM_PREFABS.md` |
| Auto-borders runtime | Maduro |
| Data Editor → Borders | Beta |
| Data Editor → Grounds/Tilesets | Placeholder |
| Modern GL renderer | Experimental |
| TFS Zones | Beta |
| Headless API | Beta |
| Sidecar LLM | Alpha — off por padrão |

## Receitas de exemplo (genéricas)

`data/procedural/recipes/` — apenas exemplos **sem tema de projeto**:

- `example_forest.json`
- `example_village_forest.json`
- `example_desert_village.json`
- `example_training_ground.json`
- `example_continent.json`, `example_ridged_continent.json`
- `example_furnished_city_1098.json`, `example_large_continent_1098.json` (bindings públicos 10.98)
- `example_balanced_city.json`, `example_layered_town.json` (composição e plantas por andar)
- `example_diverse_continent_1098.json`, `example_layered_town_1098.json` (biomas e estilos regionais/por andar)

Receitas temáticas de um jogo específico ficam no **repositório privado de assets** do consumidor, não aqui.

## Integração com pipeline de assets

```
Projeto privado de assets (spr/dat, sheets, máscaras PNG)
        ↓
RME: carrega client → edita OTBM → geração procedural
```

Sprites / PixelLab / Pixellasso: **fora deste repo**.
