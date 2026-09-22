#include "render.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WsRecipe recipe;
static Renderer frame;
int main(int argc,char **argv) {
    if(argc<2){fprintf(stderr,"usage: world_viewer product.cws [--headless]\nWASD move, arrows turn, E lift/portal, Escape exit\n");return 2;}
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;uint8_t data[48+64*WS_CAP+8*WS_LINK_CAP];size_t n=fread(data,1,sizeof(data),f);int extra=fgetc(f);fclose(f);
    WsError err=ws_load(&recipe,data,n);if(extra!=EOF||err!=WS_OK){fprintf(stderr,"product: %s\n",ws_error(err));return 2;}
    int headless=argc>2&&!strcmp(argv[2],"--headless");if(headless)SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER))return 2;
    SDL_Window *window=SDL_CreateWindow("World SDK experimental traversal",0,0,960,640,0);SDL_Renderer *display=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);SDL_Texture *texture=SDL_CreateTexture(display,SDL_PIXELFORMAT_RGB565,SDL_TEXTUREACCESS_STREAMING,W,H);
    if(!window||!display||!texture)return 2;
    WsTraveler t={0};t.at.scope=UINT16_MAX;
    for(uint16_t i=0;i<recipe.count;i++)if((recipe.modules[i].flags&WS_WALK)&&!(recipe.modules[i].flags&WS_INTERIOR)){WsModule m;ws_materialize(&recipe,i,&m);t.at.pos=(WsPos){m.pos.x,m.pos.y+300,m.pos.z};break;}
    int running=1,yaw=0,frames=0;double sum=0;WsMetrics metrics={0};
    while(running) {
        SDL_Event e;while(SDL_PollEvent(&e)) {if(e.type==SDL_QUIT)running=0;if(e.type==SDL_KEYDOWN){if(e.key.keysym.sym==SDLK_ESCAPE)running=0;if(e.key.keysym.sym==SDLK_e)ws_use_link(&recipe,&t);}}
        const Uint8 *k=SDL_GetKeyboardState(0);yaw=(yaw+(k[SDL_SCANCODE_RIGHT]-k[SDL_SCANCODE_LEFT])*3+360)%360;
        float a=yaw*.01745329252f;int forward=k[SDL_SCANCODE_W]-k[SDL_SCANCODE_S],side=k[SDL_SCANCODE_D]-k[SDL_SCANCODE_A];
        if(!t.remaining)ws_move(&recipe,&t.at,(int32_t)(80*(forward*sinf(a)+side*cosf(a))),(int32_t)(80*(forward*cosf(a)-side*sinf(a))));
        ws_travel_tick(&t,16);uint64_t start=SDL_GetPerformanceCounter();render_world(&frame,&recipe,t.at,yaw,&metrics);sum+=(SDL_GetPerformanceCounter()-start)*1000./SDL_GetPerformanceFrequency();
        draw_panel(&frame,0,0,W,11,0);draw_text(&frame,2,1,"WORLD SDK | WASD E LIFT/PORTAL",0xffff,0);
        SDL_UpdateTexture(texture,0,frame.pixels,W*2);SDL_RenderCopy(display,texture,0,0);SDL_RenderPresent(display);
        frames++;if(headless&&frames==120)running=0;if(!headless)SDL_Delay(16);
    }
    printf("{\"frames\":%d,\"mean_render_ms\":%.4f,\"submitted_triangles\":%u,\"raster_triangles\":%u,\"vertices\":%u,\"active\":%u,\"materialized\":%u,\"proxy\":%u,\"renderer_bytes\":%zu,\"recipe_bytes\":%zu,\"physical\":false}\n",frames,sum/frames,(unsigned)metrics.triangles,(unsigned)frame.triangles,(unsigned)metrics.vertices,(unsigned)metrics.active,(unsigned)metrics.materialized,(unsigned)metrics.proxy,sizeof(frame),sizeof(recipe));
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(display);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
