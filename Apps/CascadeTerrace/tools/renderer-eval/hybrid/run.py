#!/usr/bin/env python3
"""Compare the candidate against the actual ca942d3 renderer, not a reimplementation."""
from pathlib import Path
import subprocess,sys,json,platform,hashlib,os
app=Path(__file__).resolve().parents[3]
build=app/'build/renderer-hybrid';build.mkdir(parents=True,exist_ok=True)
ref_sha='ca942d39f4d9677a3015739e013818598b820b41'
original=subprocess.check_output(['git','show',ref_sha+':Apps/CascadeTerrace/core/render.c'],cwd=app,text=True)
names=['render_load_assets','render','render_world','render_bind_state','render_world_peer','render_world_marker','draw_text','draw_panel','screenshot']
source=''.join('#define '+x+' reference_'+x+'\n' for x in names)+original+'''
void reference_raw(Renderer* r,const float* v,uint32_t col,float shade,float illumination,int clip) {
 rr=r;cy=cp=1;sy=sp=0;light=illumination;
 V a={v[0],v[1],v[2]},b={v[3],v[4],v[5]},c={v[6],v[7],v[8]};
 if(clip)tri(a,b,c,col,shade);else raster(a,b,c,col,shade);
}
'''
(build/'reference.c').write_text(source)
production=''.join('#define '+x+' production_'+x+'\n' for x in names)+(app/'core/render.c').read_text()+source[source.index('void reference_raw'):].replace('reference_raw','production_raw')
(build/'production.c').write_text(production)
flags=['-O2','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Icore']
if '--sanitize' in sys.argv: flags+=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
cmd=['cc']+flags+['tools/renderer-eval/hybrid/qualification.c',str(build/'reference.c'),str(build/'production.c'),'core/state.c','core/world.c','content/cascade_adapter.c']+[str(p.relative_to(app)) for p in sorted((app/'world').glob('*.c'))]+['-lm','-o',str(build/'qualification')]
subprocess.run(cmd,cwd=app,check=True)
p=subprocess.run([str(build/'qualification')],cwd=app,stdout=subprocess.PIPE,text=True,check=True)
data=json.loads(p.stdout);data['host']=platform.platform();data['reference_commit']=ref_sha;data['reference_sha256']=hashlib.sha256(original.encode()).hexdigest();data['candidate_sha256']=hashlib.sha256((app/'core/render.c').read_bytes()).hexdigest();data['compiler']=subprocess.check_output(['cc','--version'],text=True).splitlines()[0];data['sanitized']='--sanitize' in sys.argv;data['asan_options']=os.environ.get('ASAN_OPTIONS','');data['timing_note']='Uninstrumented production/reference translation units; raster replay includes shared dispatch and depth clear; seven interleaved 100-frame batches; p95 over 700 individual frame samples.'
for scene in data['scenes']:
 import statistics
 scene['median']={k:statistics.median(x[k] for x in scene['samples']) for k in scene['samples'][0]}
 m=scene['median'];scene['raster_replay_speedup']=m['reference_raster_replay_ms']/m['candidate_raster_replay_ms'];scene['frame_speedup']=m['reference_frame_ms']/m['candidate_frame_ms']
 print(scene['scene'],scene['raster_replay_speedup'],scene['frame_speedup'])
path=app/'results/worldsdk/renderer-hybrid'/('sanitized.json' if data['sanitized'] else 'desktop.json');path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(data,indent=2)+'\n')
