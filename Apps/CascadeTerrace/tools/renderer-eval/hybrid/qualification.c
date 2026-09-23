#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"
#include "presentation.h"
typedef struct { float v[9]; uint32_t col; float shade,light; } Captured;
static Captured captured[65536];
static unsigned captured_count;
static int capture_enabled;
static void capture(float ax,float ay,float az,float bx,float by,float bz,float cx,float cy_,float cz,uint32_t col,float shade,float illumination) {
    if (!capture_enabled) return;
    if (captured_count==65536) { fprintf(stderr,"capture capacity exceeded\n"); exit(2); }
    captured[captured_count++] = (Captured){{ax,ay,az,bx,by,bz,cx,cy_,cz},col,shade,illumination};
}
#define CT_CAPTURE_RASTER(a,b,c,col,shade) capture(a.x,a.y,a.z,b.x,b.y,b.z,c.x,c.y,c.z,col,shade,light)
#include "../../../core/render.c"
void production_render(Renderer*,const Game*);
void production_render_world(Renderer*,const WsRecipe*,WsAddress,int,WsMetrics*);
void production_raw(Renderer*,const float*,uint32_t,float,float,int);
void reference_render(Renderer*,const Game*);
void reference_render_world(Renderer*,const WsRecipe*,WsAddress,int,WsMetrics*);
void reference_raw(Renderer*,const float*,uint32_t,float,float,int);
static Renderer candidate, reference;
static uint16_t scanout[W*H*4];
static Game game;
static WsRecipe recipe;
static uint8_t product[65536];
static unsigned checks, triangles_tested, frames_tested;
static uint32_t rng=0x783a25c1;
static uint32_t random32(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static float uniform(float lo,float hi) { return lo+(hi-lo)*(random32()>>8)*(1.f/16777216.f); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static void compare(const char* name,unsigned id) {
    for(int i=0;i<W*H;i++) if(candidate.pixels[i]!=reference.pixels[i] || candidate.depth[i]!=reference.depth[i]) {
        fprintf(stderr,"%s %u pixel %d color %u/%u depth %u/%u\n",name,id,i,candidate.pixels[i],reference.pixels[i],candidate.depth[i],reference.depth[i]); exit(1);
    }
    if(candidate.triangles!=reference.triangles || candidate.pixels_written!=reference.pixels_written) { fprintf(stderr,"counter mismatch %s %u\n",name,id);exit(1); }
    checks++;
}
static void reset(void) {
    memset(&candidate,0,sizeof(candidate));
    for(int i=0;i<W*H;i++) { candidate.pixels[i]=(uint16_t)(i*13); candidate.depth[i]=(i%7)?65535:(uint16_t)(20+i%64000); }
    reference=candidate;
}
static void candidate_raw(Renderer* r,const float* v,uint32_t col,float shade,float illumination,int clip) {
    rr=r; cy=cp=1; sy=sp=0; light=illumination;
    V a={v[0],v[1],v[2]},b={v[3],v[4],v[5]},c={v[6],v[7],v[8]};
    if(clip) tri(a,b,c,col,shade); else raster(a,b,c,col,shade);
}
static void primitive_tests(void) {
    for(unsigned n=0;n<12000;n++) {
        float v[9];
        for(int k=0;k<3;k++) {
            float z=uniform(.2f,600.f),x=uniform(-300.f,540.f),y=uniform(-200.f,360.f);
            if(n%6==0) { x=100.f+k*.12f; y=50.f+(k==1?.16f:0); }
            if(n%6==1) { x=uniform(-20000,20000); y=uniform(-20000,20000); }
            if(n%6==2) { x=floorf(x)+.5f; y=floorf(y)+.5f; }
            if(n%6==3) { x=40+k*70.f; y=40+k*25.f+uniform(-.0001f,.0001f); }
            if(n%6==4) { x=uniform(0,240); y=uniform(0,160); z=uniform(.2f,1); }
            v[3*k]=(x-120)*z/145; v[3*k+1]=(78-y)*z/145; v[3*k+2]=z;
        }
        int clip=n%6==5;
        if(clip) { v[2]=uniform(-2.f,.1999f); v[5]=uniform(.01f,.4f); }
        reset();
        candidate_raw(&candidate,v,0xcc7341,.8f,1,clip);
        reference_raw(&reference,v,0xcc7341,.8f,1,clip);
        compare("primitive",n); triangles_tested++;
    }
    /* Shared boundaries, both windings and overlapping depths. */
    const float q[2][9]={{-1,-1,2,1,-1,2,1,1,2},{-1,-1,2,1,1,2,-1,1,2}};
    for(int winding=0;winding<2;winding++) {
        reset();
        for(int i=0;i<2;i++) {
            float v[9]; memcpy(v,q[i],sizeof(v));
            if(winding) for(int k=0;k<3;k++) {float t=v[k];v[k]=v[6+k];v[6+k]=t;}
            candidate_raw(&candidate,v,i?0xffffff:0x3050b0,1,1,0); reference_raw(&reference,v,i?0xffffff:0x3050b0,1,1,0);
        }
        compare("shared-edge",winding);
    }
}
static WsPos center(int n) { WsModule m; ws_materialize(&recipe,n,&m); return m.pos; }
static void draw_scene(int kind,int ref,WsAddress at,int yaw) {
    WsMetrics m;
    if(kind==0) { if(ref) reference_render(&reference,&game); else if(capture_enabled) render(&candidate,&game); else production_render(&candidate,&game); }
    else {if(ref) reference_render_world(&reference,&recipe,at,yaw,&m); else if(capture_enabled) render_world(&candidate,&recipe,at,yaw,&m); else production_render_world(&candidate,&recipe,at,yaw,&m);}
}
static int yaw_to(WsPos a,WsPos b) { float y=atan2f((float)b.x-a.x,(float)b.z-a.z)*180.f/3.14159265358979323846f;return (int)(y<0?y+360:y); }
static int compare_double(const void* a,const void* b) { double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y); }
static void benchmark(const char* name,int kind,WsAddress at,int yaw,int comma) {
    reset(); captured_count=0; capture_enabled=1;draw_scene(kind,0,at,yaw);capture_enabled=0;
    draw_scene(kind,1,at,yaw);compare(name,0);
    double full[2][7],rast[2][7],total[2][7],frame_times[2][700],total_times[2][700]; const int iterations=100;
    for(int sample=0;sample<7;sample++) for(int order=0;order<2;order++) {
        int ref=(sample+order)%2;
        double start=now(); for(int i=0;i<iterations;i++) { double t=now();draw_scene(kind,ref,at,yaw);frame_times[ref][sample*iterations+i]=(now()-t)*1000; } full[ref][sample]=(now()-start)*1000/iterations;
        start=now();
        for(int i=0;i<iterations;i++) {
            Renderer* r=ref?&reference:&candidate;
            memset(r->depth,255,sizeof(r->depth));
            for(unsigned t=0;t<captured_count;t++) {
                Captured* c=&captured[t];
                if(ref) reference_raw(r,c->v,c->col,c->shade,c->light,0);
                else production_raw(r,c->v,c->col,c->shade,c->light,0);
            }
        }
        rast[ref][sample]=(now()-start)*1000/iterations;
        start=now();
        for(int i=0;i<iterations;i++) {
            double t=now();draw_scene(kind,ref,at,yaw);
            if(ref) for(int y=0;y<H*2;y++)for(int x=0;x<W*2;x++)scanout[y*W*2+x]=reference.pixels[(y/2)*W+x/2];
            else ct_expand2x(scanout,candidate.pixels,W,H);
            __asm__ volatile("" : : "m"(scanout) : "memory");
            total_times[ref][sample*iterations+i]=(now()-t)*1000;
        }
        total[ref][sample]=(now()-start)*1000/iterations;
    }
    printf("%s{\"scene\":\"%s\",\"captured_triangles\":%u,\"samples\":[",comma?",\n":"",name,captured_count);
    for(int i=0;i<7;i++) printf("%s{\"reference_frame_ms\":%.6f,\"candidate_frame_ms\":%.6f,\"reference_raster_replay_ms\":%.6f,\"candidate_raster_replay_ms\":%.6f,\"reference_total_ms\":%.6f,\"candidate_total_ms\":%.6f}",i?",":"",full[1][i],full[0][i],rast[1][i],rast[0][i],total[1][i],total[0][i]);
    for(int ref=0;ref<2;ref++) {qsort(frame_times[ref],700,sizeof(double),compare_double);qsort(total_times[ref],700,sizeof(double),compare_double);}
    printf("],\"reference_frame_p95_ms\":%.6f,\"candidate_frame_p95_ms\":%.6f,\"reference_total_p95_ms\":%.6f,\"candidate_total_p95_ms\":%.6f}",frame_times[1][664],frame_times[0][664],total_times[1][664],total_times[0][664]);
}
int main(void) {
    primitive_tests();
    FILE* f=fopen("build/macro.cws","rb");if(!f)return 2;size_t n=fread(product,1,sizeof(product),f);fclose(f);if(ws_load(&recipe,product,n))return 2;
    for(int seed=1;seed<=4;seed++) {
        game_new(&game,seed,-1); game.state.time=43200000;
        for(int yaw=0;yaw<360;yaw+=30) {
            /* The game renderer derives its camera from player state. */
            reset(); game.state.yaw=yaw;
            render(&candidate,&game);reference_render(&reference,&game);compare("game",frames_tested++);
        }
    }
    for(int m=0;m<recipe.count;m++) for(int yaw=0;yaw<360;yaw+=45) {
        WsAddress at={0};at.pos=center(m);at.pos.y+=1700;at.scope=0xffff;
        reset();draw_scene(1,0,at,yaw);draw_scene(1,1,at,yaw);compare("world",frames_tested++);
    }
    printf("{\"status\":\"PASS\",\"physical\":\"PENDING\",\"primitive_cases\":%u,\"frame_cases\":%u,\"checks_before_timing\":%u,\"color_parity\":1,\"depth_parity\":1,\"scenes\":[\n",triangles_tested,frames_tested,checks);
    game_new(&game,42,-1);game.state.time=43200000;
    WsAddress at={0};benchmark("cascade",0,at,0,0);
    at.pos=center(3);at.pos.y+=1700;at.scope=0xffff;benchmark("vista",1,at,yaw_to(center(3),center(20)),1);
    at.pos=center(4);at.pos.y+=1700;at.scope=4;benchmark("cave",1,at,0,1);
    at.pos=center(15);at.pos.y+=1700;at.scope=0xffff;benchmark("closeup",1,at,yaw_to(center(15),center(5)),1);
    printf("\n]}\n");return 0;
}
