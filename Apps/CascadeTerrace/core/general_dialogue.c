#include "game.h"
#include "semantic.h"
#include <string.h>
#include <stdio.h>
/* Adapter only: convert the existing authorized view, never consult Game truth.
   Relation mapping translates the legacy schema; it does not infer a narrative. */
static int relation(const char* s){
 for(int i=0;i<CG_RELATIONS;i++)if(!strcmp(s,cg_relations[i]))return i;
 static const struct {const char* name;int relation;} map[]={
 {"name",CG_IDENTITY},{"employment",CG_OCCUPATION},{"membership",CG_RELATIONSHIP},
 {"status",CG_PROMISE},{"repaired",CG_EVENT},{"damaged",CG_EVENT},{"promised",CG_PROMISE},
 {"said",CG_EVENT},{"price",CG_COST},{"level",CG_STRENGTH},{"pressure check",CG_OBSERVATION},
 {"conduct",CG_OBSERVATION},{"intention",CG_GOAL},{"output loss",CG_CONDITION},{"shift",CG_SCHEDULE}};
 for(size_t i=0;i<sizeof map/sizeof map[0];i++)if(!strcmp(map[i].name,s))return map[i].relation;
 return CG_OTHER;
}
void cg_from_npc(const NpcView* n,CgView* v){
 memset(v,0,sizeof *v);v->self=cg_hash("I");v->player=cg_hash("player");
 for(int i=0;i<n->count&&v->count<CG_FACTS;i++){
  const Fact* f=&n->facts[i];if(f->status<KNOWLEDGE||f->status>MEMORY)continue;
  uint32_t subject=cg_hash(f->subject);int found=0;for(int j=0;j<v->entity_count;j++)if(v->entities[j].id==subject)found=1;
  if(!found){if(v->entity_count>=CG_ENTITIES)continue;CgEntity* e=&v->entities[v->entity_count++];e->id=subject;snprintf(e->name,sizeof e->name,"%s",f->subject);}
  CgFact* d=&v->facts[v->count++];d->id=f->id;d->subject=subject;d->source=f->source;d->value=(int32_t)cg_hash(f->object);d->status=f->status;d->relation=relation(f->relation);
  /* Legacy prose is not a structural causal equation or a logical exclusive atom.
     Do not invent causal edges, confidence, timestamps or contradiction semantics. */
  snprintf(d->text,sizeof d->text,"%.95s",f->object);
 }
}
void general_dialogue(Game* g,Conversation* c,const char* input,int mode,Reply* out){
 static NpcView n;static CgView v;static CgResult r;
 npc_view(g,&n);cg_from_npc(&n,&v);
 CgContext ctx={.focus=c->subject,.last_fact=c->last_count?c->last_ids[0]:0,.valid=c->subject!=0,.relation=CG_OTHER};
 for(int i=0;i<CG_RELATIONS;i++)if(!strcmp(c->relation,cg_relations[i]))ctx.relation=i;
 cg_respond(&v,&ctx,input,mode==4,&r);
 memset(out,0,sizeof *out);snprintf(out->text,sizeof out->text,"%s",r.text);out->abstained=r.disposition!=CG_ANSWER;
 for(int i=0;i<r.count;i++){out->evidence[i]=r.outputs[i];out->statuses[i]=r.statuses[i];}out->count=r.count;
 c->subject=ctx.focus;snprintf(c->relation,sizeof c->relation,"%s",cg_relations[ctx.relation]);c->last_count=out->count;memcpy(c->last_ids,out->evidence,out->count*sizeof(uint32_t));
}
