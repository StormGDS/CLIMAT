/* Execute production C with virtual time. No HAL, device or serial access. */
#include "auto_control.h"
#include "control_core.h"
#include "temperature_probe.h"
#include "settings.h"
#include <string.h>
#define CHECK(x) do {if(!(x)) return __LINE__;} while(0)
static const AutoConfig profile={368,16,336,16,432,16,464,16,1000,1000,200};
static AutoControl a;
static ControlCore c;
/* Read by the emulator as evidence, not produced by a separate model. */
uint32_t rate_trace[512][6],rate_trace_count;
static unsigned scenario;
static uint8_t step(uint32_t t,int16_t raw,int32_t rate,unsigned valid,unsigned trend)
{
  control_observe(&c,valid,0,t);
  c.auto_requested=auto_sample(&a,c.source,valid,raw,trend,rate,c.logical,t);
  control_tick(&c,t);
  if(rate_trace_count<512) {
    uint32_t *p=rate_trace[rate_trace_count++];p[0]=scenario;p[1]=t;p[2]=(uint32_t)(int32_t)raw;
    p[3]=(uint32_t)rate;p[4]=c.auto_requested;p[5]=c.logical;
  }
  return c.logical;
}
static void start(uint32_t now,unsigned min_on)
{
  memset(&a,0,sizeof(a));auto_configure(&a,&profile);control_init(&c,now);
  c.heater_min_on_seconds=(uint16_t)min_on;
}
uint32_t rate_boundaries_test(void)
{
  AutoConfig p=profile;AutoControl x={0};CHECK(auto_configure(&x,&p));
  auto_sample(&x,0,1,320,1,0,1,0);
  CHECK(auto_sample(&x,0,1,320,1,999,1,4999)==3 && !x.rate_ready);
  CHECK(auto_sample(&x,0,1,320,1,1000,3,5000)==1 && x.rising_blocked);
  CHECK(auto_sample(&x,0,1,320,1,801,3,5001)==1);
  CHECK(auto_sample(&x,0,1,320,1,800,3,5002)==3 && !x.rising_blocked);
  CHECK(auto_sample(&x,0,1,320,1,INT32_MAX,3,5003)==1);
  CHECK(auto_sample(&x,0,1,320,1,INT32_MIN,3,5004)==3);
  /* Cooling early latch above temperature K2 threshold. */
  CHECK(auto_sample(&x,0,1,360,1,-999,1,6000)==3); /* previous extreme latched */
  CHECK(auto_sample(&x,0,1,360,1,-800,1,6001)==1);
  CHECK(auto_sample(&x,0,1,360,1,-1000,1,6002)==3);
  CHECK(auto_sample(&x,0,1,360,1,-801,1,6003)==3);
  CHECK(auto_sample(&x,0,1,360,1,-800,1,6004)==1);
  CHECK(auto_sample(&x,0,1,360,1,-1001,1,6005)==3);
  CHECK(auto_sample(&x,0,1,360,0,-1001,1,6006)==1 && !x.cooling_early);
  CHECK(auto_sample(&x,1,1,320,1,1000,1,7000)==3 && !x.rate_ready);
  CHECK(auto_sample(&x,1,1,320,1,1000,1,11999)==3);
  CHECK(auto_sample(&x,1,1,320,1,1000,1,12000)==1);
  CHECK(!auto_sample(&x,1,0,320,1,1000,1,12001) && !x.tracking);
  CHECK(auto_sample(&x,1,1,320,1,1000,1,12002)==3 && !x.rate_ready);
  p.rate_hyst_milli_per_min=0;CHECK(auto_configure(&x,&p));
  auto_sample(&x,0,1,320,1,0,1,0);
  for(unsigned i=0;i<5;++i) CHECK(auto_sample(&x,0,1,320,1,1000,3,5000+i)==1);
  CHECK(auto_sample(&x,0,1,320,1,999,3,5005)==3);
  for(unsigned i=0;i<5;++i) CHECK(auto_sample(&x,0,1,360,1,-1000,1,6000+i)==3);
  CHECK(auto_sample(&x,0,1,360,1,-999,1,6005)==1);
  p.rising_milli_per_min=p.cooling_milli_per_min=0;p.rate_hyst_milli_per_min=65535;
  CHECK(auto_configure(&x,&p));auto_sample(&x,0,1,320,1,0,1,0);
  CHECK(auto_sample(&x,0,1,320,1,INT32_MAX,1,5000)==3);
  CHECK(auto_sample(&x,0,1,360,1,INT32_MIN,1,5001)==1);
  p=profile;p.rate_hyst_milli_per_min=1001;CHECK(!auto_configure(&x,&p));
  p.cooling_milli_per_min=0;CHECK(!auto_configure(&x,&p));
  p.rising_milli_per_min=65535;p.rate_hyst_milli_per_min=65535;CHECK(auto_configure(&x,&p));
  auto_sample(&x,0,1,320,1,0,1,0);
  CHECK(auto_sample(&x,0,1,320,1,65535,1,5000)==1);
  CHECK(auto_sample(&x,0,1,320,1,1,1,5001)==1);
  CHECK(auto_sample(&x,0,1,320,1,0,1,5002)==3);
  return 0;
}
uint32_t rate_timing_test(void)
{
  rate_trace_count=0;scenario=1;start(0,30);
  CHECK(step(0,320,0,1,1)==0);
  CHECK(step(29999,320,0,1,1)==0);
  CHECK(step(30000,320,0,1,1)==1);
  CHECK(step(30001,320,0,1,1)==3);
  CHECK(step(30002,320,1000,1,1)==3);
  CHECK(step(60000,320,1000,1,1)==3);
  CHECK(step(60001,320,1000,1,1)==1);
  CHECK(step(60002,320,800,1,1)==1);
  CHECK(step(90000,320,800,1,1)==1);
  CHECK(step(90001,320,800,1,1)==3);
  CHECK(step(90002,320,1000,1,1)==3);
  CHECK(step(120000,320,800,1,1)==3);
  CHECK(step(120001,320,800,1,1)==3); /* cancelled OFF must not fire */
  CHECK(step(120002,320,1000,1,1)==1);
  /* Same input, different heater MIN_ON. */
  for(unsigned j=0;j<2;++j) {
    scenario=2+j;start(0,j?60:10);step(0,320,0,1,1);
    step(30000,320,0,1,1);CHECK(step(31000,320,0,1,1)==3);
    CHECK(step(32000,320,1000,1,1)==3);
    CHECK(step(41000,320,1000,1,1)==(j?3:1));
    CHECK(step(91000,320,1000,1,1)==1);
  }
  /* All four channels, independent min_on and fixed min_off, wraparound. */
  for(unsigned k=0;k<4;++k) for(unsigned wrap=0;wrap<2;++wrap) {
    uint32_t base=wrap?UINT32_MAX-10000U:0,off=k<2?30000:60000;
    uint8_t bit=(uint8_t)(1U<<k),want=k==1?3:bit;
    control_init(&c,base);c.heater_min_on_seconds=7;c.fan_min_on_seconds=9;
    c.auto_requested=want;control_sample(&c,1,0,base+off-1);CHECK(!c.logical);
    control_sample(&c,1,0,base+off);CHECK(c.logical==want);
    c.auto_requested=0;uint32_t on=k<2?7000:9000;
    control_sample(&c,1,0,base+off+on-1);CHECK(c.logical==want);
    control_sample(&c,1,0,base+off+on);CHECK(!c.logical);
    c.auto_requested=want;control_sample(&c,1,0,base+2*off+on-1);CHECK(!c.logical);
    control_sample(&c,1,0,base+2*off+on);CHECK(c.logical==want);
  }
  scenario=4;start(UINT32_MAX-35000U,30);uint32_t b=UINT32_MAX-35000U;
  step(b,320,0,1,1);step(b+30000,320,0,1,1);step(b+31000,320,0,1,1);
  CHECK(step(b+32000,320,1000,1,1)==3);
  CHECK(step(b+60999,320,1000,1,1)==3);CHECK(step(b+61000,320,1000,1,1)==1);
  scenario=5;start(0,600);step(0,320,0,1,1);step(30000,320,0,1,1);step(31000,320,0,1,1);
  CHECK(step(32000,320,0,0,0)==4); /* fault bypasses 600 second minimum */
  CHECK(step(33000,320,0,1,1)==4); /* fan minimum/interlock on recovery */
  start(0,600);step(0,320,0,1,1);step(30000,320,0,1,1);step(31000,320,0,1,1);
  c.fail_count=3;c.recover_count=2;
  control_observe(&c,0,1,32000);control_tick(&c,32000);CHECK(!c.logical && !c.fault);
  control_observe(&c,0,1,33000);CHECK(c.source==0);
  control_observe(&c,0,1,34000);CHECK(c.source==1);
  control_observe(&c,1,1,35000);CHECK(c.source==1);
  control_observe(&c,1,1,36000);CHECK(c.source==0);
  control_tick(&c,38500);CHECK(!c.fault);control_tick(&c,38501);CHECK(c.fault && c.logical==4);
  /* MANUAL ignores AUTO growth prohibition and all min times. */
  start(0,600);step(0,320,0,1,1);step(5000,320,1000,1,1);CHECK(a.rising_blocked);
  control_mode(&c,1,5000);CHECK(control_relay(&c,2,1,5000) && c.logical==2);
  CHECK(control_relay(&c,2,0,5001) && !c.logical);
  CHECK(control_relay(&c,4,1,5002) && c.logical==8);
  CHECK(!control_relay(&c,1,1,5003));c.timeout_ms=10;
  control_sample(&c,1,0,5011);CHECK(c.manual);control_sample(&c,1,0,5012);CHECK(!c.manual);
  return 0;
}
/* Wrappers invoke production sample/fail functions in test_temperature_probe.c. */
void test_probe_sample(ProbeSensor *,int16_t,uint32_t);
void test_probe_fail(ProbeSensor *);
uint32_t rate_integration_test(void)
{
  /* Identical 1 raw16/s rising ramp: R=0 versus R=3 C/min.
   * Both sensors independently compute the actual five-second derivative. */
  for(unsigned enabled=0;enabled<2;++enabled) {
    ProbeSensor sensors[2];memset(sensors,0,sizeof(sensors));scenario=10+enabled;start(0,10);
    a.config.rising_milli_per_min=enabled?3000:0;
    for(unsigned sec=0;sec<=50;++sec) {
      int16_t raw=(int16_t)(240+sec);
      test_probe_sample(&sensors[0],raw,sec*1000);
      test_probe_sample(&sensors[1],(int16_t)(400-sec),sec*1000);
      if(sec>=5) CHECK(sensors[0].trend_valid && sensors[0].rate_milli_per_min==3750 && sensors[1].rate_milli_per_min==-3750);
      uint8_t mask=step(sec*1000,sensors[0].raw16,sensors[0].rate_milli_per_min,1,sensors[0].trend_valid);
      if(sec>=31) CHECK(mask==(enabled?1:3));
    }
    test_probe_fail(&sensors[0]);CHECK(!sensors[0].valid && !sensors[0].trend_valid && !sensors[0].count);
    for(unsigned sec=0;sec<6;++sec) {
      test_probe_sample(&sensors[0],300,60000+sec*1000);
      CHECK(sensors[0].trend_valid==(sec==5));
    }
  }
  /* Actual cooling derivative controls early K2 before Ton2 is reached. */
  for(unsigned enabled=0;enabled<2;++enabled) {
    ProbeSensor sensor={0};scenario=13+enabled;start(0,10);
    a.config.cooling_milli_per_min=enabled?1000:0;
    for(unsigned sec=0;sec<=42;++sec) {
      test_probe_sample(&sensor,(int16_t)(400-sec),sec*1000);
      uint8_t mask=step(sec*1000,sensor.raw16,sensor.rate_milli_per_min,1,sensor.trend_valid);
      if(sec>=34) CHECK(mask==(enabled?3:1));
    }
  }
  /* +/- one LSB raw noise at constant temperature cannot trip 1 C/min thresholds. */
  ProbeSensor noisy={0};scenario=15;start(0,10);
  for(unsigned sec=0;sec<=70;++sec) {
    test_probe_sample(&noisy,(int16_t)(360+sec%2),sec*1000);
    uint8_t mask=step(sec*1000,noisy.raw16,noisy.rate_milli_per_min,1,noisy.trend_valid);
    if(sec>=30) CHECK(mask==1 && !a.rising_blocked && !a.cooling_early);
  }
  /* Temperature steps/plateaus/noise, both heater and fan stages. */
  scenario=12;start(0,2);c.fan_min_on_seconds=3;
  const uint32_t ts[]={0,30000,31000,33000,34000,60000,61000,62000,63000,64000,65000,66000,67000,100000};
  const int16_t raw[]={320,320,320,351,352,433,432,465,464,449,448,417,416,320};
  const uint8_t expected[]={0,1,3,3,1,4,4,12,12,12,4,4,0,1};
  for(unsigned i=0;i<sizeof(expected);++i) CHECK(step(ts[i],raw[i],0,1,0)==expected[i]);
  /* S A/B, R A/B and common H A/B at fixed temperature/rate trajectories. */
  for(unsigned choice=0;choice<2;++choice) {
    AutoControl x={0};AutoConfig p=profile;p.cooling_milli_per_min=choice?2000:1000;
    CHECK(auto_configure(&x,&p));auto_sample(&x,0,1,360,1,0,1,0);
    CHECK(auto_sample(&x,0,1,360,1,-1500,1,5000)==(choice?1:3));
    p=profile;p.rate_hyst_milli_per_min=choice?500:0;CHECK(auto_configure(&x,&p));
    auto_sample(&x,0,1,320,1,0,1,0);auto_sample(&x,0,1,320,1,1000,1,5000);
    CHECK(auto_sample(&x,0,1,320,1,750,1,5001)==(choice?1:3));
    auto_sample(&x,0,1,360,1,-1000,1,5002);
    CHECK(auto_sample(&x,0,1,360,1,-750,1,5003)==(choice?3:1));
  }
  /* Arithmetic: physical extreme delta exceeds int32 multiplication. */
  for(unsigned dt=4499;dt<=6001;++dt) {
    ProbeSensor s={0};for(unsigned i=0;i<5;++i) test_probe_sample(&s,-880,i*900);
    test_probe_sample(&s,2000,dt);
    CHECK(s.trend_valid==(dt>=4500 && dt<=6000));
    if(s.trend_valid) CHECK(s.rate_milli_per_min==(int32_t)(2880LL*3750000/dt));
  }
  ProbeSensor s={0};for(unsigned i=0;i<5;++i) test_probe_sample(&s,321,i*1000);
  test_probe_sample(&s,320,5001);CHECK(s.rate_milli_per_min==-749);
  return 0;
}


