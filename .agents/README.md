# RME Fork — `.agents` (repo RME)

Documentação para agentes e devs no **Remere's Map Editor** fork Ninja Saga.

**Gerenciado individualmente** — `/frameworks` é gitignored no repo pai de assets; commitar este `.agents` no repositório remoto do RME.

## Índice

| Caminho | Conteúdo |
|---------|----------|
| [roadmap/improvements.md](roadmap/improvements.md) | **Melhorias planejadas** — caminho das pedras |
| [specs/procedural-generation-v3.md](specs/procedural-generation-v3.md) | Spec geração procedural (implementado) |
| [specs/procedural-generation-v2.md](specs/procedural-generation-v2.md) | Spec v2 (histórico) |
| [skills/rme-procedural-generation/SKILL.md](skills/rme-procedural-generation/SKILL.md) | Skill Cursor para procedural |
| `docs/DATA_EDITOR_PLAN.md` | Plano Data Editor |
| `docs/REDUX_MIGRATION_CHECKLIST.md` | QA renderer / zones |

## Estado atual (2026-06)

| Feature | Status |
|---------|--------|
| Procedural v3 (image/prompt/recipe) | Beta — implementado |
| Auto-borders runtime | Maduro |
| Data Editor → Borders | Beta (grid 5×5, save XML) |
| Data Editor → Grounds/Tilesets | Placeholder |
| Modern GL renderer | Experimental (`feat/redux`) |
| TFS Zones | Beta |
| Headless API | Beta (`procedural_headless.h`) |
| Sidecar LLM | Alpha — desligado por padrão |

## Receitas Ninja Saga

`data/procedural/recipes/`:

- `ninja_saga_konoha_village.json`
- `ninja_saga_suna_desert.json`
- `ninja_saga_training_ground.json`
- `example_forest.json`

## Relação com repo assets

```
assets repo: PNG sheets, spr/dat releases
     ↓
RME: carrega client → edita OTBM → procedural base map
```

Não implementar pipeline de sprites neste repo.
