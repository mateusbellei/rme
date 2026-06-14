# Procedural Generation v3 — Roadmap & Especificação

## Pré-requisitos (v2 concluído)

- Seleção de área, legend JSON, presets (Forest…Ice), elevação multi-floor (Mountain/Ice)
- Doodads ice, pipeline borderize/walls, prompt keywords PT/EN

---

## Objetivo v3

Elevar a feature de **geração mínima** para **workflow de produção**: combinar texto + imagem, reutilizar receitas, enriquecer biomas com props, cavernas em profundidade, e ponte para IA externa.

---

## Fases v3

| Fase | Entrega | Status |
|------|---------|--------|
| **3.1** | Prompt + imagem de referência, receitas JSON, doodads por bioma | Implementado |
| **3.2** | Caverna profunda multi-floor (z+1…), sidecar Python | Implementado |
| **3.3** | LLM HTTP (OpenAI-compatible), vision → mask PNG | Planejado |
| **3.4** | Templates OTBM (city chunks), rampas entre andares | Planejado |

---

## Arquitetura v3

```
ProceduralDialog
  ├─ Save/Load Recipe (.json)
  └─ Generate
        │
        ▼
ProceduralSidecar::TryEnhance(spec)   ← opcional, script Python
        │
        ▼
ProceduralGenerator::Run
  ├─ ImageMask
  ├─ TextPrompt
  └─ PromptWithImage  ← texto + PNG referência (blend opcional)
        │
        ▼
Preset router (Mountain / Ice / DeepCave / flat biomes)
        │
        ▼
ProceduralCommon + ScatterPresetDoodads(biome_doodads.json)
```

---

## Modo Prompt + Imagem

- **Referência**: PNG/JPG anexado no modo prompt
- **Sem blend**: imagem vira máscara 1:1 (como image mask)
- **Com blend** (`referenceWeight` 1–100): mistura imagem + máscara procedural do preset
- Preset/pipeline/doodads ainda vêm do prompt e UI

---

## Receita JSON (`data/procedural/recipes/`)

```json
{
  "version": 3,
  "source": "prompt_with_image",
  "prompt": "floresta densa com rio",
  "referenceImage": "C:/masks/forest.png",
  "referenceWeight": 40,
  "preset": "forest",
  "seed": 1337,
  "useSelection": true,
  "elevation": 0,
  "doodads": { "enabled": true, "density": 14 },
  "pipeline": { "borderize": true, "randomize": false, "walls": false },
  "legend": "data/procedural/default_legend.json"
}
```

Funções: `ProceduralRecipe::Save`, `ProceduralRecipe::Load`

---

## Doodads por bioma (`biome_doodads.json`)

Registry mapeia preset → keywords + densidade default.  
`ScatterPresetDoodads` usa após geração de terreno quando checkbox ativo.

| Preset | Keywords exemplo |
|--------|------------------|
| Forest | tree, bush, fern, mushroom, log |
| Desert | cactus, dead, desert |
| Coast | palm, coral, shell |
| Cave | stalagmite, crystal, rock |
| Ice | snow, ice, frost (já existente) |

---

## Caverna profunda (multi-floor underground)

Convenção Tibia: **underground = z maior** (8, 9, 10… a partir de base z=7).

- `depthLevels` (UI ou keyword "profunda"/"deep")
- Cada andar: CA cave independente (seed + floor)
- **Shafts**: conectam células de chão entre andares adjacentes
- Superfície (z base): pedra/earth com entradas

---

## Sidecar Python

Config: `data/procedural/sidecar.json`

```json
{
  "enabled": false,
  "script": "tools/procedural_sidecar.py",
  "timeout_ms": 30000
}
```

Fluxo:
1. RME grava `request.json` em temp
2. `wxExecute` roda script
3. Script lê request, grava `response.json` (preset, maskPath, depth, etc.)
4. RME merge no `GenerationSpec`

Script exemplo incluído — ponto de extensão para LLM local/remota.

---

## Critérios de aceite v3.1–3.2

- [ ] Modo prompt + imagem gera bioma na seleção
- [ ] Salvar/carregar receita reproduz mesma config
- [ ] Forest com doodads espalha árvores/props
- [ ] Prompt "caverna profunda" gera z=7…10 com shafts
- [ ] Sidecar desabilitado = no-op; habilitado = script roda sem crash

---

## v3.3+ (próximo)

- HTTP POST OpenAI-compatible (`/v1/chat/completions` + image)
- Resposta → `mask.png` + `GenerationSpec` parcial
- Import de chunk OTBM como stamp de cidade
- Auto-ramp entre diferenças de heightmap adjacentes
