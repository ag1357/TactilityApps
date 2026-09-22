#!/usr/bin/env python3
"""Determinism/parity/adversarial product qualification, reproducible seed 20260922."""
import ctypes as C, copy, hashlib, json, pathlib, random, struct, time, zlib
from sdk import compile_recipe,h
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Id(C.Structure): _fields_=[('word',C.c_uint32*4)]
class Pos(C.Structure): _fields_=[('x',C.c_int32),('y',C.c_int32),('z',C.c_int32)]
class Module(C.Structure): _fields_=[('id',Id),('parent',C.c_uint16),('kind',C.c_uint16),('flags',C.c_uint16),('phos',C.c_uint16),('pos',Pos),('size',Pos),('color',C.c_uint32),('seed',C.c_uint32),('jx',C.c_int16),('jz',C.c_int16),('affinity',C.c_uint16),('quantity',C.c_uint16)]
class Link(C.Structure): _fields_=[('a',C.c_uint16),('b',C.c_uint16),('kind',C.c_uint16),('reserved',C.c_uint16)]
class Recipe(C.Structure): _fields_=[('ancestry',Id),('seed',C.c_uint32),('epoch',C.c_uint32),('revision',C.c_uint32),('crc',C.c_uint32),('count',C.c_uint16),('links_n',C.c_uint16),('modules',Module*128),('links',Link*256)]
def main():
    lib=C.CDLL(str(ROOT/'build/libsdk.so')); lib.ws_load.argtypes=[C.POINTER(Recipe),C.c_void_p,C.c_size_t];lib.ws_materialize.argtypes=[C.POINTER(Recipe),C.c_uint16,C.POINTER(Module)]
    rng=random.Random(20260922); r=Recipe(); vectors=[]; start=time.perf_counter(); worlds=0; bad=0
    for name in ('cascade','elek_grid'):
        src=json.loads((ROOT/f'content/worlds/{name}.json').read_text()); data,manifest=compile_recipe(src)
        for seed in range(1000):
            source=copy.deepcopy(src);source['seed']=seed
            product,_=compile_recipe(source); assert lib.ws_load(C.byref(r),product,len(product))==0
            generated=bytearray()
            for i,m in enumerate(source['modules']):
                out=Module();lib.ws_materialize(C.byref(r),i,C.byref(out));j=h(seed^m.get('variation_seed',m['key']))%5-2
                expected=m['position'].copy();expected[0]+=j*m.get('jitter',[0,0])[0];expected[2]+=j*m.get('jitter',[0,0])[1]
                assert [out.pos.x,out.pos.y,out.pos.z]==expected
                assert list(out.id.word)==manifest['modules'][i]['id']
                generated+=bytes(out)
            if seed in (0,1,42,999):vectors.append({'recipe':name,'seed':seed,'product_sha256':hashlib.sha256(product).hexdigest(),'materialized_sha256':hashlib.sha256(generated).hexdigest()})
            worlds+=1
        for n in range(len(data)):
            assert lib.ws_load(C.byref(r),data[:n],n)!=0;bad+=1
        for _ in range(1000):
            b=bytearray(data);i=rng.randrange(len(b));b[i]^=1<<rng.randrange(8);assert lib.ws_load(C.byref(r),bytes(b),len(b))!=0;bad+=1
        # Attacker recomputes CRC: semantic validation must still reject invalid fields.
        for offset,value in ((48+16,0),(48+18,255),(48+20,65535),(48+22,7),(48+60,1001)):
            b=bytearray(data);struct.pack_into('<H',b,offset,value);struct.pack_into('<I',b,12,zlib.crc32(b[16:]));assert lib.ws_load(C.byref(r),bytes(b),len(b))!=0;bad+=1
        for mutation in ('tag','phos','cycle','unconnected'):
            s=copy.deepcopy(src)
            if mutation=='tag':s['modules'][4]['tags'].append('Physics.Unknown')
            elif mutation=='phos':s['modules'][4]['phos']='Austentic'
            elif mutation=='cycle':s['modules'][0]['parent']=s['modules'][0]['name']
            else:s['links']=[]
            try:compile_recipe(s)
            except (ValueError,KeyError):bad+=1
            else:raise AssertionError(mutation)
    result={'status':'PASS','worlds':worlds,'rejected_malformed_products':bad,'elapsed_ms':round((time.perf_counter()-start)*1000,3),'workspace_bytes':C.sizeof(Recipe),'module_bytes':C.sizeof(Module),'vectors':vectors,'limitations':['Module connectivity is a portal graph, not continuous terrain reachability.','No P4 runtime parity measurement.','Two recipe families; not arbitrary world correctness.']}
    (ROOT/'results/worldsdk/generation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
