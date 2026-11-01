/* Stage-2 on-target tests: local models only, no GPIO, no production state. */
#include "control_core.h"
#include "control_selftest.h"
#include "temperature_probe.h"
uint32_t control_selftest(void)
{
  uint32_t errors=0; ControlCore c;
  control_init(&c,0);control_tick(&c,0);
  if(c.logical!=0 || !c.fault) errors|=2097152;
  control_init(&c,0); control_sample(&c,1,0,0);
  control_mode(&c,1,0);
  if(!control_relay(&c,1,1,0) || c.logical!=1) errors|=1;
  control_sample(&c,1,0,29999); if(c.logical!=1) errors|=2;
  control_sample(&c,1,0,30000); if(c.logical!=1) errors|=4;
  control_relay(&c,1,0,30001); if(c.logical || c.changed_at[0]!=30001) errors|=8;
  control_sample(&c,1,0,60000); if(c.logical!=0) errors|=16;
  control_relay(&c,3,1,60000); if(c.logical!=4) errors|=32;
  if(control_relay(&c,1,1,60000)) errors|=64;
  control_sample(&c,0,0,60001);
  if(!c.fault || c.logical!=4 || c.requested!=0 || control_relay(&c,3,0,60001)) errors|=128;
  control_sample(&c,1,0,60002); if(c.fault || c.requested!=0) errors|=256;
  c.timeout_ms=1000;control_mode(&c,1,60002);
  control_sample(&c,1,0,61002); if(c.manual || c.requested) errors|=512;
  control_init(&c,0);c.fail_count=3;c.recover_count=2;
  control_sample(&c,0,1,0);control_sample(&c,0,1,1000);
  if(c.source!=0) errors|=1024;
  control_sample(&c,0,1,2000);if(c.source!=1) errors|=2048;
  control_sample(&c,1,1,3000);if(c.source!=1) errors|=4096;
  control_sample(&c,1,1,4000);if(c.source!=0) errors|=8192;
  control_tick(&c,6501);if(!c.fault || c.logical!=4) errors|=16384;
  control_init(&c,UINT32_MAX-500);c.timeout_ms=1000;
  control_sample(&c,1,0,UINT32_MAX-500);control_mode(&c,1,UINT32_MAX-500);
  control_sample(&c,1,0,499);if(c.manual) errors|=32768;
  control_init(&c,0);control_sample(&c,1,0,0);control_mode(&c,1,0);
  control_relay(&c,1,1,0);control_sample(&c,1,0,30000);
  control_sample(&c,0,0,30001);if(c.logical!=4) errors|=65536;
  const uint8_t known[9]={0x50,0x05,0x4b,0x46,0x7f,0xff,0x0c,0x10,0x1c};
  int16_t raw=0;
  if(probe_decode(known,&raw)!=1 || raw!=1360) errors|=131072;
  uint8_t bad[9]={0};if(probe_decode(bad,&raw)!=-1) errors|=262144;
  uint8_t negative[9]={0x5e,0xff,75,70,0x7f,0xff,12,0x10,0};
  negative[8]=probe_crc8(negative,8);
  if(probe_decode(negative,&raw)!=1 || raw!=-162) errors|=524288;
  negative[0]^=1;if(probe_decode(negative,&raw)!=0) errors|=1048576;
  /* MANUAL ON/OFF are immediate for every channel, including repeat ON.
   * AUTO still enforces both minimum times for every channel. */
  for(unsigned relay=1;relay<=4;++relay) {
    uint32_t minimum=relay<=2?30000U:60000U;
    uint8_t bit=(uint8_t)(1U<<(relay-1));
    uint8_t auto_bit=relay==2?3:bit;
    control_init(&c,0);control_sample(&c,1,0,0);control_mode(&c,1,0);
    if(!control_relay(&c,relay,1,1) || c.logical!=bit) errors|=4194304;
    if(!control_relay(&c,relay,0,2) || c.logical || c.changed_at[relay-1]!=2) errors|=8388608;
    if(!control_relay(&c,relay,1,3) || c.logical!=bit) errors|=16777216;
    control_relay(&c,relay,0,4);if(c.logical) errors|=8388608;
    /* AUTO still enforces minimum ON for each of the same channels. */
    control_init(&c,0);control_sample(&c,1,0,0);c.auto_requested=auto_bit;
    control_sample(&c,1,0,minimum-1);if(c.logical) errors|=33554432;
    control_sample(&c,1,0,minimum);c.auto_requested=0;
    control_sample(&c,1,0,minimum+1);if(c.logical!=auto_bit) errors|=67108864;
    control_sample(&c,1,0,2*minimum);if(c.logical) errors|=67108864;
  }
  /* Regress the reported heater -> fan -> same heater sequence for all
   * heater/fan pairs. Switching groups must not leave a stale request. */
  for(unsigned heater=1;heater<=2;++heater) for(unsigned fan=3;fan<=4;++fan) {
    uint8_t heat=(uint8_t)(1U<<(heater-1)),cool=(uint8_t)(1U<<(fan-1));
    control_init(&c,0);control_sample(&c,1,0,0);control_mode(&c,1,0);
    control_relay(&c,heater,1,1);if(c.logical!=heat) errors|=134217728;
    control_relay(&c,heater,0,2);if(c.logical) errors|=134217728;
    control_relay(&c,fan,1,3);if(c.logical!=cool) errors|=134217728;
    control_relay(&c,fan,0,4);if(c.logical) errors|=134217728;
    control_relay(&c,heater,1,5);if(c.logical!=heat) errors|=268435456;
    if(control_relay(&c,fan,1,6) || c.logical!=heat) errors|=536870912;
    control_relay(&c,heater,0,7);control_relay(&c,fan,1,8);
    if(c.logical!=cool) errors|=268435456;
    control_relay(&c,fan,0,9);if(c.logical || c.requested) errors|=134217728;
  }
  return errors;
}
