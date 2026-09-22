#define _POSIX_C_SOURCE 200809L
#include "state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define WIRE_CAP 12000
/* Single streaming codec also computes the canonical semantic hash without a
   temporary state copy. Explicit widths give host/P4 parity, independent of ABI. */
typedef struct { uint8_t *p; size_t n,cap; uint32_t crc; int bad; } Writer;
static void put(Writer *w,uint32_t v,unsigned bytes) {
    for(unsigned i=0;i<bytes;i++) {uint8_t b=(uint8_t)(v>>(i*8));if(w->p){if(w->n<w->cap)w->p[w->n]=b;else w->bad=1;}w->n++;w->crc^=b;for(int k=0;k<8;k++)w->crc=(w->crc>>1)^(0xedb88320U&(0U-(w->crc&1)));}
}
static void payload(const WsState *s,Writer *w,int tail) {
    for(int i=0;i<4;i++)put(w,s->ancestry.word[i],4);
    put(w,s->recipe_crc,4);put(w,s->revision,4);put(w,s->count,2);put(w,s->player_count,2);put(w,s->feed_count,2);put(w,tail?s->tail_count:0,2);
    for(int i=0;i<s->count;i++) {const WsEntity *e=&s->entities[i];put(w,e->epoch,4);put(w,e->revision,4);put(w,e->owner,4);put(w,e->quantity,2);put(w,e->health,2);for(int j=0;j<4;j++)put(w,e->decor[j],2);put(w,e->alive,1);put(w,e->public_access,1);put(w,e->behavior,1);put(w,e->reserved,1);}
    for(int i=0;i<s->player_count;i++) {const WsPlayer *p=&s->players[i];put(w,p->id,4);put(w,p->sequence,4);put(w,p->credits,4);for(int j=0;j<7;j++)put(w,p->inventory[j],2);for(int j=0;j<WS_CAP;j++)put(w,p->completed[j],4);}
    for(int i=0;i<s->player_count;i++)for(int j=0;j<s->player_count;j++){const WsRelationship *v=&s->relations[i][j];put(w,(uint16_t)v->trust,2);put(w,(uint16_t)v->reliability,2);put(w,(uint16_t)v->cooperation,2);put(w,(uint16_t)v->aggression,2);put(w,v->confidence,2);put(w,v->promise,2);}
    for(int i=0;i<s->feed_count;i++){const WsFeed *f=&s->feed[i];put(w,f->revision,4);put(w,f->actor,4);put(w,f->epoch,4);put(w,f->target,2);put(w,f->kind,2);}
    if(tail)for(int i=0;i<s->tail_count;i++){const WsOperation *o=&s->tail[i];put(w,o->sequence,4);put(w,o->epoch,4);put(w,o->base_revision,4);put(w,o->action,2);put(w,o->target,2);put(w,o->amount,2);put(w,o->aux,2);}
}
static int bounded(const WsState *s){return s->count<=WS_CAP&&s->player_count<=WS_PLAYER_CAP&&s->feed_count<=WS_FEED_CAP&&s->tail_count<=WS_TAIL_CAP;}
uint32_t ws_state_hash(const WsState *s){if(!bounded(s))return 0;Writer w={0,0,0,~0U,0};payload(s,&w,0);return ~w.crc;}
size_t ws_state_encode(const WsState *s,uint8_t *p,size_t cap) {
    if(cap<48||!bounded(s))return 0;Writer w={p,16,cap,~0U,0};payload(s,&w,1);if(w.bad)return 0;
    uint32_t crc=~w.crc;Writer header={p,0,16,0,0};put(&header,0x31535743U,4);put(&header,WS_SCHEMA,2);put(&header,WS_GENERATOR,2);put(&header,(uint32_t)w.n,4);put(&header,crc,4);return w.n;
}
typedef struct { const uint8_t *p;size_t n,cap;int bad; } Reader;
static uint32_t get(Reader *r,unsigned n) {uint32_t v=0;for(unsigned i=0;i<n;i++){if(r->n>=r->cap){r->bad=1;return 0;}v|=(uint32_t)r->p[r->n++]<<(i*8);}return v;}
static int16_t signed16(uint32_t v){return v<=INT16_MAX?(int16_t)v:(int16_t)(-1-(int32_t)(65535-v));}
WsError ws_state_decode(WsState *s,const WsRecipe *recipe,const uint8_t *p,size_t n) {
    if(n<48||n>WIRE_CAP)return WS_FORMAT;Reader r={p,0,n,0};
    if(get(&r,4)!=0x31535743U)return WS_FORMAT;
    if(get(&r,2)!=WS_SCHEMA||get(&r,2)!=WS_GENERATOR)return WS_VERSION;
    if(get(&r,4)!=n||get(&r,4)!=ws_crc(p+16,n-16))return WS_FORMAT;
    /* Validate counts and exact length before writes. Decode into a staging
       state when preserving an existing canonical state across failures. */
    unsigned count=p[40]|p[41]<<8,players=p[42]|p[43]<<8,feeds=p[44]|p[45]<<8,tails=p[46]|p[47]<<8;
    if(count>WS_CAP||players>WS_PLAYER_CAP||feeds>WS_FEED_CAP||tails>WS_TAIL_CAP||n!=48+count*28+players*538+players*players*12+feeds*16+tails*20)return WS_BOUNDS;
    memset(s,0,sizeof(*s));for(int i=0;i<4;i++)s->ancestry.word[i]=get(&r,4);
    s->recipe_crc=get(&r,4);s->revision=get(&r,4);s->count=(uint16_t)get(&r,2);s->player_count=(uint16_t)get(&r,2);s->feed_count=(uint16_t)get(&r,2);s->tail_count=(uint16_t)get(&r,2);
    for(int i=0;i<s->count;i++){WsEntity *e=&s->entities[i];e->epoch=get(&r,4);e->revision=get(&r,4);e->owner=get(&r,4);e->quantity=(uint16_t)get(&r,2);e->health=(uint16_t)get(&r,2);for(int j=0;j<4;j++)e->decor[j]=(uint16_t)get(&r,2);e->alive=(uint8_t)get(&r,1);e->public_access=(uint8_t)get(&r,1);e->behavior=(uint8_t)get(&r,1);e->reserved=(uint8_t)get(&r,1);}
    for(int i=0;i<s->player_count;i++){WsPlayer *v=&s->players[i];v->id=get(&r,4);v->sequence=get(&r,4);v->credits=get(&r,4);for(int j=0;j<7;j++)v->inventory[j]=(uint16_t)get(&r,2);for(int j=0;j<WS_CAP;j++)v->completed[j]=get(&r,4);}
    for(int i=0;i<s->player_count;i++)for(int j=0;j<s->player_count;j++){WsRelationship *v=&s->relations[i][j];v->trust=signed16(get(&r,2));v->reliability=signed16(get(&r,2));v->cooperation=signed16(get(&r,2));v->aggression=signed16(get(&r,2));v->confidence=(uint16_t)get(&r,2);v->promise=(uint16_t)get(&r,2);}
    for(int i=0;i<s->feed_count;i++){WsFeed *f=&s->feed[i];f->revision=get(&r,4);f->actor=get(&r,4);f->epoch=get(&r,4);f->target=(uint16_t)get(&r,2);f->kind=(uint16_t)get(&r,2);}
    for(int i=0;i<s->tail_count;i++){WsOperation *o=&s->tail[i];o->sequence=get(&r,4);o->epoch=get(&r,4);o->base_revision=get(&r,4);o->action=(uint16_t)get(&r,2);o->target=(uint16_t)get(&r,2);o->amount=(uint16_t)get(&r,2);o->aux=(uint16_t)get(&r,2);}
    WsError e=r.bad?WS_FORMAT:ws_state_validate(s,recipe);if(e!=WS_OK)memset(s,0,sizeof(*s));return e;
}
int ws_save(const WsState *s,const char *base) {
    uint8_t *data=malloc(WIRE_CAP);if(!data)return 0;size_t n=ws_state_encode(s,data,WIRE_CAP);
    char path[512],tmp[520];int k=snprintf(path,sizeof(path),"%s.%u",base,(unsigned)(s->revision&1));if(k<0||(size_t)k>=sizeof(path)){free(data);return 0;}
    snprintf(tmp,sizeof(tmp),"%s.tmp",path);FILE *f=n?fopen(tmp,"wb"):NULL;int ok=0;
    if(f){ok=fwrite(data,1,n,f)==n;if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;if(ok)ok=rename(tmp,path)==0;}free(data);return ok;
}
int ws_restore(WsState *s,const WsRecipe *r,const char *base) {
    uint8_t *data=malloc(WIRE_CAP);WsState *stage=malloc(sizeof(*stage));if(!data||!stage){free(data);free(stage);return 0;}int found=0;
    for(int i=0;i<2;i++){char path[512];int k=snprintf(path,sizeof(path),"%s.%d",base,i);if(k<0||(size_t)k>=sizeof(path))continue;FILE *f=fopen(path,"rb");if(!f)continue;size_t n=fread(data,1,WIRE_CAP,f);int extra=fgetc(f);fclose(f);if(extra==EOF&&ws_state_decode(stage,r,data,n)==WS_OK&&(!found||stage->revision>s->revision)){*s=*stage;found=1;}}
    free(data);free(stage);return found;
}
