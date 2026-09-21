#!/usr/bin/env python3
"""Kyra low-poly rig: local millimetres, half-width/height/half-depth, RGB, bone.
Positive/negative bone IDs apply opposite leg/arm swing. Authored geometry.
"""
import struct,json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
parts=[[-200,0,0,130,720,140,0x253647,1],[200,0,0,130,720,140,0x253647,-1],[0,650,0,350,600,220,0x39bcc5,0],[0,1260,0,230,390,220,0xd9a376,0],[0,1590,0,270,170,250,0x493e66,0],[-480,750,0,100,450,120,0x39bcc5,-1],[480,750,0,100,450,120,0x39bcc5,1],[0,1280,231,190,80,20,0x79dce3,0],[290,720,0,120,200,290,0xc09755,0]]
(root/'assets/kyra.json').write_text(json.dumps({'format':'CTM1','parts':parts},indent=2)+'\n')
(root/'assets/kyra.mesh').write_bytes(b'CTM1'+struct.pack('<H',len(parts))+b''.join(struct.pack('<6hIh',*p) for p in parts))
(root/'core/kyra.inc').write_text('static MeshPart kyra_parts[16]={'+','.join('{'+','.join(str(v) for v in p)+'}' for p in parts)+'};\nstatic int kyra_count='+str(len(parts))+';\n')
