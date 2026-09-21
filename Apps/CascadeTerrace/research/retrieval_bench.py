#!/usr/bin/env python3
"""Same 128-byte records, exact authorized materialization, and repeated hot C cognition.
Cold means application-cache miss; OS page cache NOT evicted. No SD timing claim.
"""
import os,json,struct,tempfile,time,statistics,sys,dataclasses,random,ctypes as C
from abi import *
from vendor.aethersparse_semantic_address import SemanticAddressPlane,_OccurrenceRow,canonical_entity_id,normalize_mention

def size(obj,seen=None):
 seen=set() if seen is None else seen
 if id(obj) in seen:return 0
 seen.add(id(obj));n=sys.getsizeof(obj)
 if isinstance(obj,dict):n+=sum(size(k,seen)+size(v,seen) for k,v in obj.items())
 elif isinstance(obj,(tuple,list)):n+=sum(size(x,seen) for x in obj)
 elif hasattr(obj,'__dict__'):n+=size(vars(obj),seen)
 return n

def run():
 rows=[];rng=random.Random(781);lib=library()
 for n in [100,1000,10000,100000]:
  groups=n//20;names=[f'entity {i}' for i in range(groups)];ids=[canonical_entity_id(x) for x in names];index={name:i for i,name in enumerate(names)};id_to_group={x:i for i,x in enumerate(ids)}
  occurrence=[_OccurrenceRow(mention=names[i],target_title=names[i],target_entity_id=ids[i],occurrence_count=1,total_mention_occurrences=1,probability=1.,ambiguity_count=1,entropy_nats=0.,source_document_count=1,title_indicator=True,title_prior=1.,redirect_indicator=False,redirect_support_count=0,redirect_prior=0.,alias_types=('title',)) for i in range(groups)]
  plane=SemanticAddressPlane(occurrence,alpha=1.,requested_mention_count=groups,source_pack_sha256='0'*64)
  with tempfile.TemporaryFile() as store:
   for i in range(n):store.write(struct.pack('<IIII',i//20,i,i%19,1)+bytes(112))
   store.flush();queries=[rng.randrange(groups) for _ in range(80)]
   for mode in ['linear_stream','id_index','aethersparse_occurrence_plane']:
    for hitrate in [0.,.9,1.]:
     cache={};lat=[];material=[];bytesread=reads=hits=found=0;selected=0;fingerprints=[]
     for qi,g in enumerate(queries):
      # Explicit forced hit schedule, not a misleading nominal LRU hit rate.
      hit=(qi%10 < round(hitrate*10));key=names[g]
      if hit:cache[g]=[struct.pack('<IIII',g,j,j%19,1)+bytes(112) for j in range(g*20,g*20+20)]
      else:cache.pop(g,None)
      start=time.perf_counter_ns();result=[]
      if g in cache:result=cache[g];hits+=1
      else:
       if mode=='linear_stream':
        # Streaming reference uses constant-size scratch, not N resident records.
        for off in range(0,n*128,4096):
         b=os.pread(store.fileno(),min(4096,n*128-off),off);reads+=1;bytesread+=len(b)
         for j in range(0,len(b),128):
          if struct.unpack_from('<I',b,j)[0]==g:result.append(b[j:j+128])
       else:
        if mode=='id_index':candidates=[index.get(normalize_mention(key),-1)]
        else:candidates=[id_to_group[x.entity_id] for x in plane.distribution(key).hypotheses]
        for c in candidates:
         if c!=g:continue # common authorization filter, never supplied to address ranking
         b=os.pread(store.fileno(),20*128,c*20*128);reads+=1;bytesread+=len(b);result.extend(b[j:j+128] for j in range(0,len(b),128))
       cache[g]=result
      ms=(time.perf_counter_ns()-start)/1e6;lat.append(ms)
      if not hit:material.append(ms)
      found+=sum(struct.unpack_from('<I',b)[0]==g for b in result);selected+=len(result)
      fingerprints.append(hash(tuple(result)))
     rows.append({'records':n,'method':mode,'requested_hit_rate':hitrate,'observed_hit_rate':hits/len(queries),'candidate_completeness':found/(len(queries)*20),'precision':found/max(1,selected),'resident_python_index_bytes':0 if mode=='linear_stream' else size(index) if mode=='id_index' else size(plane)+size(id_to_group),'index_packed_estimate_bytes':0 if mode=='linear_stream' else groups*12 if mode=='id_index' else groups*52,'stream_scratch_bytes':4096 if mode=='linear_stream' else 2560,'authorized_working_wire_bytes':2560,'cold_storage_bytes':n*128,'mean_bytes_read_per_query':bytesread/len(queries),'read_calls_per_query':reads/len(queries),'read_pattern':'sequential chunks' if mode=='linear_stream' else 'one seek plus contiguous NPC region','mean_ms':statistics.mean(lat),'p95_ms':sorted(lat)[75],'materialization_mean_ms':statistics.mean(material) if material else None,'downstream_input_fingerprints':fingerprints})
   same=[x for x in rows if x['records']==n]
   assert all(x['downstream_input_fingerprints']==same[0]['downstream_input_fingerprints'] for x in same)
   for x in same:del x['downstream_input_fingerprints']
 # Same actual C downstream hot operations after materialization.
 from evaluate import load
 world=load('validation')[0];v=view(world);ctx=Context();r=Result();q=world['cases'][0]['text'].encode();times=[]
 for i in range(500):
  st=time.perf_counter_ns();lib.cg_respond(C.byref(v),C.byref(ctx),q,0,C.byref(r));times.append((time.perf_counter_ns()-st)/1e6)
 # Ambiguous address test: retain both supported entities, never force top-1.
 from dataclasses import replace
 import math
 base=occurrence[:2];ambiguity=[replace(x,mention='shared alias',total_mention_occurrences=2,probability=.5,ambiguity_count=2,entropy_nats=math.log(2)) for x in base]
 ap=SemanticAddressPlane(ambiguity,alpha=1.,requested_mention_count=1,source_pack_sha256='0'*64);ar=ap.distribution('SHARED_alias')
 report={'source_repository':'ag1357/AetherSparse','source_commit':'c3aa2ef61e6ae77a12063e47221c6e4decae3762','component':'src/aethersparse/controller/semantic_address.py: SemanticAddressPlane','adaptation':'annotation-only EntityMention import moved under TYPE_CHECKING; algorithms unchanged','scope':'Occurrence-backed exact/ambiguous mention lookup, NOT complete VSA retrieval or full corpus runtime','cache_definition':'application cache only; OS cache not evicted; Python desktop timings','rows':rows,'hot_cognition':{'view_bytes':C.sizeof(v),'mean_ms':statistics.mean(times),'p95_ms':sorted(times)[475],'read_bytes':0},'ambiguous_alias':{'candidates':len(ar.hypotheses),'mass':ar.probability_mass,'top1_forced':False},'physical_status':'PENDING; no P4/SD timing inferred','decision':'Do not adopt full runtime. Exact ID index is sufficient here; preserve explicit uncertainty concept. This does not falsify other AetherSparse mechanisms.'}
 (ROOT/'research/results/retrieval.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'rows':len(rows),'hot_cognition':report['hot_cognition'],'ambiguity':report['ambiguous_alias']}))
if __name__=='__main__':run()
