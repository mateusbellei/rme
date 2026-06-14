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
| **3.3** | LLM HTTP (OpenAI-compatible), vision → mask PNG | Implementado |
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
  "timeout_ms": 60000,
  "llm": {
    "enabled": false,
    "base_url": "https://api.openai.com/v1",
    "api_key_env": "OPENAI_API_KEY",
    "model": "gpt-4o",
    "vision": true,
    "timeout_sec": 60
  }
}
```

Fluxo:
1. RME resolve região (seleção ou spin W×H)
2. RME grava `request.json` em temp (inclui bloco `llm` do config)
3. `wxExecute` roda script Python
4. Script: stub keywords **ou** HTTP chat completions (vision se imagem referência)
5. Resposta → `response.json` (`preset`, `elevation`, `depthLevels`, `doodadDensity`, `maskPath`, `legendPath`)
6. RME merge no `GenerationSpec`; `maskPath` promove modo TextPrompt → PromptWithImage

**LLM mask:** resposta JSON com `maskRegions` (retângulos RGB) ou `maskBase64` (PNG).  
Script grava `%TEMP%/rme_pg_mask.png`. Requer `pip install Pillow` para renderizar máscara.

**Sem API key:** deixe `llm.enabled: false` — stub por keywords continua funcionando.

---

## Critérios de aceite v3.1–3.3

- [ ] Modo prompt + imagem gera bioma na seleção
- [ ] Salvar/carregar receita reproduz mesma config
- [ ] Forest com doodads espalha árvores/props
- [ ] Prompt "caverna profunda" gera z=7…10 com shafts
- [ ] Sidecar desabilitado = no-op; habilitado = script roda sem crash
- [ ] Sidecar + `llm.enabled` + `OPENAI_API_KEY` refinam preset e opcionalmente geram `maskPath`

---

## v3.4 (próximo)

- Import de chunk OTBM como stamp de cidade
- Auto-ramp entre diferenças de heightmap adjacentes
