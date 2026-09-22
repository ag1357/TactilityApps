#!/usr/bin/env python3
"""Loopback qualification server. C owns semantic operations; Python owns I/O.
No Internet deployment claim: transport is plaintext and movement has no rate limit.
Resume tokens are persisted locally with mode 0600 and never included in traces.
"""
import argparse, ctypes as C, hashlib, json, os, pathlib, secrets, socketserver, threading
from qualify import ROOT,Recipe,Pos,Id
class Address(C.Structure):_fields_=[('pos',Pos),('scope',C.c_uint16)]
class Traveler(C.Structure):_fields_=[('at',Address),('destination',Address),('remaining',C.c_uint32)]
class Entity(C.Structure):_fields_=[('epoch',C.c_uint32),('revision',C.c_uint32),('owner',C.c_uint32),('quantity',C.c_uint16),('health',C.c_uint16),('decor',C.c_uint16*4),('alive',C.c_uint8),('public_access',C.c_uint8),('behavior',C.c_uint8),('reserved',C.c_uint8)]
class Player(C.Structure):_fields_=[('id',C.c_uint32),('sequence',C.c_uint32),('credits',C.c_uint32),('inventory',C.c_uint16*7),('completed',C.c_uint32*128)]
class Relationship(C.Structure):_fields_=[('trust',C.c_int16),('reliability',C.c_int16),('cooperation',C.c_int16),('aggression',C.c_int16),('confidence',C.c_uint16),('promise',C.c_uint16)]
class Feed(C.Structure):_fields_=[('revision',C.c_uint32),('actor',C.c_uint32),('epoch',C.c_uint32),('target',C.c_uint16),('kind',C.c_uint16)]
class Operation(C.Structure):_fields_=[('sequence',C.c_uint32),('epoch',C.c_uint32),('base_revision',C.c_uint32),('action',C.c_uint16),('target',C.c_uint16),('amount',C.c_uint16),('aux',C.c_uint16)]
class State(C.Structure):_fields_=[('ancestry',Id),('recipe_crc',C.c_uint32),('revision',C.c_uint32),('count',C.c_uint16),('player_count',C.c_uint16),('feed_count',C.c_uint16),('tail_count',C.c_uint16),('entities',Entity*128),('players',Player*8),('relations',(Relationship*8)*8),('feed',Feed*16),('tail',Operation*16)]
class Context(C.Structure):_fields_=[('player',C.c_uint32),('at',Address),('server_authority',C.c_uint8)]
class Disposition(C.Structure):_fields_=[('status',C.c_int),('world_changed',C.c_uint8),('rewarded',C.c_uint8),('historical',C.c_uint8),('reserved',C.c_uint8),('revision',C.c_uint32)]
ACTIONS={'EXTRACT':1,'REPAIR':2,'DAMAGE':3,'TRANSFER':4,'GRANT':5,'DECORATE':6,'AID':7,'KILL':8,'PROMISE':9,'KEEP_PROMISE':10}
def library():
    l=C.CDLL(str(ROOT/'build/libsdk.so'))
    l.ws_load.argtypes=[C.POINTER(Recipe),C.c_void_p,C.c_size_t];l.ws_state_init.argtypes=[C.POINTER(State),C.POINTER(Recipe)];l.ws_join.argtypes=[C.POINTER(State),C.c_uint32]
    l.ws_apply.argtypes=[C.POINTER(State),C.POINTER(Recipe),Context,Operation];l.ws_apply.restype=Disposition
    l.ws_move.argtypes=[C.POINTER(Recipe),C.POINTER(Address),C.c_int32,C.c_int32]
    l.ws_use_link.argtypes=[C.POINTER(Recipe),C.POINTER(Traveler)];l.ws_travel_tick.argtypes=[C.POINTER(Traveler),C.c_uint32]
    l.ws_save.argtypes=[C.POINTER(State),C.c_char_p];l.ws_restore.argtypes=[C.POINTER(State),C.POINTER(Recipe),C.c_char_p];return l
