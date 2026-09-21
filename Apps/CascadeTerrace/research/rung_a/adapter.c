/* Test-only injection of identical authorized records into the unchanged patch.
   It is never linked into gameplay; patch scenario logic remains untouched. */
#include "game.h"
#include "semantic.h"
#include <string.h>
#include <stdio.h>
static NpcView authorized;
void research_npc_view(const Game* g,NpcView* v){(void)g;*v=authorized;}
int research_game_apply(Game* g,Operation op){(void)g;(void)op;return 0;}
void rung_a_run(const CgView* v,Conversation* c,const char* input,Reply* reply){
 memset(&authorized,0,sizeof authorized);for(int i=0;i<v->count&&i<MAX_FACTS;i++){
 const CgFact* f=&v->facts[i];Fact* a=&authorized.facts[authorized.count++];a->id=f->id;a->source=f->source;a->status=f->status;
 for(int e=0;e<v->entity_count;e++)if(v->entities[e].id==f->subject)snprintf(a->subject,sizeof a->subject,"%.39s",v->entities[e].name);
 snprintf(a->relation,sizeof a->relation,"%s",cg_relations[f->relation]);snprintf(a->object,sizeof a->object,"%s",f->text);}
 static Game g;cognition_dialogue(&g,c,input,reply);
}
