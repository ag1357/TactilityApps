#include "semantic.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
const char* const cg_relations[CG_RELATIONS]={"identity","occupation","relationship","location","condition","possession","goal","responsibility","trust","promise","event","observation","balance","schedule","strength","cost","requirement","age","other"};
const char* const cg_operators[CG_OPS]={"QUERY","RECALL","COMPARE","CORROBORATE","CONTRADICT","CAUSE","CONSEQUENCE","COUNTERFACTUAL","UPDATE_BELIEF","CHECK_SOURCE","CHECK_KNOWLEDGE","ABSTAIN","ASK_CLARIFICATION"};
const uint8_t cg_head_sizes[CG_HEADS]={13,19,6,3,2,3}; /* sum 46; reserved output rows unused */
uint32_t cg_hash(const char* p) {uint32_t h=2166136261u; for(;*p;p++){h^=(unsigned char)tolower((unsigned char)*p);h*=16777619u;}return h;}
int cg_fact_index(const CgView* v,uint32_t id){for(int i=0;i<v->count;i++)if(v->facts[i].id==id)return i;return -1;}
static const char* name(const CgView* v,uint32_t id){for(int i=0;i<v->entity_count;i++)if(v->entities[i].id==id)return v->entities[i].name;return "unidentified source";}
static void lower(const char* in,char* out,size_t n){size_t i=0;for(;in[i]&&i+1<n;i++)out[i]=(char)tolower((unsigned char)in[i]);out[i]=0;}
static int word(const char* s,const char* w){size_t n=strlen(w);if(!n)return 0;for(const char* p=s;(p=strstr(p,w));p++)if((p==s||!isalnum((unsigned char)p[-1]))&&!isalnum((unsigned char)p[n]))return 1;return 0;}
static int any(const char* s,const char* words){char b[160];snprintf(b,sizeof b,"%s",words);char* p=b;while(p){char* next=strchr(p,'|');if(next)*next++=0;if(word(s,p))return 1;p=next;}return 0;}
static const char* const aliases[CG_RELATIONS]={"name|identity","occupation|job|profession","relationship|sibling","location|where","condition|status","possession|owns|property","goal|objective","responsibility|responsible|blame","trust","promise|commitment","event|action","observation|observed|evidence","balance|money","schedule|timetable","strength|level","cost|price","requirement|requires","age|when",""};
/* This parser is the general deterministic language baseline. It knows no story identities. */
void cg_parse(const CgView* v,const CgContext* ctx,const char* input,int learned,CgFrame* f){
 memset(f,0,sizeof *f);f->relation=CG_OTHER;f->second_relation=CG_OTHER;f->confidence=255;
 char s[512];lower(input,s,sizeof s);
 uint32_t found[CG_ENTITIES];const char* positions[CG_ENTITIES];int nf=0;
 for(int i=0;i<v->entity_count;i++){char n[48];lower(v->entities[i].name,n,sizeof n);if(word(s,n)){int at=nf;const char* pos=strstr(s,n);while(at>0&&positions[at-1]>pos){found[at]=found[at-1];positions[at]=positions[at-1];at--;}found[at]=v->entities[i].id;positions[at]=pos;nf++;}}
 if(nf){f->entity=found[0];if(nf>1)f->second_entity=found[1];if(nf>2)f->ambiguous=1;}
 if(!nf&&any(s,"you|your"))f->entity=v->self;
 if(!nf&&any(s,"me|my|i"))f->entity=v->player;
 int pronoun=any(s,"he|she|it|they|him|her|that|its|their");
 if(!nf&&pronoun){if(ctx&&ctx->valid)f->entity=ctx->focus;else f->ambiguous=1;}
 if(learned){uint16_t features[CG_FEATURES];uint8_t pred[CG_HEADS],conf;
  int n=cg_features(v,input,features,CG_FEATURES);cg_predict(features,n,pred,&conf);
  f->op=pred[0];f->relation=pred[1];f->epistemic=pred[2];f->act=pred[3];f->negation=pred[4];f->hypothetical=pred[5];f->confidence=conf;
 }else{
  for(int r=0;r<CG_RELATIONS-1;r++)if(any(s,aliases[r])){if(f->relation==CG_OTHER)f->relation=r;else f->second_relation=r;}
  if(any(s,"recall|remember|previous"))f->op=CG_RECALL;
  if(any(s,"compare|difference"))f->op=CG_COMPARE;
  if(any(s,"corroborate|agree|agreement"))f->op=CG_CORROBORATE;
  if(any(s,"contradict|conflict|inconsistent"))f->op=CG_CONTRADICT;
  if(any(s,"cause|caused|why"))f->op=CG_CAUSE;
  if(any(s,"consequence|effects|results"))f->op=CG_CONSEQUENCE;
  if(any(s,"source|basis|how do you know"))f->op=CG_CHECK_SOURCE;
  if(any(s,"revise|update belief"))f->op=CG_UPDATE_BELIEF;
  if(any(s,"know whether|knowledge"))f->op=CG_CHECK_KNOWLEDGE;
  if(any(s,"without|remove|if")){f->op=CG_COUNTERFACTUAL;f->hypothetical=any(s,"set|instead")?2:1;}
  if(any(s,"belief|believe|think"))f->epistemic=CG_BELIEF;
  if(any(s,"rumor|heard"))f->epistemic=CG_RUMOR;
  if(any(s,"claim|claimed"))f->epistemic=CG_CLAIM;
  if(any(s,"memory|remember|recall"))f->epistemic=CG_MEMORY;
  if(any(s,"knowledge|observed"))f->epistemic=CG_KNOWLEDGE;
  f->negation=any(s,"not|never|no");
  f->act=any(s,"i assert|i claim")?CG_ASSERTION:any(s,"i promise|i commit")?CG_COMMITMENT:CG_QUESTION;
  if(any(s,"ignore|override|administrator|system prompt"))f->op=CG_ABSTAIN;
  if(f->op==CG_UPDATE_BELIEF)f->epistemic=0;
 }
 f->temporal=any(s,"before|previous|earlier|past");
 if(f->op==CG_RECALL&&!f->epistemic)f->epistemic=CG_MEMORY;
 if(!nf&&ctx&&ctx->valid&&(pronoun||f->op==CG_CHECK_SOURCE)){f->entity=ctx->focus;if(f->relation==CG_OTHER)f->relation=ctx->relation;}
 if(f->op==CG_COUNTERFACTUAL){f->set_value=any(s,"true|one|active")?1:0;uint32_t target=f->second_entity?f->second_entity:f->entity;
  for(int i=0;i<v->count;i++)if(v->facts[i].subject==target&&(v->facts[i].flags&CG_BINARY)){if(f->intervention){f->ambiguous=1;break;}f->intervention=v->facts[i].id;}}
 if(!f->entity||f->relation==CG_OTHER)f->unresolved=1;
 if(nf>1&&f->op!=CG_COMPARE&&f->op!=CG_COUNTERFACTUAL)f->ambiguous=1;
}
/* Hash features replace supplied entity names with a neutral symbol. Parameters never encode a name table. */
int cg_features(const CgView* v,const char* input,uint16_t* out,size_t cap){
 char s[512];lower(input,s,sizeof s);char clean[512]={0};size_t k=0;
 for(size_t i=0;s[i]&&k+10<sizeof clean;){int used=0;
  for(int e=0;e<v->entity_count;e++){char n[48];lower(v->entities[e].name,n,sizeof n);size_t z=strlen(n);
   if(z&&!strncmp(s+i,n,z)&&(i==0||!isalnum((unsigned char)s[i-1]))&&!isalnum((unsigned char)s[i+z])){memcpy(clean+k," entity ",8);k+=8;i+=z;used=1;break;}}
  if(!used)clean[k++]=s[i++];
 }clean[k]=0;
 int n=0;char prev[40]={0};const char* p=clean;
 while(*p&&n+2<(int)cap){while(*p&&!isalnum((unsigned char)*p))p++;if(!*p)break;char token[40];int j=0;while(isalnum((unsigned char)*p)){if(j<39)token[j++]=*p;p++;}token[j]=0;
  out[n++]=(uint16_t)(cg_hash(token)%CG_BUCKETS);if(*prev){char pair[82];snprintf(pair,sizeof pair,"%s_%s",prev,token);out[n++]=(uint16_t)(cg_hash(pair)%CG_BUCKETS);}snprintf(prev,sizeof prev,"%s",token);
 }return n;
}
static int consult(CgResult* r,uint32_t id){for(int i=0;i<r->consulted_count;i++)if(r->consulted[i]==id)return 1;if(r->consulted_count>=CG_FACTS)return 0;r->consulted[r->consulted_count++]=id;return 1;}
static int step(CgResult* r,const CgFact* f,int op,int32_t value,int status,int hypo){if(r->step_count>=CG_STEPS){r->disposition=CG_LIMIT;return 0;}CgStep* p=&r->steps[r->step_count++];*p=(CgStep){.fact=f->id,.parents={f->deps[0],f->deps[1]},.value=value,.op=op,.status=status,.hypothetical=hypo,.legal=1};consult(r,f->id);return 1;}
static void emit(CgResult* r,const CgFact* f,int32_t value,int status){if(r->count>=CG_OUT){r->disposition=CG_LIMIT;return;}int n=r->count++;r->outputs[n]=f->id;r->values[n]=value;r->statuses[n]=status;}
/* Explicit binary structural equations only: root value or AND(parent values).
   Missing edges, cycles and excessive depth produce UNKNOWN; absence is not falsity. */