uint32_t parameter_sensitivity_test(void)
{
  /* Vary each temperature threshold/width independently on identical two-point inputs. */
  const int16_t first[]={370,368,340,336,434,440,466,470};
  const int16_t second[]={370,388,340,356,434,412,466,444};
  const uint8_t low[]={0,0,1,1,4,0,12,4},high[]={1,1,3,3,0,4,4,12};
  for(unsigned field=0;field<8;++field) for(unsigned variant=0;variant<2;++variant) {
    AutoConfig cfg=profile;int16_t values[8];memcpy(values,&cfg,16);
    if(variant) values[field]+=field%2?8:8;
    memcpy(&cfg,values,16);AutoControl x={0};CHECK(auto_configure(&x,&cfg));
    auto_sample(&x,0,1,first[field],0,0,1,0);
    CHECK(auto_sample(&x,0,1,second[field],0,0,1,1000)==(variant?high[field]:low[field]));
  }
  for(unsigned fan=0;fan<2;++fan) for(unsigned variant=0;variant<2;++variant) {
    start(0,30);c.fan_min_on_seconds=variant?20:5;
    int16_t t=fan?470:440;step(0,t,0,1,0);CHECK(step(60000,t,0,1,0)==(fan?12:4));
    CHECK(step(65000,400,0,1,0)==(variant?(fan?12:4):0));
    CHECK(step(80000,400,0,1,0)==0);
  }
  /* Alternation follows completed cycles, never a transient request. */
  start(0,30);a.alternate=1;c.fan_min_on_seconds=1;
  step(0,440,0,1,0);CHECK(step(60000,440,0,1,0)==4);
  step(60001,440,0,1,0);CHECK(step(61000,400,0,1,0)==0);step(62000,400,0,1,0);
  CHECK(step(63000,440,0,1,0)==8);
  return 0;
}
