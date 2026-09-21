#!/usr/bin/env python3
"""Only consumes real device JSON. Missing evidence remains PENDING, never zero/pass."""
import json,sys,statistics
from pathlib import Path
rows=[]
for line in Path(sys.argv[1]).read_text(errors='replace').splitlines():
 try:
  row=json.loads(line[line.index('{'):]);rows.append(row)
 except (ValueError,TypeError):pass
boot=[r for r in rows if r.get('type')=='boot' and r.get('platform')=='esp32p4']
if not boot:raise SystemExit('No ESP32-P4 boot record; refusing desktop/empty input')
frames=sorted(r['period_us']/1000 for r in rows if r.get('type')=='frame' and 'period_us' in r)
saves=[r for r in rows if r.get('type')=='save']
out={'qualification':'PENDING — telemetry alone does not certify all gates','boot':boot,'heap':[r for r in rows if r.get('type')=='heap'],'frames':len(frames),'frame_mean_ms':statistics.mean(frames) if frames else None,'frame_p95_ms':frames[int(.95*(len(frames)-1))] if frames else None,'saves':saves,'dialogue':[r for r in rows if r.get('type')=='dialogue'],'still_required':['power-cut/reload on SD','CardKB2 and touch','visual inspection','peak internal and PSRAM attribution','streaming','full dialogue accuracy','unimplemented Q1 requirements']}
print(json.dumps(out,indent=2))
