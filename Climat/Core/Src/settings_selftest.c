#include "settings.h"
#include "temperature_probe.h"
#include <string.h>
static uint8_t fake_flash[SETTINGS_SIZE];
static unsigned writes,fail_read,fail_write,corrupt_write;
static int read_fake(uint8_t *p)
{if(fail_read) return 0;memcpy(p,fake_flash,SETTINGS_SIZE);return 1;}
static int write_fake(const uint8_t *p)
{
  ++writes;memcpy(fake_flash,p,SETTINGS_SIZE);
  if(fail_write) {memset(fake_flash,255,4);return 0;}
  if(corrupt_write) fake_flash[8]^=1;
  return 1;
}
static void w16(uint8_t *p,unsigned v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void crc(uint8_t *p,unsigned n)
{uint32_t c=settings_crc32(p,n);for(unsigned i=0;i<4;++i) p[n+i]=(uint8_t)(c>>(8*i));}
static void rom(uint8_t *p,unsigned id)
{memset(p,0,8);p[0]=0x28;p[1]=(uint8_t)id;p[7]=probe_crc8(p,7);}
uint32_t settings_selftest(void)
{
  uint32_t e=0;Settings s,d,retained;uint8_t good[SETTINGS_SIZE],b[SETTINGS_SIZE];
  settings_defaults(&s);rom(s.sensor_rom[0],1);rom(s.sensor_rom[1],2);
  s.automatic.cooling_milli_per_min=65535;s.device_name[0]='A';s.device_name[1]=0xff;
  if(settings_crc32((const uint8_t *)"123456789",9)!=0xcbf43926U) e|=1;
  if(!settings_encode(&s,good) || settings_decode(good,&d)!=SETTINGS_OK ||
     !settings_encode(&d,b) || memcmp(good,b,SETTINGS_SIZE)) e|=2;
  retained=d;
  for(unsigned bit=0;bit<SETTINGS_SIZE*8;++bit) {
    memcpy(b,good,SETTINGS_SIZE);b[bit/8]^=(uint8_t)(1U<<(bit%8));
    if(settings_decode(b,&d)!=SETTINGS_INVALID || memcmp(&d,&retained,sizeof(d))) e|=4;
  }
  memset(b,255,SETTINGS_SIZE);if(settings_decode(b,&d)!=SETTINGS_EMPTY) e|=8;
  memcpy(b+4,good+4,SETTINGS_SIZE-4);if(settings_decode(b,&d)!=SETTINGS_INVALID) e|=16;
  const unsigned corrupt[]={4,6,10,34,36,38,40,42};
  for(unsigned i=0;i<sizeof(corrupt)/sizeof(corrupt[0]);++i) {
    memcpy(b,good,SETTINGS_SIZE);b[corrupt[i]]=0;crc(b,68);
    if(settings_decode(b,&d)!=SETTINGS_INVALID) e|=32;
  }
  memcpy(b,good,SETTINGS_SIZE);b[28]=2;crc(b,68);
  if(settings_decode(b,&d)!=SETTINGS_INVALID) e|=32;
  memcpy(b,good,SETTINGS_SIZE);b[32]=b[33]=0;crc(b,68);
  if(settings_decode(b,&d)!=SETTINGS_INVALID) e|=32;
  memcpy(b,good,SETTINGS_SIZE);b[62]=1;crc(b,68);
  if(settings_decode(b,&d)!=SETTINGS_INVALID) e|=32;
  memcpy(b,good,SETTINGS_SIZE);memcpy(b+50,b+42,8);crc(b,68);
  if(settings_decode(b,&d)!=SETTINGS_INVALID) e|=64;
  memcpy(b,good,SETTINGS_SIZE);b[49]^=1;crc(b,68);
  if(settings_decode(b,&d)!=SETTINGS_INVALID) e|=128;
  /* Old records migrate to measured control, widths, no invented speed or ROM. */
  const unsigned old[9]={184,176,184,168,176,400,384,560,544};
  for(unsigned version=1;version<=3;++version) {
    memset(b,255,SETTINGS_SIZE);memset(b,0,48);memcpy(b,"CLMT",4);
    w16(b+4,version);w16(b+6,48);
    for(unsigned i=0;i<9;++i) w16(b+8+2*i,old[i]);
    b[26]=3;b[27]=2;b[28]=1;w16(b+30,600);w16(b+32,168);
    w16(b+34,version==1?30:5);w16(b+36,30);w16(b+38,30);w16(b+40,60);
    if(version==3) {b[29]='A';b[42]='0';b[43]='1';}crc(b,44);
    if(settings_decode(b,&d)!=SETTINGS_OK || d.automatic.heat1_on!=176 ||
       d.automatic.heat1_hyst!=8 || d.automatic.cool1_hyst!=16 ||
       d.automatic.cooling_milli_per_min || d.sensor_rom[0][0] || d.sensor_rom[1][0] ||
       d.device_name[0]!=(version==3?'A':0) || d.fail_count!=3 || d.recover_count!=2 ||
       d.manual_seconds!=600) e|=256;
    if(!settings_encode(&d,good) || good[4]!=5 || good[6]!=72) e|=512;
    b[8]=0;crc(b,44);if(settings_decode(b,&d)!=SETTINGS_INVALID) e|=1024;
  }
  /* Exact v4 layout/CRC, plus arbitrary bytes beyond its 64-byte record. */
  memset(b,0,SETTINGS_SIZE);memcpy(b,"CLMT",4);w16(b+4,4);w16(b+6,64);
  auto_config_write(&s.automatic,b+8);b[26]=3;b[27]=2;b[28]=1;
  memcpy(b+29,"ABC",3);w16(b+32,600);w16(b+34,5);w16(b+36,168);
  w16(b+38,30);w16(b+40,60);memcpy(b+42,s.sensor_rom,16);crc(b,60);memset(b+64,0xA5,8);
  if(settings_decode(b,&d)!=SETTINGS_OK || d.heater_min_on_seconds!=30 || d.fan_min_on_seconds!=60 ||
     d.automatic.rising_milli_per_min || d.automatic.rate_hyst_milli_per_min ||
     d.automatic.cooling_milli_per_min!=65535 || memcmp(d.sensor_rom,s.sensor_rom,16) || memcmp(d.device_name,"ABC",3)) e|=524288;
  if(!settings_encode(&d,good) || good[4]!=5 || settings_decode(good,&retained)!=SETTINGS_OK) e|=524288;
  w16(b+38,31);crc(b,60);if(settings_decode(b,&d)!=SETTINGS_INVALID) e|=524288;
  s.automatic.rising_milli_per_min=65535;s.automatic.rate_hyst_milli_per_min=65535;
  s.heater_min_on_seconds=600;s.fan_min_on_seconds=1;
  SettingsBackend io={read_fake,write_fake};unsigned same;
  writes=fail_read=fail_write=corrupt_write=0;memset(fake_flash,255,SETTINGS_SIZE);
  if(settings_load(io,&d)!=SETTINGS_EMPTY) e|=2048;
  if(settings_save(io,&s,&same)!=SETTINGS_OK || same || writes!=1) e|=4096;
  if(settings_save(io,&s,&same)!=SETTINGS_OK || !same || writes!=1) e|=8192;
  /* Reset/load round trip retains each role and the maximum speed. */
  memset(&d,0,sizeof(d));if(settings_load(io,&d)!=SETTINGS_OK ||
     memcmp(d.sensor_rom,s.sensor_rom,16) || d.automatic.cooling_milli_per_min!=65535 ||
     d.automatic.rising_milli_per_min!=65535 || d.automatic.rate_hyst_milli_per_min!=65535 ||
     d.heater_min_on_seconds!=600 || d.fan_min_on_seconds!=1) e|=16384;
  fail_read=1;if(settings_save(io,&s,&same)!=SETTINGS_IO_ERROR || writes!=1) e|=32768;
  fail_read=0;fail_write=1;s.manual_seconds=601;
  if(settings_save(io,&s,&same)!=SETTINGS_IO_ERROR || settings_load(io,&d)!=SETTINGS_INVALID) e|=65536;
  fail_write=0;corrupt_write=1;if(settings_save(io,&s,&same)!=SETTINGS_IO_ERROR) e|=131072;
  corrupt_write=0;if(settings_save(io,&s,&same)!=SETTINGS_OK) e|=262144;
  return e;
}
