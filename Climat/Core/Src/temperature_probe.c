#include "temperature_probe.h"
#include <string.h>
#ifdef CLIMAT_COMPACT
#define PROBE_TEXT(short_text,full_text) short_text
#else
#define PROBE_TEXT(short_text,full_text) full_text
#endif


uint8_t probe_crc8(const uint8_t *data, size_t length)
{
  uint8_t crc = 0;
  while (length--) {
    uint8_t b = *data++;
    for (unsigned i = 0; i < 8; ++i) {
      unsigned mix = (crc ^ b) & 1U;
      crc >>= 1; if (mix) crc ^= 0x8c;
      b >>= 1;
    }
  }
  return crc;
}

int probe_decode(const uint8_t s[9], int16_t *raw16)
{
  if (probe_crc8(s, 8) != s[8]) return 0;
  /* Also reject a stuck-low bus (all zeros has a valid CRC). */
  if ((s[4] & 0x9fU) != 0x1fU || s[5] != 0xff || s[7] != 0x10) return -1;
  int16_t raw = (int16_t)((uint16_t)s[0] | ((uint16_t)s[1] << 8));
  unsigned resolution = (s[4] >> 5) & 3U;
  raw = (int16_t)((uint16_t)raw & (uint16_t)~((1U << (3U-resolution))-1U));
  if (raw < -55*16 || raw > 125*16) return -1;
  *raw16 = raw;
  return 1;
}

static void wr(TemperatureProbe *p, uint8_t v)
{ for (unsigned i=0; i<8; ++i) { p->bus.write_bit(v & 1U); v >>= 1; } }
static uint8_t rd(TemperatureProbe *p)
{ uint8_t v=0; for(unsigned i=0;i<8;++i) v |= (uint8_t)(p->bus.read_bit()<<i); return v; }
static void match(TemperatureProbe *p, const uint8_t rom[8])
{ wr(p,0x55); for(unsigned i=0;i<8;++i) wr(p,rom[i]); }
static const char *reset_error(int result)
{ return result < 0 ? "BUS_LOW" : PROBE_TEXT("ABSENT","NO_PRESENCE"); }
static void fail(ProbeSensor *s, const char *why)
{
  s->valid=0; s->trend_valid=0; s->count=0; s->head=0;
  s->status=why;
  if(s->errors != UINT32_MAX) ++s->errors;
}
static int fail_all(TemperatureProbe *p, const char *why)
{
  for(unsigned i=0;i<2;++i) fail(&p->sensor[i],why);
  p->converting=0;
  return 1;
}

/* Dallas ROM tree search, bounded even with noise or extra bus devices.
 * Discovery never assigns roles. Explicit persisted ROM bindings own S1/S2. */
static void scan(TemperatureProbe *p)
{
  uint8_t rom[8]={0};
  unsigned last=0;
  p->scan_limited=0; p->scan_status="OK";p->discovered_count=0;
  for(unsigned pass=0;pass<8;++pass) {
    int reset=p->bus.reset();
    if(reset!=1) { p->scan_status=reset_error(reset); return; }
    wr(p,0xf0);
    unsigned next=0;
    for(unsigned bit=1;bit<=64;++bit) {
      unsigned a=p->bus.read_bit(), b=p->bus.read_bit(), direction;
      if(a && b) { p->scan_status=PROBE_TEXT("SEARCH","SEARCH_ERROR"); return; }
      if(a!=b) direction=a;
      else {
        direction=bit<last ? ((rom[(bit-1)/8]>>((bit-1)%8))&1U) : (bit==last);
        if(!direction) next=bit;
      }
      uint8_t mask=(uint8_t)(1U<<((bit-1)%8));
      if(direction) rom[(bit-1)/8]|=mask; else rom[(bit-1)/8]&=(uint8_t)~mask;
      p->bus.write_bit(direction);
    }
    if(probe_crc8(rom,7)!=rom[7]) { p->scan_status="ROM_CRC"; return; }
    if(rom[0]==0x28) {
      if(p->discovered_count<2) memcpy(p->discovered[p->discovered_count++],rom,8);
      else p->scan_limited=1;
    } else p->scan_status=PROBE_TEXT("FAMILY","OTHER_FAMILY");
    last=next;
    if(!last) return;
  }
  p->scan_limited=1;
}

static int32_t trend_rate(int32_t delta, uint32_t dt)
{
  /* |delta|<=2880, 4500<=dt<=6000: all products fit uint32_t. */
  uint32_t magnitude=delta<0?0U-(uint32_t)delta:(uint32_t)delta;
  uint32_t rate=magnitude*(3750000U/dt)+magnitude*(3750000U%dt)/dt;
  return delta<0?-(int32_t)rate:(int32_t)rate;
}
static void sample(ProbeSensor *s, int16_t value, uint32_t now)
{
  unsigned pos=s->head;
  s->raw16=value; s->valid=1; s->status=value==85*16 ? PROBE_TEXT("OK85","OK_85_CHECK_POWER") : "OK";
  s->history[pos]=value; s->time[pos]=now;
  s->head=(uint8_t)((pos+1)%TREND_SAMPLES);
  if(s->count<TREND_SAMPLES) ++s->count;
  s->trend_valid=0;
  if(s->count==TREND_SAMPLES) {
    uint32_t dt=now-s->time[s->head];
    if(dt>=TREND_WINDOW_SECONDS*1000U-500U && dt<=TREND_WINDOW_SECONDS*1000U+1000U) {
      int32_t delta=(int32_t)value-s->history[s->head];
      /* Exact decomposition avoids 64-bit division on M0. */
      s->rate_milli_per_min=trend_rate(delta,dt);
      s->trend_valid=1;
    }
  }
}

