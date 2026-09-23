#define _POSIX_C_SOURCE 200809L
#include "presentation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static uint16_t src[65536],dst[262146],expected[262144];
static unsigned checks;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"presentation:%d: %s\n",__LINE__,#x);return 1;}checks++;}while(0)
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
__attribute__((noinline)) static void reference_expand(uint16_t* d,const uint16_t* s,unsigned w,unsigned h) {
 for(unsigned y=0;y<2*h;y++)for(unsigned x=0;x<2*w;x++)d[(size_t)y*2*w+x]=s[(size_t)(y/2)*w+x/2];
}
static int cmp(const void* a,const void* b){double x=*(const double*)a,y=*(const double*)b;return(x>y)-(x<y);}
int main(void) {
 for(unsigned i=0;i<65536;i++)src[i]=(uint16_t)i;
 for(unsigned w=1;w<=65;w++)for(unsigned offset=0;offset<2;offset++) {
  unsigned h=7,n=w*h*4;memset(dst,0xa5,sizeof(dst));reference_expand(expected,src,w,h);ct_expand2x(dst+offset+1,src,w,h);
  CHECK(memcmp(dst+offset+1,expected,n*2)==0);CHECK(dst[offset]==0xa5a5);CHECK(dst[offset+n+1]==0xa5a5);
 }
 reference_expand(expected,src,256,256);ct_expand2x(dst+1,src,256,256);CHECK(memcmp(dst+1,expected,262144*2)==0);
 uint16_t a[16]={0},b[16]={0},snapshot[4],input[4]={1,2,3,4};CtPresentPipe p;
 ct_pipe_init(&p,a,b,snapshot,sizeof(input));
 for(unsigned i=0;i<1000;i++) {
  uint16_t* old=p.front;input[0]=(uint16_t)i;CHECK(ct_pipe_begin(&p,input));input[0]^=0xffff;
  CHECK(!ct_pipe_begin(&p,input));CHECK(snapshot[0]==i);CHECK(!ct_pipe_poll(&p));CHECK(p.front==old);
  ct_expand2x(p.back,p.snapshot,2,2);ct_pipe_complete(&p);CHECK(ct_pipe_poll(&p));CHECK(p.front!=old);CHECK(p.front[0]==i);CHECK(!ct_pipe_poll(&p));
 }
 CHECK(ct_pipe_begin(&p,input));ct_pipe_rejected(&p);CHECK(!ct_pipe_poll(&p));CHECK(ct_pipe_begin(&p,input));ct_pipe_complete(&p);CHECK(ct_pipe_poll(&p));
 double elapsed[2][2000];unsigned checksum=0;
 for(unsigned k=0;k<2000;k++)for(unsigned order=0;order<2;order++) {
  unsigned ref=(k+order)%2;double t=now();if(ref)reference_expand(dst,src,240,160);else ct_expand2x(dst,src,240,160);
  __asm__ volatile("" : : "m"(dst) : "memory");elapsed[ref][k]=(now()-t)*1000;checksum+=dst[k];
 }
 double mean[2]={0};for(unsigned ref=0;ref<2;ref++){for(unsigned i=0;i<2000;i++)mean[ref]+=elapsed[ref][i]/2000;qsort(elapsed[ref],2000,sizeof(double),cmp);}
 printf("{\"status\":\"PASS\",\"checks\":%u,\"color_parity\":1,\"async_ownership_cycles\":1000,\"source_bytes\":76800,\"scanout_bytes\":307200,\"ppa_extra_bytes\":384000,\"reference_mean_ms\":%.6f,\"portable_mean_ms\":%.6f,\"reference_p95_ms\":%.6f,\"portable_p95_ms\":%.6f,\"checksum\":%u,\"pie_physical\":\"PENDING\",\"ppa_physical\":\"PENDING\"}\n",checks,mean[1],mean[0],elapsed[1][1899],elapsed[0][1899],checksum);
 return 0;
}
