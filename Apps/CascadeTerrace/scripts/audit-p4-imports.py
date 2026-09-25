#!/usr/bin/env python3
"""Check external ELF imports against tracked firmware module export tables."""
import argparse, hashlib, json, pathlib, re, subprocess
p=argparse.ArgumentParser();p.add_argument('--firmware',required=True);p.add_argument('--elf',default='build-p4-native/cascadeterrace.app.elf');a=p.parse_args()
elf=pathlib.Path(a.elf); firmware=pathlib.Path(a.firmware)
source=subprocess.check_output(['git','-C',str(firmware),'grep','-h','DEFINE_MODULE_SYMBOL','--','*.c','*.cpp'],text=True)
exports=set(re.findall(r'DEFINE_MODULE_SYMBOL(?:_SIGNATURE)?\(\s*(\w+)',source))
exports.update(re.findall(r'DEFINE_MODULE_SYMBOL_ALIAS\(\s*"([^" ]+)"',source))
read=subprocess.run(['readelf','--dyn-syms','--wide',str(elf)],text=True,capture_output=True,check=True)
# ESP-IDF external-app ELF omits .dynamic; its .dynsym is still authoritative.
if read.stderr.strip() not in ('','readelf: Error: no .dynamic section in the dynamic segment'):
    raise SystemExit(read.stderr)
symbols=read.stdout
imports={line.split()[-1] for line in symbols.splitlines() if ' UND ' in line and len(line.split())>=8}
if not imports: raise SystemExit("No dynamic imports found; refusing an empty audit")
missing=sorted(imports-exports)
record={'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'elf_bytes':elf.stat().st_size,'import_count':len(imports),'missing_from_pinned_export_tables':missing,'firmware_checkout':subprocess.check_output(['git','-C',str(firmware),'rev-parse','HEAD'],text=True).strip(),'scope':'Source-table membership; physical loader and enabled runtime modules remain unmeasured'}
print(json.dumps(record,indent=2));raise SystemExit(bool(missing))