uint32_t probe_trend_selftest(void)
{
  ProbeSensor s={0};uint32_t e=0;
  for(unsigned i=0;i<5;++i) {
    sample(&s,(int16_t)(320+16*i),i*1000);
    if(s.trend_valid) e|=1;
  }
  sample(&s,400,5000);
  if(!s.trend_valid || s.rate_milli_per_min!=60000) e|=2;
  sample(&s,416,6000);
  if(!s.trend_valid || s.rate_milli_per_min!=60000) e|=4;
  fail(&s,"TEST");sample(&s,400,7000);
  if(s.trend_valid || s.count!=1) e|=8;
  memset(&s,0,sizeof(s));
  for(unsigned i=0;i<6;++i) sample(&s,(int16_t)(400-16*i),UINT32_MAX-2000U+i*1000U);
  if(!s.trend_valid || s.rate_milli_per_min!=-60000) e|=16;
  sample(&s,300,10000);
  if(s.trend_valid) e|=32; /* long gap cannot masquerade as a 5-second window */
  const int16_t deltas[]={-2880,-2879,-880,-17,-16,-1,0,1,16,17,880,2879,2880};
  for(uint32_t dt=4500;dt<=6000;++dt)
    for(unsigned i=0;i<sizeof(deltas)/sizeof(deltas[0]);++i)
      if(trend_rate(deltas[i],dt)!=(int32_t)((int64_t)deltas[i]*3750000/dt)) e|=64;
  return e;
}

void probe_init(TemperatureProbe *p, ProbeBus bus)
{
  memset(p,0,sizeof(*p)); p->bus=bus; p->scan_status=PROBE_TEXT("NO_SCAN","NOT_SCANNED");
  for(unsigned i=0;i<2;++i) p->sensor[i].status=PROBE_TEXT("UNBOUND","NOT_FOUND");
}

int probe_bindings_valid(const uint8_t rom[2][8])
{
  const uint8_t empty[8]={0};
  for(unsigned i=0;i<2;++i)
    if(memcmp(rom[i],empty,8) && (rom[i][0]!=0x28 || probe_crc8(rom[i],8))) return 0;
  return !memcmp(rom[0],empty,8) || memcmp(rom[0],rom[1],8)!=0;
}
int probe_set_bindings(TemperatureProbe *p, const uint8_t rom[2][8])
{
  if(!probe_bindings_valid(rom)) return 0;
  ProbeBus bus=p->bus;
  /* Callers supply a separate settings buffer, never pointers into p. */
  probe_init(p,bus);
  for(unsigned i=0;i<2;++i) {
    memcpy(p->sensor[i].rom,rom[i],8);
    p->sensor[i].assigned=(uint8_t)(rom[i][0]==0x28);
  }
  return 1;
}

int probe_poll(TemperatureProbe *p, uint32_t now)
{
  if(p->converting) {
    if((uint32_t)(now-p->convert_at)<760) return 0;
    if(!p->bus.read_bit()) {
      if((uint32_t)(now-p->convert_at)<850) return 0;
      return fail_all(p,PROBE_TEXT("TIMEOUT","CONVERSION_TIMEOUT"));
    }
    for(unsigned i=0;i<2;++i) {
      ProbeSensor *s=&p->sensor[i];
      if(!s->assigned) { fail(s,PROBE_TEXT("UNBOUND","NOT_FOUND")); continue; }
      int reset=p->bus.reset();
      if(reset!=1) { fail(s,reset_error(reset)); continue; }
      match(p,s->rom); wr(p,0xbe);
      uint8_t scratch[9]; for(unsigned j=0;j<9;++j) scratch[j]=rd(p);
      int16_t raw=0; int decoded=probe_decode(scratch,&raw);
      if(decoded!=1) { fail(s,decoded==0 ? "SCRATCH_CRC" : PROBE_TEXT("DATA","INVALID_DATA")); continue; }
      sample(s,raw,now);
    }
    p->converting=0;
    return 1;
  }
  if(p->started && (uint32_t)(now-p->cycle_at)<1000) return 0;
  p->cycle_at=now;
  if(!p->started || (uint32_t)(now-p->scan_at)>=5000) { scan(p); p->scan_at=now; }
  p->started=1;
  int reset=p->bus.reset();
  if(reset!=1) return fail_all(p,reset_error(reset));
  wr(p,0xcc); wr(p,0xb4); /* All devices must have external VDD. */
  if(!p->bus.read_bit()) return fail_all(p,PROBE_TEXT("POWER","PARASITE_UNSUPPORTED"));
  reset=p->bus.reset();
  if(reset!=1) return fail_all(p,reset_error(reset));
  wr(p,0xcc); wr(p,0x44);
  p->convert_at=p->bus.now_ms();
  p->converting=1;
  return 0;
}
