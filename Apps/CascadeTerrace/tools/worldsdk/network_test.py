#!/usr/bin/env python3
"""Two OS client processes, real TCP server, reconnect and disk restart checks."""
import hashlib,json,multiprocessing as mp,pathlib,socket,subprocess,sys,tempfile,time
from qualify import ROOT

def worker(pipe,port):
    token=None;sock=None;stream=None
    def connect():
        nonlocal sock,stream,token
        sock=socket.create_connection(('127.0.0.1',port),timeout=5);stream=sock.makefile('rwb');stream.write(json.dumps({'cmd':'hello','schema':1,'token':token}).encode()+b'\n');stream.flush();v=json.loads(stream.readline());token=v.pop('token',token);return v
    try:
        pipe.send(connect())
        while True:
            cmd=pipe.recv()
            if cmd=='stop':break
            if cmd=='reconnect':
                stream.close();sock.close();time.sleep(.05);reply=connect()
            else:
                stream.write(json.dumps(cmd).encode()+b'\n');stream.flush();reply=json.loads(stream.readline())
            pipe.send(reply)
    finally:
        if stream:stream.close()
        if sock:sock.close()
        pipe.close()
def main():
    traces=[];checks=0
    def check(value):
        nonlocal checks
        checks+=1
        if not value:raise AssertionError(f'check {checks}')
    with tempfile.TemporaryDirectory(prefix='ct-net-') as temp:
        save=str(pathlib.Path(temp)/'world')
        def start():
            p=subprocess.Popen([sys.executable,str(ROOT/'tools/worldsdk/server.py'),str(ROOT/'build/cascade.cws'),'--save',save,'--port','0'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            ready=json.loads(p.stdout.readline());return p,ready['port']
        server,port=start();clients=[]
        try:
            for _ in range(2):
                parent,child=mp.Pipe();p=mp.Process(target=worker,args=(child,port));p.start();clients.append((p,parent));v=parent.recv();check(v['ok'])
            def send(index,msg):
                pipe=clients[index][1];pipe.send(msg);v=pipe.recv();traces.append({'client':index,'request':msg,'status':v.get('status'),'public_hash':v.get('public_hash'),'revision':v.get('world',{}).get('revision')});return v
            def snapshot(i):return send(i,{'cmd':'snapshot'})
            def act(i,action,target,amount=1,aux=0,sequence=None):
                v=snapshot(i);e=v['world']['entities'][target];return send(i,{'cmd':'act','action':action,'target':target,'sequence':sequence if sequence is not None else v['you']['sequence']+1,'epoch':e['epoch'],'base_revision':e['revision'],'amount':amount,'aux':aux,'recipe_sha256':v['world']['recipe_sha256']})
            check(act(0,'EXTRACT',16,10)['ok']);check(act(1,'EXTRACT',16,5)['ok']);v=act(0,'REPAIR',4);check(v['ok'] and v['you']['credits']==10 and v['world']['entities'][4]['health']==100)
            check(not act(1,'REPAIR',4)['ok']);v=act(0,'TRANSFER',16,2,102);check(v['ok'] and v['you']['inventory'][1]==7)
            check(snapshot(1)['you']['inventory'][1]==7);before=v['public_hash'];check(not act(0,'TRANSFER',16,2,102,sequence=v['you']['sequence'])['ok']);check(snapshot(0)['public_hash']==before)
            check(not act(0,'DAMAGE',4)['ok'])
            for _ in range(20):check(send(0,{'cmd':'move','dx':0,'dz':100})['ok'])
            for _ in range(10):check(send(1,{'cmd':'move','dx':100,'dz':0})['ok'])
            v=snapshot(0);check(v['movement']['101']!=v['movement']['102']);check('relations' not in v['world']);check(v['public_hash']==snapshot(1)['public_hash']);check(send(0,'reconnect')['public_hash']==v['public_hash'])
            # Persisted canonical state survives a terminated and restarted server.
            expected=v['public_hash']
            for p,pipe in clients:pipe.send('stop');p.join(5);check(p.exitcode==0)
            clients=[];server.terminate();server.wait(5);server,port=start()
            # Inspect persisted world with the same C decoder through host authority.
            from server import World
            restored=World(ROOT/'build/cascade.cws',save);check(restored.snapshot(101)['public_hash']==expected)
            report={'status':'PASS','checks':checks,'clients':2,'transport':'TCP loopback; separate OS client processes','server_restart':True,'public_replica_convergence':True,'private_relationships_sent':False,'traces':traces,'limitations':['No P4 network transport qualification','No Internet deployment: plaintext transport and movement rate limits pending','Offline merge verified by C suite; network branch upload is not implemented','Clients are protocol harnesses, not integrated game clients']}
            (ROOT/'results/worldsdk/network.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='traces'}))
        finally:
            for p,pipe in clients:
                if p.is_alive():p.terminate();p.join(5)
            server.terminate();server.wait(5)
if __name__=='__main__':main()
