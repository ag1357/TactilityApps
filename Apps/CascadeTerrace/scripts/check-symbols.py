#!/usr/bin/env python3
"""Static ABI audit, not a substitute for device load verification."""
import argparse,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('elf');p.add_argument('firmware');p.add_argument('--nm',default='riscv32-esp-elf-nm');a=p.parse_args()
exports=set()
for path in Path(a.firmware).rglob('*'):
 if path.suffix not in {'.c','.cpp','.h'}:continue
 exports.update(re.findall(r'DEFINE_MODULE_SYMBOL(?:_SIGNATURE)?\s*\(\s*(\w+)',path.read_text(errors='replace')))
imports={line.split()[-1] for line in subprocess.check_output([a.nm,'-D','-u',a.elf],text=True).splitlines() if line.split()}
missing=sorted(imports-exports)
print(json.dumps({'import_count':len(imports),'missing_from_source_exports':missing,'scope':'source table membership; runtime modules and firmware version still require validation'},indent=2))
raise SystemExit(bool(missing))
