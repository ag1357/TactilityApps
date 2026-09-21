#include "semantic.h"
#include <stdint.h>
#ifdef CG_TRAINED_MODEL
#include "../research/models/decision_int8.inc"
#else
static const int8_t cg_embedding[CG_BUCKETS][CG_WIDTH]={{0}};
static const int8_t cg_weights[CG_OUTPUTS][CG_WIDTH]={{0}};
static const int32_t cg_bias[CG_OUTPUTS]={0};
#endif
/* Integer-only inference; pooled embedding -> ReLU -> six independent heads.
   Confidence is an uncalibrated logit-margin indicator, never epistemic authority. */
void cg_predict(const uint16_t* features,int n,uint8_t* output,uint8_t* confidence){
 int32_t h[CG_WIDTH]={0},logits[CG_OUTPUTS];
 if(n>CG_FEATURES)n=CG_FEATURES;
 for(int t=0;t<n;t++)for(int j=0;j<CG_WIDTH;j++)h[j]+=cg_embedding[features[t]%CG_BUCKETS][j];
 for(int j=0;j<CG_WIDTH;j++){h[j]=n?h[j]/n:0;if(h[j]<0)h[j]=0;}
 for(int o=0;o<CG_OUTPUTS;o++){int32_t s=cg_bias[o];for(int j=0;j<CG_WIDTH;j++)s+=h[j]*cg_weights[o][j];logits[o]=s;}
 int off=0,margin=255;
 for(int head=0;head<CG_HEADS;head++){int best=0;int32_t a=INT32_MIN,b=INT32_MIN;
  for(int j=0;j<cg_head_sizes[head];j++){int32_t z=logits[off+j];if(z>a){b=a;a=z;best=j;}else if(z>b)b=z;}
  output[head]=best;int32_t d=a-b;if(d<margin)margin=d;off+=cg_head_sizes[head];
 }*confidence=(uint8_t)(margin<0?0:margin>255?255:margin);
}