static int derive(const CgView* v,int ix,const CgFrame* q,CgResult* r,uint8_t* mark,int32_t* values,int depth){
 if(ix<0||ix>=v->count||depth>12)return 0;if(mark[ix]==1)return 0;if(mark[ix]==2)return 1;
 const CgFact* f=&v->facts[ix];if(f->status!=CG_KNOWLEDGE||!(f->flags&CG_BINARY))return 0;
 mark[ix]=1;consult(r,f->id);r->inference_ops++;int32_t val=f->value;
 if(q->hypothetical&&q->intervention==f->id)val=q->hypothetical==1?0:q->set_value;
 else if(f->flags&CG_RULE){if(!f->dep_count||f->dep_count>2)return 0;val=1;for(int j=0;j<f->dep_count;j++){int d=cg_fact_index(v,f->deps[j]);if(!derive(v,d,q,r,mark,values,depth+1))return 0;val=val&&values[d];}}
 values[ix]=val;mark[ix]=2;return step(r,f,q->op,val,CG_KNOWLEDGE,q->hypothetical!=0);
}
uint32_t cg_legal_mask(const CgView* v){
 uint32_t mask=(1u<<CG_QUERY)|(1u<<CG_RECALL)|(1u<<CG_COMPARE)|(1u<<CG_CHECK_SOURCE)|(1u<<CG_CHECK_KNOWLEDGE)|(1u<<CG_ABSTAIN)|(1u<<CG_CLARIFY);
 for(int i=0;i<v->count&&i<CG_FACTS;i++){
  const CgFact* f=&v->facts[i];if(f->status==CG_KNOWLEDGE&&(f->flags&CG_BINARY)){mask|=1u<<CG_COUNTERFACTUAL;mask|=1u<<CG_CONSEQUENCE;if(f->flags&CG_RULE)mask|=1u<<CG_CAUSE;}
  for(int j=0;j<i;j++){const CgFact* g=&v->facts[j];if(f->subject==g->subject&&f->relation==g->relation&&f->time==g->time){mask|=(1u<<CG_CONTRADICT)|(1u<<CG_CORROBORATE);if(f->status==CG_KNOWLEDGE&&g->status==CG_KNOWLEDGE&&f->source!=g->source)mask|=1u<<CG_UPDATE_BELIEF;}}
 }return mask;
}
void cg_execute(const CgView* v,const CgFrame* q,CgResult* r){
 memset(r,0,sizeof *r);r->frame=*q;r->disposition=CG_NO_KNOWLEDGE;
 if(v->count>CG_FACTS||v->entity_count>CG_ENTITIES||q->op>=CG_OPS||q->relation>=CG_RELATIONS||q->epistemic>CG_MEMORY){r->disposition=CG_REJECTED;return;}
 if(q->op==CG_ABSTAIN||!(cg_legal_mask(v)&(1u<<q->op)))return;
 if(q->ambiguous||q->op==CG_CLARIFY){r->disposition=CG_NEEDS_CLARIFICATION;return;}
 if(q->unresolved||q->act!=CG_QUESTION)return; /* proposed utterances never mutate world state */
 int indices[CG_FACTS],n=0;int64_t latest=INT64_MIN,prior=INT64_MIN;
 for(int i=0;i<v->count;i++){const CgFact* f=&v->facts[i];r->inference_ops++;
  if(f->status<CG_KNOWLEDGE||f->status>CG_MEMORY||f->relation>=CG_RELATIONS)continue;
  if(f->subject!=q->entity&&!(q->op==CG_COMPARE&&f->subject==q->second_entity))continue;
  if(q->relation!=CG_OTHER&&f->relation!=q->relation&&f->relation!=q->second_relation)continue;
  if(q->epistemic&&f->status!=q->epistemic)continue;
  if(q->op==CG_CHECK_KNOWLEDGE&&f->status!=CG_KNOWLEDGE)continue;
  indices[n++]=i;if(f->time>latest){prior=latest;latest=f->time;}else if(f->time<latest&&f->time>prior)prior=f->time;
 }
 if(!n)return;
 if(q->op==CG_CAUSE||q->op==CG_COUNTERFACTUAL||q->op==CG_CONSEQUENCE){
  if(q->op==CG_COUNTERFACTUAL&&(!q->intervention||cg_fact_index(v,q->intervention)<0))return;
  uint8_t mark[CG_FACTS]={0};int32_t values[CG_FACTS]={0};
  if(q->op==CG_CONSEQUENCE){/* Explicit descendants; do not invent outgoing causes. */
   uint8_t reach[CG_FACTS]={0};for(int k=0;k<n;k++)reach[indices[k]]=1;
   for(int round=0;round<12;round++)for(int i=0;i<v->count;i++)for(int j=0;j<v->facts[i].dep_count&&j<2;j++){int d=cg_fact_index(v,v->facts[i].deps[j]);if(d>=0&&reach[d])reach[i]=1;}
   for(int i=0;i<v->count;i++)if(reach[i]&&v->facts[i].subject!=q->entity){if(!derive(v,i,q,r,mark,values,0)){r->count=0;return;}emit(r,&v->facts[i],values[i],CG_KNOWLEDGE);}
  }else for(int k=0;k<n;k++){int i=indices[k];if(q->op==CG_CAUSE&&!(v->facts[i].flags&CG_RULE))continue;if(!derive(v,i,q,r,mark,values,0)){r->count=0;return;}emit(r,&v->facts[i],values[i],CG_KNOWLEDGE);}
 }else{
  for(int k=0;k<n;k++){const CgFact* f=&v->facts[indices[k]];
   if(q->temporal&&prior!=INT64_MIN&&f->time!=prior)continue;
   if(!q->temporal&&q->op!=CG_RECALL){int newer=0;for(int j=0;j<n;j++){const CgFact* g=&v->facts[indices[j]];if(g->subject==f->subject&&g->relation==f->relation&&g->status==f->status&&g->time>f->time)newer=1;}if(newer)continue;}
   if(q->negation&&!(f->flags&CG_NEGATED))continue; /* explicit negatives only */
   if(q->op==CG_CONTRADICT||q->op==CG_CORROBORATE||q->op==CG_UPDATE_BELIEF){int match=0;for(int j=0;j<n;j++){const CgFact* g=&v->facts[indices[j]];if(g->id==f->id||g->subject!=f->subject||g->relation!=f->relation||g->time!=f->time)continue;
     int opposite=((f->flags^g->flags)&CG_NEGATED)||((f->flags&g->flags&CG_EXCLUSIVE)&&f->value!=g->value);
     if(q->op==CG_CONTRADICT?opposite:!opposite&&f->value==g->value&&f->source!=g->source){match=1;consult(r,g->id);if(opposite)r->conflict=1;}}
    if(!match)continue;
    if(q->op==CG_UPDATE_BELIEF&&f->status!=CG_KNOWLEDGE)continue;
   }
   int st=q->op==CG_UPDATE_BELIEF?CG_BELIEF:f->status;
   if(!step(r,f,q->op,f->value,st,0))return;emit(r,f,f->value,st);
  }
 }
 if(r->count&&r->disposition!=CG_LIMIT)r->disposition=CG_ANSWER;
 r->verified=cg_verify(v,r);if(!r->verified){r->count=0;r->disposition=CG_REJECTED;}
}
/* Re-execute no free text: evidence identity, status, and structural equation are checked. */
int cg_verify(const CgView* v,const CgResult* r){
 if(r->count>CG_OUT||r->step_count>CG_STEPS||r->consulted_count>CG_FACTS)return 0;
 for(int i=0;i<r->consulted_count;i++){int k=cg_fact_index(v,r->consulted[i]);if(k<0||v->facts[k].status<1||v->facts[k].status>CG_MEMORY)return 0;}
 for(int i=0;i<r->step_count;i++){const CgStep* s=&r->steps[i];int k=cg_fact_index(v,s->fact);if(k<0)return 0;const CgFact* f=&v->facts[k];if(!s->legal||s->op!=r->frame.op)return 0;
  if(s->status!=f->status&&!(s->op==CG_UPDATE_BELIEF&&f->status==CG_KNOWLEDGE&&s->status==CG_BELIEF))return 0;
  if(s->op==CG_CAUSE||s->op==CG_CONSEQUENCE||s->op==CG_COUNTERFACTUAL){
   if(f->status!=CG_KNOWLEDGE||!(f->flags&CG_BINARY))return 0;int32_t value=f->value;
   if(r->frame.hypothetical&&f->id==r->frame.intervention)value=r->frame.hypothetical==1?0:r->frame.set_value;
   else if(f->flags&CG_RULE){value=1;if(!f->dep_count||f->dep_count>2)return 0;for(int d=0;d<f->dep_count;d++){int found=0;for(int j=0;j<i;j++)if(r->steps[j].fact==f->deps[d]){found=1;value=value&&r->steps[j].value;}if(!found)return 0;}}
   if(s->value!=value||s->hypothetical!=(r->frame.hypothetical!=0))return 0;
  }else if(s->value!=f->value||s->hypothetical)return 0;
 }
 for(int o=0;o<r->count;o++){int found=0;for(int i=0;i<r->step_count;i++)if(r->outputs[o]==r->steps[i].fact&&r->values[o]==r->steps[i].value&&r->statuses[o]==r->steps[i].status)found=1;if(!found)return 0;}
 return 1;
}
void cg_realize(const CgView* v,CgResult* r){
 static const char* const status[]={"unknown","knowledge","belief","rumor","player claim","memory"};r->text[0]=0;
 if(r->disposition!=CG_ANSWER){snprintf(r->text,sizeof r->text,"%s",r->disposition==CG_NEEDS_CLARIFICATION?"Which entity or event do you mean?":"I do not have authorized evidence sufficient to answer that.");return;}
 for(int i=0;i<r->count;i++){int ix=cg_fact_index(v,r->outputs[i]);if(ix<0)continue;const CgFact* f=&v->facts[ix];char line[260];
  if(r->frame.op==CG_CAUSE||r->frame.op==CG_CONSEQUENCE||r->frame.op==CG_COUNTERFACTUAL)
   snprintf(line,sizeof line,"%s[%s] %s: %s = %d (bounded causal graph). ",r->frame.hypothetical?"Under the intervention: ":"",status[r->statuses[i]],name(v,f->subject),cg_relations[f->relation],r->values[i]);
  else snprintf(line,sizeof line,"[%s] %s: %s = %s%s. ",status[r->statuses[i]],name(v,f->subject),cg_relations[f->relation],f->flags&CG_NEGATED?"not ":"",f->text);
  size_t n=strlen(r->text);snprintf(r->text+n,sizeof(r->text)-n,"%s",line);
  if(r->frame.op==CG_CHECK_SOURCE){n=strlen(r->text);snprintf(r->text+n,sizeof(r->text)-n,"Recorded source ID %u (%s). ",f->source,name(v,f->source));}
 }
}
void cg_respond(const CgView* v,CgContext* c,const char* text,int learned,CgResult* r){CgFrame f;cg_parse(v,c,text,learned,&f);cg_execute(v,&f,r);cg_realize(v,r);if(r->count){c->focus=f.entity;c->relation=f.relation;c->last_fact=r->outputs[0];c->valid=1;}}
