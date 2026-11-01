#include "control_core.h"
#include <string.h>
static uint8_t increment(uint8_t n) { return n==255 ? n : (uint8_t)(n+1); }
void control_init(ControlCore *c, uint32_t now)
{
  memset(c,0,sizeof(*c)); c->source=0; c->timeout_ms=600000;
  c->heater_min_on_seconds=30;c->fan_min_on_seconds=60;
  for(unsigned i=0;i<4;++i) c->changed_at[i]=now;
}
void control_observe(ControlCore *c, unsigned v1, unsigned v2, uint32_t now)
{
  c->valid[0]=(uint8_t)(v1!=0); c->valid[1]=(uint8_t)(v2!=0);
  c->sampled=1; c->sample_at=now;
  for(unsigned i=0;i<2;++i) {
    if(c->valid[i]) { c->successes[i]=increment(c->successes[i]); c->failures[i]=0; }
    else { c->failures[i]=increment(c->failures[i]); c->successes[i]=0; }
  }
  if(c->source==0 && !v1 && v2 && c->fail_count && c->recover_count &&
     c->failures[0]>=c->fail_count) c->source=1;
  else if(c->source==1 && v1 && c->recover_count &&
          c->successes[0]>=c->recover_count) c->source=0;
}
void control_sample(ControlCore *c, unsigned v1, unsigned v2, uint32_t now)
{ control_observe(c,v1,v2,now);control_tick(c,now); }
void control_tick(ControlCore *c, uint32_t now)
{
  if(c->manual && (uint32_t)(now-c->manual_at)>=c->timeout_ms) {
    c->manual=0; c->requested=0;
  }
  /* Startup is not a confirmed two-sensor failure. Keep all logical outputs
   * off until the first complete measurement cycle establishes validity. */
  if(!c->sampled) { c->fault=1;c->requested=0;c->logical=0;return; }
  uint8_t was_fault=c->fault;
  c->fault=(uint8_t)(!c->sampled || (uint32_t)(now-c->sample_at)>2500 ||
                    (!c->valid[0] && !c->valid[1]));
  if(c->fault) {
    c->auto_requested=0;
    c->requested=0; /* Never resurrect a pre-fault manual heating command. */
    for(unsigned i=0;i<4;++i)
      if(((c->logical^4U)>>i)&1U) c->changed_at[i]=now;
    c->logical=4; return; /* K3 only; bypass minimum times. */
  }
  if(was_fault) c->requested=0;
  uint8_t desired=c->manual ? c->requested : c->auto_requested;
  if(!c->manual && !c->valid[c->source]) {
    /* No trustworthy control temperature while waiting for failover:
     * remove heat immediately, even during its minimum ON interval. */
    desired=0; c->auto_requested=0;
    for(unsigned i=0;i<2;++i) if(c->logical&(1U<<i)) c->changed_at[i]=now;
    c->logical&=(uint8_t)~3U;
  }
  /* Apply OFF first. Explicit user rule (2026-10-28): MANUAL never waits
   * for minimum ON/OFF. AUTO retains both minimum times.
   * The fault branch above still overrides manual commands. */
  for(unsigned i=0;i<4;++i) {
    unsigned channel=i<2 ? 1U-i : i; /* K2 OFF before K1. */
    uint8_t bit=(uint8_t)(1U<<channel);
    uint32_t minimum=(uint32_t)(channel<2 ? c->heater_min_on_seconds : c->fan_min_on_seconds)*1000U;
    if(!c->manual && channel==0 && (c->logical&2U)) continue;
    if((c->logical&bit) && !(desired&bit) &&
       (c->manual || (uint32_t)(now-c->changed_at[channel])>=minimum)) {
      c->logical&=(uint8_t)~bit; c->changed_at[channel]=now;
    }
  }
  for(unsigned i=0;i<4;++i) {
    uint8_t bit=(uint8_t)(1U<<i);
    uint32_t minimum=i<2 ? 30000 : 60000;
    if(!c->manual && i==1 && !(c->logical&1U)) continue;
    if((desired&bit) && !(c->logical&bit) &&
       !(c->logical & (i<2 ? 12U : 3U)) &&
       (c->manual || (uint32_t)(now-c->changed_at[i])>=minimum)) {
      c->logical|=bit; c->changed_at[i]=now;
    }
  }
}
void control_mode(ControlCore *c, unsigned manual, uint32_t now)
{ c->manual=(uint8_t)(manual!=0); c->requested=0; c->manual_at=now; control_tick(c,now); }
int control_relay(ControlCore *c, unsigned relay, unsigned on, uint32_t now)
{
  control_tick(c,now);
  if(!c->manual || c->fault || relay<1 || relay>4) return 0;
  uint8_t bit=(uint8_t)(1U<<(relay-1));
  uint8_t request=on ? c->requested|bit : c->requested&(uint8_t)~bit;
  if((request&3U) && (request&12U)) return 0;
  c->requested=request; c->manual_at=now; control_tick(c,now); return 1;
}
