#!/usr/bin/env python3
"""Regenerate templates from local originals/ and manifest.json (requires Pillow).

Adjust each agent's center_px in manifest.json, then run this script.
No external downloads or generative image tools are used.
"""
from PIL import Image
from pathlib import Path
import json

ROOT=Path(__file__).resolve().parent.parent
manifest_path=ROOT/'manifest.json'
manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
team_colors={k:tuple(v) for k,v in manifest['team_composite_rgb'].items()}

for name,meta in manifest['agents'].items():
    im=Image.open(ROOT/meta['source']).convert('RGBA')
    cx,cy=meta['center_px']
    crops={}
    for role, sizes in [('main',manifest['main_sizes']),('assist',manifest['assist_sizes'])]:
        base_width=870 if role=='main' else 720
        for w,h in sizes:
            source_h=round(base_width*h/w)
            left=round(cx-base_width/2)
            top=round(cy-source_h/2)
            rect=(left,top,left+base_width,top+source_h)
            img=im.crop(rect).resize((w,h),Image.Resampling.LANCZOS)
            rel=Path(role)/f'{w}x{h}'/f'{name}.png'
            paths=[ROOT/'templates'/rel,ROOT/'masks'/rel]
            for p in paths:p.parent.mkdir(parents=True,exist_ok=True)
            img.save(paths[0],optimize=True)
            img.getchannel('A').save(paths[1],optimize=True)
            for team,rgb in team_colors.items():
                p=ROOT/'team_backgrounds'/team/rel
                p.parent.mkdir(parents=True,exist_ok=True)
                background=Image.new('RGBA',img.size,tuple(rgb)+(255,))
                background.alpha_composite(img)
                background.convert('RGB').save(p,optimize=True)
            crops[f'{role}/{w}x{h}']=list(rect)
    meta['template_crops_px']=crops
manifest_path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(f'Regenerated templates for {len(manifest["agents"])} agents.')
