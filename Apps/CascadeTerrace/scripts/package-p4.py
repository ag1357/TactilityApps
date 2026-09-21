#!/usr/bin/env python3
"""Uses the exact v3 USTAR layout implemented by repository tactility.py."""
from pathlib import Path
import tarfile,hashlib,json,struct,sys,io
root=Path(__file__).resolve().parents[1]
elf=root/'build-p4-native/cascadeterrace.app.elf'
b=elf.read_bytes()
if b[:4]!=b'\x7fELF' or b[4]!=1 or struct.unpack_from('<H',b,18)[0]!=243:raise SystemExit('Not a 32-bit RISC-V ELF')
if len(b)>4*1024*1024:raise SystemExit('Binary exceeds 4 MiB')
out=root/'build/ag1357.cascadeterrace.app';out.parent.mkdir(exist_ok=True)
with tarfile.open(out,'w',format=tarfile.USTAR_FORMAT) as tar:
 if '--qualification' in sys.argv:
  marker=tarfile.TarInfo('assets/qualification.flag');marker.size=1;tar.addfile(marker,io.BytesIO(b'1'))
 for src,dst in [(root/'manifest.properties','manifest.properties'),(elf,'bin/esp32p4/cascadeterrace.elf')]+[(p,'assets/'+p.name) for p in sorted((root/'assets').iterdir()) if p.is_file()]:
  info=tar.gettarinfo(str(src),dst);info.uid=info.gid=0;info.uname=info.gname='';info.mtime=0
  with src.open('rb') as f:tar.addfile(info,f)
record={'status':'P4 BUILD PRODUCED; PENDING PHYSICAL VALIDATION','binary_bytes':len(b),'package_bytes':out.stat().st_size,'elf_sha256':hashlib.sha256(b).hexdigest(),'package_sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'format':'Tactility manifest v0.3 / USTAR / esp32p4 external ELF'}
(root/'results/p4-build.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
