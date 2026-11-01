/* Native runner: cc -std=c11 -ICore/Inc tests/test_temperature_probe.c -o probe_test
 * This file deliberately includes the portable implementation to test its
 * internal history handling without exposing it in the production API. */
#include <assert.h>
#include <stdio.h>
#include "../Core/Src/temperature_probe.c"
static uint32_t clock_ms;
static int reset_result=1;
static unsigned read_result=1;
static int fake_reset(void) { return reset_result; }
static void fake_write(unsigned b) { (void)b; }
static unsigned fake_read(void) { return read_result; }
static uint32_t fake_now(void) { return clock_ms; }
static void encode(uint8_t s[9], int16_t raw, uint8_t config)
{
  s[0]=(uint8_t)raw; s[1]=(uint8_t)((uint16_t)raw>>8);
  s[2]=75; s[3]=70; s[4]=config; s[5]=0xff; s[6]=12; s[7]=0x10;
  s[8]=probe_crc8(s,8);
}
#undef assert
#define assert(e) do { if(!(e)) return __LINE__; } while(0)
int probe_test(void)
{
  uint8_t known[9]={0x50,0x05,0x4b,0x46,0x7f,0xff,0x0c,0x10,0x1c};
  int16_t temperature;
  assert(probe_crc8(known,8)==0x1c);
  assert(probe_decode(known,&temperature)==1 && temperature==1360);
  known[0]^=1; assert(probe_decode(known,&temperature)==0);
  uint8_t s[9];
  encode(s,-162,0x7f); assert(probe_decode(s,&temperature)==1 && temperature==-162);
  encode(s,407,0x1f); assert(probe_decode(s,&temperature)==1 && temperature==400);
  encode(s,-55*16,0x7f); assert(probe_decode(s,&temperature)==1);
  encode(s,125*16,0x7f); assert(probe_decode(s,&temperature)==1);
  encode(s,125*16+1,0x7f); assert(probe_decode(s,&temperature)==-1);
  memset(s,0,9); assert(probe_decode(s,&temperature)==-1);
  memset(s,0xff,9); assert(probe_decode(s,&temperature)==0);
  ProbeSensor sensor={0};
  for(unsigned i=0;i<5;++i) sample(&sensor,(int16_t)(320+i),i*1000);
  assert(!sensor.trend_valid);
  sample(&sensor,325,5000);
  assert(sensor.trend_valid && sensor.rate_milli_per_min==3750);

  fail(&sensor,"TEST"); assert(!sensor.valid && !sensor.count && !sensor.trend_valid);
  for(unsigned i=0;i<6;++i) sample(&sensor,320,UINT32_MAX-2000U+i*1000U);
  assert(sensor.trend_valid && sensor.rate_milli_per_min==0);
  assert(probe_trend_selftest()==0);
  TemperatureProbe p;
  ProbeBus bus={fake_reset,fake_write,fake_read,fake_now};
  probe_init(&p,bus);
  reset_result=-1; assert(probe_poll(&p,0)==1);
  assert(!strcmp(p.sensor[0].status,"BUS_LOW"));
  assert(probe_poll(&p,999)==0);
  reset_result=0; assert(probe_poll(&p,1000)==1);
  assert(!strcmp(p.sensor[0].status,PROBE_TEXT("ABSENT","NO_PRESENCE")));
  reset_result=1; read_result=0; assert(probe_poll(&p,2000)==1);
  assert(!strcmp(p.sensor[0].status,PROBE_TEXT("POWER","PARASITE_UNSUPPORTED")));
  read_result=1; clock_ms=3004;
  assert(probe_poll(&p,3000)==0 && p.converting);
  assert(p.convert_at==3004); /* Conversion starts after bus work. */
  read_result=0;
  assert(probe_poll(&p,3763)==0);
  assert(probe_poll(&p,3800)==0);
  assert(probe_poll(&p,3854)==1 && !p.converting);
  assert(!strcmp(p.sensor[1].status,PROBE_TEXT("TIMEOUT","CONVERSION_TIMEOUT")));

  return 0;
}

/* One physical DS18B20. Search and MATCH ROM are emulated bit by bit so that
 * a missing bound sensor cannot be silently replaced by a discovered one. */
static uint8_t present_rom[8],matched_rom[8],scratch_rom[9];
static unsigned bus_mode,bus_value,bus_bits,rom_at,search_at,search_phase,read_at;
static int device_reset(void)
{bus_mode=bus_value=bus_bits=rom_at=search_at=search_phase=read_at=0;return 1;}
static void device_write(unsigned bit)
{
  if(bus_mode==1) {++search_at;search_phase=0;return;}
  bus_value|=bit<<bus_bits;
  if(++bus_bits!=8) return;
  uint8_t byte=(uint8_t)bus_value;bus_bits=bus_value=0;
  if(bus_mode==2) {
    matched_rom[rom_at++]=byte;if(rom_at==8) bus_mode=0;
  } else if(byte==0xf0) bus_mode=1;
  else if(byte==0x55) {bus_mode=2;rom_at=0;}
  else if(byte==0xbe) {bus_mode=3;read_at=0;}
}
static unsigned device_read(void)
{
  if(bus_mode==1) {
    unsigned bit=(present_rom[search_at/8]>>(search_at%8))&1U;
    return search_phase++ ? !bit : bit;
  }
  if(bus_mode==3) {
    unsigned at=read_at++;
    return memcmp(matched_rom,present_rom,8)?1U:(scratch_rom[at/8]>>(at%8))&1U;
  }
  return 1;
}
int bindings_test(void)
{
  uint8_t bindings[2][8]={{0}};
  bindings[0][0]=bindings[1][0]=0x28;bindings[0][1]=1;bindings[1][1]=2;
  for(unsigned i=0;i<2;++i) bindings[i][7]=probe_crc8(bindings[i],7);
  ProbeBus bus={device_reset,device_write,device_read,fake_now};TemperatureProbe p;
  probe_init(&p,bus);memcpy(present_rom,bindings[1],8);
  assert(probe_set_bindings(&p,bindings));
  scan(&p);assert(p.discovered_count==1 && !memcmp(p.discovered[0],bindings[1],8));
  assert(!memcmp(p.sensor[0].rom,bindings[0],8) && !memcmp(p.sensor[1].rom,bindings[1],8));
  encode(scratch_rom,320,0x7f);clock_ms=0;
  assert(!probe_poll(&p,0));assert(probe_poll(&p,760));
  assert(!p.sensor[0].valid && p.sensor[1].valid && p.sensor[1].raw16==320);
  /* Reload in the opposite role order; detection must not sort the slots. */
  uint8_t swapped[2][8];memcpy(swapped[0],bindings[1],8);memcpy(swapped[1],bindings[0],8);
  assert(probe_set_bindings(&p,swapped));clock_ms=1000;
  assert(!probe_poll(&p,1000));assert(probe_poll(&p,1760));
  assert(p.sensor[0].valid && !p.sensor[1].valid);
  uint8_t duplicate[2][8];memcpy(duplicate[0],bindings[0],8);memcpy(duplicate[1],bindings[0],8);
  assert(!probe_set_bindings(&p,duplicate) && !memcmp(p.sensor[0].rom,bindings[1],8));
  memset(bindings,0,16);assert(probe_set_bindings(&p,bindings));scan(&p);
  assert(p.discovered_count==1 && !p.sensor[0].assigned && !p.sensor[1].assigned);
  return 0;
}

void test_probe_sample(ProbeSensor *s,int16_t raw,uint32_t now) {sample(s,raw,now);}
void test_probe_fail(ProbeSensor *s) {fail(s,"TEST");}
