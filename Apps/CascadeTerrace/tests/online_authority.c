#include "../core/game.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
void *oa_world(uint32_t);void *oa_player(void *);
void oa_free(void *);void oa_clock(void *,unsigned);
void oa_personal_clock(void *,void *,unsigned);
void oa_step(void *,void *,int,int,int,int,int);
void oa_pose(void *,int32_t *);
int oa_action(void *,void *,unsigned,unsigned,int,unsigned);
size_t oa_state(void *,void *,uint8_t *,size_t);
size_t oa_world_state(void *,uint8_t *,size_t);
int oa_restore_player(void *,const uint8_t *,size_t);
int oa_restore_world(void *,const uint8_t *,size_t);
int main(void) {
    void *world=oa_world(42),*a=oa_player(world),*b=oa_player(world);
    assert(world&&a&&b);
    int32_t initial[8],pose[8];oa_pose(a,initial);
    for(unsigned i=0;i<2300;i++){oa_clock(world,20);oa_personal_clock(world,a,20);oa_personal_clock(world,b,20);}
    oa_step(world,a,1000,0,0,0,1);oa_pose(a,pose);assert(pose[2]!=initial[2]);
    assert(oa_action(world,a,CONDENSE,IT_CHIT,1,0));
    uint8_t bytes[32768];State state;
    size_t n=oa_state(world,a,bytes,sizeof(bytes));assert(n&&state_decode(&state,bytes,n));
    assert(state.player.quantity[0]==1&&oa_restore_player(a,bytes,n));
    n=oa_state(world,b,bytes,sizeof(bytes));assert(n&&state_decode(&state,bytes,n));
    assert(state.player.quantity[0]==0);
    assert(!oa_action(world,a,WAIT,IT_CHIT,100,0));
    assert(!oa_action(world,a,REPAIR,IT_CHIT,1,1200));
    n=oa_world_state(world,bytes,sizeof(bytes));assert(n&&oa_restore_world(world,bytes,n));
    assert(!oa_restore_player(a,bytes,n-1));
    oa_free(a);oa_free(b);oa_free(world);
    puts("Authority projection, motion, personal isolation, duration denial and codecs: PASS");
    return 0;
}
