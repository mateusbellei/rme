"""Create a self-contained, offline decoration review from generation.json.

Usage: python tools/world_quality.py path/to/generation.json [output.html]
Counts describe generated anchors, not sprite area or an aesthetic grade.
"""
import html
import json
from pathlib import Path
import sys


def render(report):
    regions = report.get("decorationRegions", [])
    if len(regions) > 131072:
        raise ValueError("Too many diagnostic regions")
    for region in regions:
        position = region["position"]
        if len(position) != 3 or any(type(v) is not int for v in position):
            raise ValueError("Invalid region position")
        if not (0 <= position[0] <= 65535 and 0 <= position[1] <= 65535 and 0 <= position[2] <= 15):
            raise ValueError("Region position outside map bounds")
        if any(type(region[k]) is not int or not 0 <= region[k] <= 32 for k in ("width", "height")):
            raise ValueError("Invalid region dimensions")
        if any(type(region[k]) is not int or region[k] < 0 for k in ("naturalTiles", "objectAnchors", "scenes")):
            raise ValueError("Invalid region count")
    # JSON is embedded as inert data. Escape HTML delimiters, including profile IDs.
    payload = json.dumps({"regions": regions, "brushes": report.get("objectBrushUsage", {}),
                          "patches": report.get("objectPatchUsage", {}), "variants": report.get("sceneVariantUsage", {})}, ensure_ascii=True).replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")
    title = html.escape(str(report.get("planFingerprint", "unknown")))
    return '''<!doctype html><html lang="pt-BR"><meta charset="utf-8"><title>Revisão procedural</title>
<style>body{font:16px system-ui;background:#142021;color:#e3eddf;max-width:1120px;margin:30px auto;padding:0 18px}h1{font-size:25px}select{padding:7px;background:#263636;color:inherit;border:1px solid #607567}canvas{width:100%;image-rendering:pixelated;background:#0b1417;margin-top:15px}#hint{min-height:48px}table{width:100%;border-collapse:collapse}td,th{text-align:left;padding:7px;border-bottom:1px solid #304242}.columns{display:grid;grid-template-columns:repeat(3,1fr);gap:24px}.muted{color:#a9c1b9}small{font-size:13px}@media(max-width:750px){.columns{grid-template-columns:1fr}}</style>
<h1>Distribuição de natureza e lugares</h1><p class="muted">Plano ''' + title + '''. Cada célula representa uma região de até 32 × 32 tiles. Passe o mouse para inspecionar.</p>
<label>Piso <select id="floor"></select></label> <label>Medida <select id="metric"><option value="density">Objetos por 100 tiles naturais</option><option value="objectAnchors">Âncoras de objetos</option><option value="scenes">Cenas</option></select></label>
<canvas id="map"></canvas><p id="hint">Regiões sem tiles naturais elegíveis ficam cinza. A intensidade indica quantidade relativa no piso.</p>
<p class="muted"><small>As contagens abrangem somente os pisos e a seleção processados. Objetos compostos contam uma âncora por brush; cobertura visual e tamanho dos sprites variam. Baixa densidade pode ser uma escolha de paisagem. Este relatório não mede beleza, jogabilidade ou distância de viagem.</small></p>
<div class="columns"><section><h2>Objetos utilizados</h2><table id="brushes"></table></section><section><h2>Manchas utilizadas</h2><table id="patches"></table></section><section><h2>Variantes utilizadas</h2><table id="variants"></table></section></div>
<script id="data" type="application/json">''' + payload + '''</script><script>
const data=JSON.parse(document.getElementById('data').textContent),canvas=document.getElementById('map'),ctx=canvas.getContext('2d'),floor=document.getElementById('floor'),metric=document.getElementById('metric');
let shown=[],bounds={x:0,y:0,w:1,h:1};
for(const z of [...new Set(data.regions.map(r=>r.position[2]))].sort((a,b)=>a-b)){const o=document.createElement('option');o.value=z;o.textContent='z='+z;floor.append(o);}
function value(r){return metric.value==='density'?(r.naturalTiles?100*r.objectAnchors/r.naturalTiles:0):r[metric.value];}
function draw(){shown=data.regions.filter(r=>r.position[2]===Number(floor.value));if(!shown.length){canvas.width=1;canvas.height=1;return;}const x=Math.min(...shown.map(r=>r.position[0])),y=Math.min(...shown.map(r=>r.position[1]));bounds={x,y,w:Math.max(...shown.map(r=>r.position[0]+r.width))-x,h:Math.max(...shown.map(r=>r.position[1]+r.height))-y};canvas.width=bounds.w;canvas.height=bounds.h;const max=Math.max(1,...shown.map(value));ctx.fillStyle='#0b1417';ctx.fillRect(0,0,canvas.width,canvas.height);for(const r of shown){ctx.fillStyle=r.naturalTiles?'hsl('+Math.round(180-130*value(r)/max)+' 55% '+Math.round(25+35*value(r)/max)+'%)':'#384442';ctx.fillRect(r.position[0]-x,r.position[1]-y,r.width-1,r.height-1);}}
canvas.addEventListener('mousemove',e=>{const b=canvas.getBoundingClientRect(),x=bounds.x+(e.clientX-b.left)*canvas.width/b.width,y=bounds.y+(e.clientY-b.top)*canvas.height/b.height;const r=shown.find(r=>x>=r.position[0]&&y>=r.position[1]&&x<r.position[0]+r.width&&y<r.position[1]+r.height);if(r)document.getElementById('hint').textContent='('+r.position.join(', ')+') — '+r.naturalTiles+' tiles naturais elegíveis, '+r.objectAnchors+' âncoras, '+r.scenes+' cenas; '+(r.naturalTiles?100*r.objectAnchors/r.naturalTiles:0).toFixed(1)+' objetos por 100 tiles.';});
for(const key of ['brushes','patches','variants']){const table=document.getElementById(key);for(const [name,count]of Object.entries(data[key]).sort((a,b)=>b[1]-a[1])){const row=table.insertRow();row.insertCell().textContent=name;row.insertCell().textContent=count;}}
floor.addEventListener('change',draw);metric.addEventListener('change',draw);draw();</script></html>'''


def main():
    source = Path(sys.argv[1])
    output = Path(sys.argv[2]) if len(sys.argv) > 2 else source.with_name("quality.html")
    output.write_text(render(json.loads(source.read_text(encoding="utf-8-sig"))), encoding="utf-8")
    print(output.resolve())


if __name__ == "__main__":
    main()
