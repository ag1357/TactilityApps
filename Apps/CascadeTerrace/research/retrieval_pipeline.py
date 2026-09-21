"""Supplemental end-to-end parity audit: materialized benchmark records actually
feed the same C operator and verifier. Not a replacement for retrieval timings.
No final-held input or cognition implementation changes.
"""
import os,struct,tempfile,json,ctypes as C,zlib
from abi import ROOT,View,Entity,Fact,Result,frame,library
from vendor.aethersparse_semantic_address import SemanticAddressPlane,_OccurrenceRow,canonical_entity_id
lib=library();checks=[]
for n in [100,1000,10000,100000]:
 groups=n//20;names=[f'entity {i}' for i in range(groups)];ids=[canonical_entity_id(x) for x in names];index={s:i for i,s in enumerate(names)};reverse={s:i for i,s in enumerate(ids)}
 rows=[_OccurrenceRow(names[i],names[i],ids[i],1,1,1.,1,0.,1,True,1.,False,0,0.,('title',)) for i in range(groups)]
 plane=SemanticAddressPlane(rows,alpha=1.,requested_mention_count=groups,source_pack_sha256='0'*64)
 with tempfile.TemporaryFile() as f:
  for i in range(n):f.write(struct.pack('<IIII',i//20+1,i+1,i%19,i%2)+bytes(112))
  f.flush()
  for g in [0,groups//2,groups-1]:
   outputs=[]
   for method in ['linear_stream','id_index','aethersparse_occurrence_plane']:
    material=[]
    if method=='linear_stream':
     for off in range(0,n*128,4096):
      b=os.pread(f.fileno(),min(4096,n*128-off),off)
      material.extend(b[j:j+128] for j in range(0,len(b),128) if struct.unpack_from('<I',b,j)[0]==g+1)
    else:
     candidates=[index[names[g]]] if method=='id_index' else [reverse[h.entity_id] for h in plane.distribution(names[g]).hypotheses]
     for c in candidates:
      if c!=g:continue
      b=os.pread(f.fileno(),2560,c*2560);material.extend(b[j:j+128] for j in range(0,len(b),128))
    v=View();v.entity_count=1;v.entities[0]=Entity(g+1,names[g].encode());v.self=g+1;v.count=len(material)
    for j,b in enumerate(material):
     subject,fid,relation,value=struct.unpack_from('<IIII',b);v.facts[j]=Fact(id=fid,subject=subject,source=g+1,value=value,relation=relation,status=1,text=str(value).encode())
    q=frame(dict(entity=g+1,relation=2,op=0));trace=Result();lib.cg_execute(C.byref(v),C.byref(q),C.byref(trace));lib.cg_realize(C.byref(v),C.byref(trace))
    assert trace.count and lib.cg_verify(C.byref(v),C.byref(trace))
    result=(list(trace.outputs[:trace.count]),list(trace.values[:trace.count]),list(trace.statuses[:trace.count]),trace.text.decode())
    outputs.append(result);checks.append({'records':n,'authorized_entity':g+1,'method':method,'materialized_bytes':len(material)*128,'working_view_bytes':C.sizeof(v),'result':result,'provenance_valid':True})
   assert outputs[0]==outputs[1]==outputs[2]
report={'purpose':'actual common C downstream audit for controlled retrieval benchmark','checks':len(checks),'identical_method_outputs':True,'all_provenance_valid':True,'timing_scope':'No performance claims; primary retrieval timings remain retrieval.json','rows':checks}
(ROOT/'research/results/retrieval-pipeline.json').write_text(json.dumps(report,indent=2)+'\n');print({'checks':len(checks),'identical_method_outputs':True})
