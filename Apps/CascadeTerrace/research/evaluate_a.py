import ctypes as C,json,sys,time,statistics
from abi import *
from evaluate import load
class Conversation(C.Structure):_fields_=[('subject',C.c_uint32),('relation',C.c_char*48),('last_ids',C.c_uint32*8),('last_count',C.c_uint8)]
class Reply(C.Structure):_fields_=[('text',C.c_char*1024),('evidence',C.c_uint32*8),('statuses',C.c_uint8*8),('count',C.c_uint8),('abstained',C.c_uint8)]
def run(split):
 lib=C.CDLL(str(ROOT/'build/librunga.so'));lib.rung_a_run.argtypes=[C.POINTER(View),C.POINTER(Conversation),C.c_char_p,C.POINTER(Reply)];rows=[]
 for w in load(split):
  v=view(w)
  for q in w['cases']:
   ctx=Conversation();r=Reply()
   for h in q['history']:lib.rung_a_run(C.byref(v),C.byref(ctx),h.encode(),C.byref(r))
   t=time.perf_counter_ns();lib.rung_a_run(C.byref(v),C.byref(ctx),q['text'].encode(),C.byref(r));ms=(time.perf_counter_ns()-t)/1e6
   got=sorted((r.evidence[i],r.statuses[i]) for i in range(r.count));exp=sorted((x['fact'],x['status']) for x in q['expected'])
   rows.append({'world':w['id'],'id':q['id'],'class':q['kind'],'input':q['text'],'text':r.text.decode(),'evidence':got,'expected_evidence':exp,'exact_evidence_match':got==exp and (bool(r.abstained)==(q['disposition']!=0)), 'provenance_ids_valid':all(any(f['id']==fid and f['status']==st for f in w['facts']) for fid,st in got),'latency_ms':ms})
 out=ROOT/'research/results';(out/f'{split}-A-traces.jsonl').write_text(''.join(json.dumps(x)+'\n' for x in rows));summary={'n':len(rows),'exact_evidence_match':sum(x['exact_evidence_match'] for x in rows)/len(rows),'provenance_ids_valid':sum(x['provenance_ids_valid'] for x in rows)/len(rows),'mean_ms':statistics.mean(x['latency_ms'] for x in rows),'caveat':'Citation matching is an upper bound, NOT semantic proposition correctness. Private patch frame/operator interfaces are not equivalent to the general schema. No per-stage accuracy fabricated.'};(out/f'{split}-A-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary)
if __name__=='__main__':run(sys.argv[1])
