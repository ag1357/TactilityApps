#include "semantic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static CgView v;static CgResult r;static int checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 v.entity_count=3;v.entities[0]=(CgEntity){.id=17,.name="Axira"};v.entities[1]=(CgEntity){.id=82,.name="Belon"};v.entities[2]=(CgEntity){.id=90,.name="Celun"};v.self=17;v.player=82;
 v.count=3;v.facts[0]=(CgFact){.id=31,.subject=17,.relation=CG_EVENT,.status=CG_KNOWLEDGE,.flags=CG_BINARY,.value=1,.text="root"};
 v.facts[1]=(CgFact){.id=32,.subject=82,.relation=CG_EVENT,.status=CG_KNOWLEDGE,.flags=CG_RULE|CG_BINARY,.dep_count=1,.deps={31},.value=1,.text="effect"};
 v.facts[2]=(CgFact){.id=33,.subject=90,.relation=CG_EVENT,.status=CG_KNOWLEDGE,.flags=CG_RULE|CG_BINARY,.dep_count=1,.deps={32},.value=1,.text="consequence"};
 CgFrame f={.entity=90,.relation=CG_EVENT,.second_relation=CG_OTHER,.op=CG_COUNTERFACTUAL,.hypothetical=1,.intervention=31};
 cg_execute(&v,&f,&r);CHECK(r.count==1&&r.values[0]==0&&r.verified);CHECK(v.facts[0].value==1);CHECK(r.step_count==3);CHECK(r.consulted_count==3);
 r.values[0]=1;CHECK(!cg_verify(&v,&r));r.values[0]=0;r.steps[1].value=1;CHECK(!cg_verify(&v,&r));
 f.hypothetical=2;f.set_value=1;cg_execute(&v,&f,&r);CHECK(r.count==1&&r.values[0]==1&&r.verified);
 v.facts[0].status=CG_RUMOR;cg_execute(&v,&f,&r);CHECK(!r.count);v.facts[0].status=CG_UNKNOWN;cg_execute(&v,&f,&r);CHECK(!r.count);v.facts[0].status=CG_KNOWLEDGE;
 v.facts[1].deps[0]=999;cg_execute(&v,&f,&r);CHECK(!r.count);v.facts[1].deps[0]=33;cg_execute(&v,&f,&r);CHECK(!r.count);v.facts[1].deps[0]=31;
 CgContext c={0};cg_respond(&v,&c,"What is Celun event without Axira?",0,&r);CHECK(r.count==1&&r.values[0]==0);CHECK(r.frame.entity==90&&r.frame.intervention==31);
 memset(&c,0,sizeof c);cg_respond(&v,&c,"What is its event?",0,&r);CHECK(r.disposition==CG_NEEDS_CLARIFICATION);
 cg_respond(&v,&c,"What is Belon event?",0,&r);CHECK(c.focus==82);cg_respond(&v,&c,"What is the source of it?",0,&r);CHECK(r.count==1&&r.outputs[0]==32);
 f=(CgFrame){.entity=17,.relation=CG_EVENT,.second_relation=CG_OTHER,.op=CG_QUERY};v.facts[0].status=CG_CLAIM;cg_execute(&v,&f,&r);CHECK(r.count==1&&r.statuses[0]==CG_CLAIM);r.statuses[0]=CG_KNOWLEDGE;CHECK(!cg_verify(&v,&r));
 v.facts[0].status=CG_UNKNOWN;cg_execute(&v,&f,&r);CHECK(!r.count);
 uint16_t a[CG_FEATURES],b[CG_FEATURES];int na=cg_features(&v,"What is Axira event?",a,CG_FEATURES);strcpy(v.entities[0].name,"Unseenperson");int nb=cg_features(&v,"What is Unseenperson event?",b,CG_FEATURES);CHECK(na==nb&&!memcmp(a,b,na*sizeof(uint16_t)));
 v.facts[0].status=CG_KNOWLEDGE;v.facts[0].flags=CG_EXCLUSIVE;v.facts[1]=v.facts[0];v.facts[1].id=99;v.facts[1].value=0;v.facts[1].status=CG_BELIEF;v.count=2;f.op=CG_CONTRADICT;cg_execute(&v,&f,&r);CHECK(r.count==2&&r.conflict&&r.verified);CHECK(r.statuses[1]==CG_BELIEF);
 f.op=CG_QUERY;f.negation=1;cg_execute(&v,&f,&r);CHECK(!r.count);v.facts[0].flags|=CG_NEGATED;cg_execute(&v,&f,&r);CHECK(r.count==1);
 printf("{\"checks\":%d,\"failures\":0,\"view_bytes\":%zu,\"result_bytes\":%zu,\"frame_bytes\":%zu,\"context_bytes\":%zu,\"physical_status\":\"PENDING\"}\n",checks,sizeof v,sizeof r,sizeof f,sizeof c);return 0;
}
