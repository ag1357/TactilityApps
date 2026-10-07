/* Generation-v1 authority projection. Only this content adapter knows which
 * legacy fields are personal. One shared Game supplies geometry/world facts;
 * dynamically allocated player projections do not impose a world population
 * cap. Transport, identities, receipts and archive storage live above it. */
#include "../core/game.h"
#include "../core/interaction.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef struct {State s;CtActorPresentation actor;uint32_t substep;} Player;
static void private_copy(State *to,const State *from) {
    to->player=from->player;to->npc=from->npc;to->player_pos=from->player_pos;
    to->yaw=from->yaw;to->vertical_speed=from->vertical_speed;to->grounded=from->grounded;
    to->promise_state=from->promise_state;to->promise_deadline=from->promise_deadline;
    to->evidence_shown=from->evidence_shown;to->event_count=from->event_count;
    to->event_next=from->event_next;memcpy(to->events,from->events,sizeof(to->events));
}
static void bind(Game *w,Player *p) {private_copy(&w->state,&p->s);w->actor=p->actor;w->substep=p->substep;}
static void unbind(Game *w,Player *p) {private_copy(&p->s,&w->state);p->actor=w->actor;p->substep=w->substep;}
void *oa_world(uint32_t seed) {Game *w=calloc(1,sizeof(*w));if(w)game_new(w,seed,-1);return w;}
void *oa_player(void *world) {
    Game *w=world;Player *p=calloc(1,sizeof(*p));if(!p)return NULL;
    Game *fresh=calloc(1,sizeof(*fresh));if(!fresh){free(p);return NULL;}
    game_new(fresh,w->world.seed,-1);p->s=fresh->state;p->actor=fresh->actor;free(fresh);return p;
}
void oa_free(void *p) {free(p);}
void oa_step(void *world,void *player,int forward,int strafe,int yaw,int jump,int run) {
    Game *w=world;Player *p=player;bind(w,p);w->state.yaw=yaw;
    game_motion_tick(w,(Input){forward,strafe,0,jump,run,0},20);unbind(w,p);
}
void oa_clock(void *world,unsigned ms) {
    Game *w=world;w->state.promise_state=0;world_advance(w,(int64_t)ms*30);
}
void oa_personal_clock(void *world,void *player,unsigned ms) {
    Game *w=world;Player *p=player;
    if(p->s.player.quantity[19]<100000)p->s.player.quantity[19]+=ms/4;
    if(p->s.player.quantity[19]>100000)p->s.player.quantity[19]=100000;
    if(p->s.promise_state==1&&p->s.promise_deadline<w->state.time)p->s.promise_state=3;
}
/* Eight integer fields, explicit wire encoding is owned by the caller. */
void oa_pose(void *player,int32_t *out) {
    Player *p=player;out[0]=p->s.player_pos.x;out[1]=p->s.player_pos.y;out[2]=p->s.player_pos.z;
    out[3]=p->s.yaw;out[4]=p->s.vertical_speed;out[5]=p->s.grounded;
    out[6]=p->actor.facing;out[7]=p->actor.phase_milliradians;
}
int oa_action(void *world,void *player,unsigned op,unsigned item,int amount,unsigned target) {
    Game *w=world;Player *p=player;
    /* Legacy waits/extraction/repair fast-forward time. Online duration/jobs
     * must be explicit, never let one client advance everybody's clock. */
    if(op==WAIT||op==EXTRACT||op==REPAIR||op==FUND||op==BREAK_PROMISE||
       op==TELL||op==PROMISE||op==OP_NONE||op>BREAK_PROMISE)return 0;
    if((op==CONDENSE&&(target!=0||item!=IT_CHIT))||
       (op==DAMAGE&&target!=1200)||
       (op==PICK_UP&&target!=(item==IT_CABLE?1100u:1101u))||
       ((op==BUY||op==SELL||op==GIVE)&&(target!=3&&target!=KYRA_ID))||
       ((op==SHOW||op==TRANSFER)&&target!=KYRA_ID))return 0;
    bind(w,p);int ok=game_apply(w,(Operation){op,PLAYER_ID,target,item,amount,NULL});unbind(w,p);return ok;
}
int oa_speak(void *world,void *player,const char *text,char *reply,size_t cap) {
    Game *w=world;Player *p=player;CtInteraction target;bind(w,p);
    if(!ct_interaction_refresh(w,KYRA_ID,&target)||!ct_interaction_dialogue_supported(&target))return 0;
    /* Dialogue can propose time-consuming actions. Do not run a mutation-
     * capable local dialogue dispatcher online in this first checkpoint. */
    NpcView view;npc_view(w,&view);
    snprintf(reply,cap,"Kyra is here. Online dialogue actions are not yet integrated.");
    (void)text;return 1;
}
size_t oa_state(void *world,void *player,uint8_t *out,size_t cap) {
    Game *w=world;bind(w,player);return state_encode(&w->state,out,cap);
}
int oa_restore_player(void *player,const uint8_t *bytes,size_t n) {
    State *s=calloc(1,sizeof(*s));if(!s)return 0;int ok=state_decode(s,bytes,n);
    if(ok)private_copy(&((Player*)player)->s,s);free(s);return ok;
}
size_t oa_world_state(void *world,uint8_t *out,size_t cap) {
    Game *w=world;State *s=calloc(1,sizeof(*s));if(!s)return 0;
    *s=w->state;memset(&s->player,0,sizeof(s->player));memset(&s->npc,0,sizeof(s->npc));
    s->player_pos=(Pos){0};s->yaw=s->vertical_speed=0;s->grounded=0;s->promise_state=0;
    s->promise_deadline=0;s->evidence_shown=0;s->event_count=0;s->event_next=0;
    size_t n=state_encode(s,out,cap);free(s);return n;
}
int oa_restore_world(void *world,const uint8_t *bytes,size_t n) {return state_decode(&((Game*)world)->state,bytes,n);}
