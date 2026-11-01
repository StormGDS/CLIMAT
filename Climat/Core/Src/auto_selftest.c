#include "auto_control.h"
#include "control_core.h"
#include <string.h>
uint32_t auto_selftest(void)
{
  uint32_t e=0;AutoControl a={0};
  const AutoConfig p={368,16,336,16,432,16,464,16,1000,0,0};
  if(auto_sample(&a,0,1,320,0,0,0,0)) e|=1;
  if(!auto_configure(&a,&p)) e|=2;
  AutoConfig bad=p;bad.heat1_hyst=0;
  if(auto_configure(&a,&bad) || memcmp(&a.config,&p,sizeof(p))) e|=4;
  bad=p;bad.heat1_hyst=INT16_MAX;if(auto_configure(&a,&bad)) e|=4;
  bad=p;bad.cool1_hyst=INT16_MAX;if(auto_configure(&a,&bad)) e|=4;
  if(auto_sample(&a,0,1,368,0,0,0,0)!=1) e|=8;
  if(auto_sample(&a,0,1,380,0,0,1,1000)!=1) e|=16;
  if(auto_sample(&a,0,1,384,0,0,1,2000)!=0) e|=32;
  /* Temperature path: cannot request K2 before actual K1. */
  if(auto_sample(&a,0,1,336,0,0,0,3000)!=1) e|=64;
  if(auto_sample(&a,0,1,336,0,0,1,4000)!=3) e|=128;
  if(auto_sample(&a,0,1,351,0,0,3,5000)!=3) e|=256;
  if(auto_sample(&a,0,1,352,0,0,3,6000)!=1) e|=512;
  /* Early path has a fresh-window guard and inclusive cooling threshold. */
  auto_reset(&a);
  if(auto_sample(&a,0,1,360,1,-1000,1,0)!=1 || a.rate_ready) e|=1024;
  if(auto_sample(&a,0,1,360,1,-1000,1,4999)!=1) e|=1024;
  if(auto_sample(&a,0,1,360,1,-1000,0,5000)!=1) e|=2048;
  if(auto_sample(&a,0,1,360,1,-1000,1,6000)!=3) e|=4096;
  if(auto_sample(&a,0,1,360,1,-999,3,7000)!=1) e|=8192;
  if(auto_sample(&a,0,1,360,1,INT32_MIN,1,8000)!=3) e|=16384;
  if(auto_sample(&a,0,1,360,0,-2000,3,9000)!=1) e|=32768;
  auto_sample(&a,0,1,360,1,-1000,1,10000);
  if(auto_sample(&a,0,1,336,1,0,3,11000)!=3 ||
     auto_sample(&a,0,1,344,1,1000,3,12000)!=3 ||
     auto_sample(&a,0,1,352,1,1000,3,13000)!=1) e|=65536;
  if(auto_sample(&a,1,1,360,1,-1000,1,14000)!=1 || a.rate_ready) e|=131072;
  auto_reset(&a);auto_sample(&a,0,1,360,1,-1000,1,UINT32_MAX-1999);
  if(auto_sample(&a,0,1,360,1,-1000,1,3000)!=3 || !a.rate_ready) e|=262144;
  if(auto_sample(&a,0,0,360,1,-1000,1,4000) || a.tracking) e|=524288;
  AutoConfig disabled=p;disabled.cooling_milli_per_min=0;auto_configure(&a,&disabled);
  auto_sample(&a,0,1,360,1,INT32_MIN,1,0);
  if(auto_sample(&a,0,1,360,1,INT32_MIN,1,5000)!=1) e|=1048576;
  auto_configure(&a,&p);
  if(auto_sample(&a,0,1,432,1,-100000,0,0)!=0 ||
     auto_sample(&a,0,1,433,1,-100000,0,1000)!=4 ||
     auto_sample(&a,0,1,465,1,100000,4,2000)!=12 ||
     auto_sample(&a,0,1,448,1,0,12,3000)!=4) e|=2097152;
  a.alternate=1;auto_sample(&a,0,1,416,0,0,4,4000);
  if(a.lead || auto_sample(&a,0,1,416,0,0,0,5000) || a.lead!=1 ||
     auto_sample(&a,0,1,433,0,0,0,6000)!=8) e|=4194304;
  ControlCore c;control_init(&c,0);control_sample(&c,1,0,0);
  c.auto_requested=2;control_sample(&c,1,0,30000);if(c.logical) e|=8388608;
  c.auto_requested=3;control_sample(&c,1,0,30001);if(c.logical!=3) e|=8388608;
  /* Preserve K1 while K2 is completing its AUTO minimum ON. */
  c.changed_at[1]=59000;c.auto_requested=0;control_sample(&c,1,0,60001);
  if(c.logical!=3) e|=16777216;
  control_sample(&c,1,0,89000);if(c.logical) e|=16777216;
  c.auto_requested=3;control_sample(&c,1,0,120000);
  control_sample(&c,0,0,120001);if(c.logical!=4) e|=33554432;
  for(int t=-880;t<=2000;++t) for(unsigned actual=0;actual<16;++actual) {
    uint8_t mask=auto_sample(&a,0,1,(int16_t)t,1,INT32_MIN,(uint8_t)actual,200000);
    if(((mask&3) && (mask&12)) || ((mask&2) && !(actual&1))) e|=67108864;
    if(a.effective_milli!=(int32_t)t*1000/16) e|=134217728;
  }
  return e;
}
