"""ctypes ABI for the actual C engine used by desktop and P4; no Python cognition surrogate."""
import ctypes as C
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
F=64; E=32; O=8; STEPS=64
class Entity(C.Structure): _fields_=[('id',C.c_uint32),('name',C.c_char*48)]
class Fact(C.Structure): _fields_=[('id',C.c_uint32),('subject',C.c_uint32),('source',C.c_uint32),('value',C.c_int32),('time',C.c_int64),('deps',C.c_uint32*2),('relation',C.c_uint8),('status',C.c_uint8),('flags',C.c_uint8),('dep_count',C.c_uint8),('text',C.c_char*96)]
class View(C.Structure): _fields_=[('facts',Fact*F),('entities',Entity*E),('count',C.c_uint16),('entity_count',C.c_uint16),('self',C.c_uint32),('player',C.c_uint32)]
class Context(C.Structure): _fields_=[('focus',C.c_uint32),('last_fact',C.c_uint32),('relation',C.c_uint8),('valid',C.c_uint8)]
class Frame(C.Structure): _fields_=[('entity',C.c_uint32),('second_entity',C.c_uint32),('intervention',C.c_uint32),('set_value',C.c_int32)]+[(x,C.c_uint8) for x in ['op','relation','second_relation','epistemic','act','negation','hypothetical','ambiguous','unresolved','temporal','confidence']]
class Step(C.Structure): _fields_=[('fact',C.c_uint32),('parents',C.c_uint32*2),('value',C.c_int32)]+[(x,C.c_uint8) for x in ['op','status','hypothetical','legal']]
class Result(C.Structure): _fields_=[('frame',Frame),('steps',Step*STEPS),('consulted',C.c_uint32*F),('outputs',C.c_uint32*O),('values',C.c_int32*O),('statuses',C.c_uint8*O),('step_count',C.c_uint16),('consulted_count',C.c_uint16)]+[(x,C.c_uint8) for x in ['count','disposition','conflict','verified']]+[('inference_ops',C.c_uint32),('text',C.c_char*1024)]
REL=['identity','occupation','relationship','location','condition','possession','goal','responsibility','trust','promise','event','observation','balance','schedule','strength','cost','requirement','age','other']
OPS=['QUERY','RECALL','COMPARE','CORROBORATE','CONTRADICT','CAUSE','CONSEQUENCE','COUNTERFACTUAL','UPDATE_BELIEF','CHECK_SOURCE','CHECK_KNOWLEDGE','ABSTAIN','ASK_CLARIFICATION']
SIZES=[13,19,6,3,2,3]
def library():
 lib=C.CDLL(str(ROOT/'build/libsemantic.so'))
 lib.cg_legal_mask.argtypes=[C.POINTER(View)]
 lib.cg_legal_mask.restype=C.c_uint32
 lib.cg_parse.argtypes=[C.POINTER(View),C.POINTER(Context),C.c_char_p,C.c_int,C.POINTER(Frame)]
 lib.cg_execute.argtypes=[C.POINTER(View),C.POINTER(Frame),C.POINTER(Result)]
 lib.cg_respond.argtypes=[C.POINTER(View),C.POINTER(Context),C.c_char_p,C.c_int,C.POINTER(Result)]
 lib.cg_verify.argtypes=[C.POINTER(View),C.POINTER(Result)]
 lib.cg_realize.argtypes=[C.POINTER(View),C.POINTER(Result)]
 lib.cg_features.argtypes=[C.POINTER(View),C.c_char_p,C.POINTER(C.c_uint16),C.c_size_t]
 lib.cg_predict.argtypes=[C.POINTER(C.c_uint16),C.c_int,C.POINTER(C.c_uint8),C.POINTER(C.c_uint8)]
 return lib
def view(world):
 v=View();v.count=len(world['facts']);v.entity_count=len(world['entities']);v.self=world['entities'][0]['id'];v.player=world['entities'][1]['id']
 assert v.count<=F and v.entity_count<=E
 for i,e in enumerate(world['entities']):v.entities[i]=Entity(e['id'],e['name'].encode())
 for i,d in enumerate(world['facts']):
  f=Fact()
  for k in ('id','subject','source','value','time','relation','status','flags'):setattr(f,k,d.get(k,0))
  f.text=d.get('text',str(f.value)).encode();f.dep_count=len(d.get('deps',[]))
  for j,x in enumerate(d.get('deps',[])):f.deps[j]=x
  v.facts[i]=f
 return v
def frame(d):
 f=Frame();f.relation=18;f.second_relation=18;f.confidence=255
 for k,x in d.items():setattr(f,k,x)
 return f
def trace(r):
 return {'frame':{k:int(getattr(r.frame,k)) for k,_ in Frame._fields_},'consulted':list(r.consulted[:r.consulted_count]),'operations':[{'op':OPS[x.op],'fact':x.fact,'parents':list(x.parents),'value':x.value,'status':x.status,'hypothetical':bool(x.hypothetical),'legal':bool(x.legal)} for x in r.steps[:r.step_count]],'propositions':[{'fact':r.outputs[i],'value':r.values[i],'status':r.statuses[i]} for i in range(r.count)],'disposition':r.disposition,'verified':bool(r.verified),'text':r.text.decode(),'inference_ops':r.inference_ops}
