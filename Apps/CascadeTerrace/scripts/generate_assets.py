#!/usr/bin/env python3
"""Reproducible tiny font and Q15 heading table. Geometry is authored in render.c.
Font source: DejaVu Sans Mono, Bitstream Vera/DejaVu permissive license.
Requires Pillow only at asset generation time, never on the device.
"""
import math
from pathlib import Path
from PIL import Image, ImageFont, ImageDraw
root=Path(__file__).resolve().parents[1]
f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',8)
rows=[]
for i in range(32,127):
 im=Image.new('1',(5,8));ImageDraw.Draw(im).text((0,-2),chr(i),font=f,fill=1)
 rows.append('{'+','.join(str(sum(int(im.getpixel((x,y)))<<x for x in range(5))) for y in range(8))+'}')
(root/'core/font.inc').write_text('static const unsigned char font[95][8]={'+','.join(rows)+'};\n')
(root/'core/trig.inc').write_text('static const int16_t sine[360]={'+','.join(str(round(32767*math.sin(math.radians(i)))) for i in range(360))+'};\n')
