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
| Procedural v3 (image/prompt/recipe) | Beta |
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

Receitas temáticas de um jogo específico ficam no **repositório privado de assets** do consumidor, não aqui.

## Integração com pipeline de assets

```
Projeto privado de assets (spr/dat, sheets, máscaras PNG)
        ↓
RME: carrega client → edita OTBM → geração procedural
```

Sprites / PixelLab / Pixellasso: **fora deste repo**.
