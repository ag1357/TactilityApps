#!/usr/bin/env python3
import argparse,ctypes as C,json,time,hashlib,collections,statistics
from abi import *
def load(split):
 p=ROOT/'research/data'/f'{split}.jsonl';m=json.loads((p.parent/'manifest.json').read_text())
 assert hashlib.sha256(p.read_bytes()).hexdigest()==m['partitions'][split]['sha256']
 if split=='final':
  freeze=ROOT/'research/results/freeze.json'
  if not freeze.exists():raise RuntimeError('FINAL HELD-OUT IS SEALED')
  for rel,h in json.loads(freeze.read_text())['sha256'].items():assert hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()==h,rel
 return [json.loads(x) for x in p.read_text().splitlines()]
def props(r):return sorted((int(r.outputs[i]),int(r.values[i]),int(r.statuses[i])) for i in range(r.count))
def evaluate(split,modes=('oracle','B','C')):
 lib=library();worlds=load(split);summary={};rows=[]
 for mode in modes:
  metrics=collections.Counter();den=collections.Counter();classes=collections.defaultdict(lambda:collections.Counter());timings=[]
  for w in worlds:
   v=view(w)
   for q in w['cases']:
    gold=frame(q['frame']);ctx=Context(**q['context']);r=Result();start=time.perf_counter_ns()
    if mode=='oracle':lib.cg_execute(C.byref(v),C.byref(gold),C.byref(r));lib.cg_realize(C.byref(v),C.byref(r))
    else:
     # Stateful histories are executed, never replace their parser outputs with oracle bindings.
     if q['history']:
      ctx=Context()
      for h in q['history']:tmp=Result();lib.cg_respond(C.byref(v),C.byref(ctx),h.encode(),int(mode=='C'),C.byref(tmp))
     lib.cg_respond(C.byref(v),C.byref(ctx),q['text'].encode(),int(mode=='C'),C.byref(r))
    ms=(time.perf_counter_ns()-start)/1e6;timings.append(ms)
    expected=sorted((p['fact'],p['value'],p['status']) for p in q['expected'])
    checks={k:int(getattr(r.frame,k))==int(getattr(gold,k)) for k in ['entity','relation','op','epistemic','act','negation','hypothetical','second_entity','intervention','second_relation']}
    semantic_keys=['entity','relation','epistemic','act','negation','hypothetical','second_relation']
    checks['frame']=all(checks[k] for k in semantic_keys)
    checks['argument']=checks['entity'] and checks['intervention'] and checks['second_entity']
    checks['answer']=props(r)==expected and r.disposition==q['disposition']
    checks['provenance']=bool(lib.cg_verify(C.byref(v),C.byref(r)))
    checks['correct_abstention']=r.disposition==q['disposition']
    # Unsupported propositions checked independently against authorized records or gold SCM values.
    byid={f['id']:f for f in w['facts']};unsupported=0
    for i in range(r.count):
     f=byid.get(r.outputs[i]);legal=f is not None and (r.statuses[i]==f['status'] or r.frame.op==8 and f['status']==1 and r.statuses[i]==2)
     if legal and r.frame.op in [5,6,7]:
      def scm(fid,seen=()):
       if fid in seen or len(seen)>12:raise ValueError()
       ff=byid[fid]
       if r.frame.hypothetical and fid==r.frame.intervention:return 0 if r.frame.hypothetical==1 else r.frame.set_value
       return int(all(scm(d,seen+(fid,)) for d in ff['deps'])) if ff.get('deps') else ff['value']
      try:legal=legal and r.values[i]==scm(f['id'])
      except (KeyError,ValueError):legal=False
     elif legal:legal=r.values[i]==f['value']
     unsupported+=not legal
    metrics['unsupported_propositions']+=unsupported;metrics['emitted_propositions']+=r.count
    secret=w['private_truth']['secret'];metrics['truth_leaks']+=secret in r.text.decode()
    # Realizer copies only typed values; this is a proposition/token preservation check, not a human fluency score.
    additions=0
    for i in range(r.count):
     f=byid[r.outputs[i]]
     if r.frame.op not in [5,6,7] and f.get('text',str(f['value'])) not in r.text.decode():additions+=1
    metrics['realizer_omissions']+=additions
    goldids={p['fact'] for p in q['expected']};got=set(r.consulted[:r.consulted_count]);outids=set(r.outputs[:r.count])
    metrics['retrieval_gold']+=len(goldids);metrics['retrieval_hit']+=len(goldids&got);metrics['retrieval_selected']+=len(outids);metrics['retrieval_relevant']+=len(outids&goldids)
    for k,x in checks.items():metrics[k]+=x;den[k]+=1
    classes[q['kind']]['correct']+=checks['answer'];classes[q['kind']]['n']+=1
    failure='none' if checks['answer'] else 'semantic_parse' if not checks['frame'] else 'operator_policy' if not checks['op'] else 'arguments' if not checks['argument'] else 'cognition_or_retrieval'
    row={'mode':mode,'world':w['id'],'id':q['id'],'class':q['kind'],'input':q['text'],'expected':q['expected'],'expected_frame':q['frame'],'checks':checks,'failure_stage':failure,'latency_ms':ms,'legal_operation_mask':int(lib.cg_legal_mask(C.byref(v))),**trace(r)};rows.append(row)
  summary[mode]={'n':len(timings),'accuracy':{k:metrics[k]/den[k] for k in den},'counts':dict(metrics),'classes':dict(classes),'latency_ms':{'mean':statistics.mean(timings),'p95':sorted(timings)[int(.95*(len(timings)-1))]},'errors':dict(collections.Counter(x['failure_stage'] for x in rows if x['mode']==mode)), 'candidate_completeness':metrics['retrieval_hit']/max(1,metrics['retrieval_gold']),'retrieval_relevance':metrics['retrieval_relevant']/max(1,metrics['retrieval_selected'])}
 out=ROOT/'research/results';(out/f'{split}-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
 (out/f'{split}-traces.jsonl').write_text(''.join(json.dumps(x,separators=(',',':'))+'\n' for x in rows))
 print(json.dumps({k:{'answer':v['accuracy']['answer'],'frame':v['accuracy']['frame'],'provenance':v['accuracy']['provenance'],'errors':v['errors']} for k,v in summary.items()},indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('split',choices=['train','validation','test','final']);p.add_argument('--modes',nargs='+',default=['oracle','B','C']);a=p.parse_args();evaluate(a.split,a.modes)