class World:
    def __init__(self,product,path):
        self.lock=threading.Lock();self.lib=library();self.recipe=Recipe();data=pathlib.Path(product).read_bytes();self.product_sha=hashlib.sha256(data).hexdigest()
        if self.lib.ws_load(C.byref(self.recipe),data,len(data)):raise ValueError('invalid product')
        self.state=State();self.path=str(path).encode();self.lib.ws_state_init(C.byref(self.state),C.byref(self.recipe));self.lib.ws_restore(C.byref(self.state),C.byref(self.recipe),self.path)
        self.key_path=pathlib.Path(str(path)+'.keys');self.keys=json.loads(self.key_path.read_text()) if self.key_path.exists() else {};self.positions={};self.connections=set()
        for p in self.state.players[:self.state.player_count]:self.positions[p.id]=self.spawn()
    def spawn(self):
        from qualify import Module
        for i,m in enumerate(self.recipe.modules[:self.recipe.count]):
            if m.flags&1 and not m.flags&64:
                out=Module();self.lib.ws_materialize(C.byref(self.recipe),i,C.byref(out));return Traveler(Address(Pos(out.pos.x,out.pos.y+300,out.pos.z),65535))
        raise ValueError('no player spawn')
    def checkpoint(self):
        if not self.lib.ws_save(C.byref(self.state),self.path):raise OSError('checkpoint failed')
    def hello(self,token):
        if token:
            if token not in self.keys:raise ValueError('invalid resume token')
            player=self.keys[token]
        else:
            player=101+self.state.player_count
            if self.lib.ws_join(C.byref(self.state),player):raise ValueError('server full')
            self.positions[player]=self.spawn();token=secrets.token_hex(24);self.keys[token]=player
            fd=os.open(self.key_path,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)
            with os.fdopen(fd,'w') as f:json.dump(self.keys,f);f.flush();os.fsync(f.fileno())
            self.checkpoint()
        if player in self.connections:raise ValueError('identity already connected')
        self.connections.add(player);return player,token
    def snapshot(self,player):
        # Public replica deliberately omits private directed behavioral vectors.
        public={'ancestry':list(self.state.ancestry.word),'recipe_sha256':self.product_sha,'revision':self.state.revision,'entities':[{'epoch':e.epoch,'revision':e.revision,'owner':e.owner,'quantity':e.quantity,'health':e.health,'alive':e.alive,'decor':list(e.decor),'public_access':e.public_access} for e in self.state.entities[:self.state.count]],'feed':[{'revision':f.revision,'actor':f.actor,'epoch':f.epoch,'target':f.target,'kind':f.kind} for f in self.state.feed[:self.state.feed_count]]}
        digest=hashlib.sha256(json.dumps(public,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        p=next(p for p in self.state.players[:self.state.player_count] if p.id==player)
        return {'world':public,'public_hash':digest,'you':{'id':player,'sequence':p.sequence,'credits':p.credits,'inventory':list(p.inventory)},'movement':{str(k):{'x':v.at.pos.x,'y':v.at.pos.y,'z':v.at.pos.z,'scope':v.at.scope} for k,v in self.positions.items()}}
    def dispatch(self,player,msg):
        cmd=msg['cmd'];status=0
        if cmd=='move':
            dx,dz=msg['dx'],msg['dz']
            if type(dx)!=int or type(dz)!=int or abs(dx)>1000 or abs(dz)>1000:raise ValueError('movement bounds')
            status=0 if self.lib.ws_move(C.byref(self.recipe),C.byref(self.positions[player].at),dx,dz) else 7
        elif cmd=='act':
            if msg.get('recipe_sha256')!=self.product_sha:raise ValueError('recipe mismatch')
            values=[msg[k] for k in ('sequence','epoch','base_revision','target','amount','aux')]
            if any(type(v)!=int or v<0 or v>(0xffffffff if i<3 else 65535) for i,v in enumerate(values)):raise ValueError('operation bounds')
            seq,epoch,rev,target,amount,aux=values
            op=Operation(seq,epoch,rev,ACTIONS[msg['action']],target,amount,aux)
            previous=bytes(self.state)
            d=self.lib.ws_apply(C.byref(self.state),C.byref(self.recipe),Context(player,self.positions[player].at,0),op);status=d.status
            if not status:
                try:self.checkpoint()
                except OSError:
                    C.memmove(C.byref(self.state),previous,len(previous));raise
        elif cmd!='snapshot':raise ValueError('unknown command')
        return {'ok':status==0,'status':status,**self.snapshot(player)}
class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        self.request.settimeout(20);player=None
        try:
            while True:
                raw=self.rfile.readline(16385)
                if not raw:return
                if len(raw)>16384 or not raw.endswith(b'\n'):raise ValueError('message too large')
                msg=json.loads(raw)
                with self.server.world.lock:
                    if player is None:
                        if msg.get('cmd')!='hello' or msg.get('schema')!=1:raise ValueError('handshake required')
                        player,token=self.server.world.hello(msg.get('token'));reply={'ok':True,'token':token,**self.server.world.snapshot(player)}
                    else:reply=self.server.world.dispatch(player,msg)
                self.wfile.write(json.dumps(reply,separators=(',',':')).encode()+b'\n');self.wfile.flush()
        except (ValueError,KeyError,TypeError,OSError,StopIteration):
            try:self.wfile.write(b'{"ok":false,"error":"protocol or checkpoint failure"}\n')
            except OSError:pass
        finally:
            if player:
                with self.server.world.lock:self.server.world.connections.discard(player)
class Server(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('product');a.add_argument('--save',default='build/world-server');a.add_argument('--port',type=int,default=7777);args=a.parse_args()
    world=World(args.product,args.save)
    with Server(('127.0.0.1',args.port),Handler) as server:
        server.world=world;print(json.dumps({'ready':True,'port':server.server_address[1]}),flush=True);server.serve_forever()
