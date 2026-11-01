#include "settings.h"
#include "thermal_timing.h"
#include "temperature_probe.h"
#include <string.h>
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t get32(const uint8_t *p) { return get16(p)|((uint32_t)get16(p+2)<<16); }
static void put16(uint8_t *p, uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p, uint32_t v) {put16(p,(uint16_t)v);put16(p+2,(uint16_t)(v>>16));}
uint32_t settings_crc32(const uint8_t *p, size_t n)
{
  uint32_t crc=0xffffffffU;
  while(n--) {
    crc^=*p++;
    for(unsigned i=0;i<8;++i) crc=(crc>>1)^((crc&1)?0xedb88320U:0);
  }
  return crc^0xffffffffU;
}
void settings_defaults(Settings *s)
{
  memset(s,0,sizeof(*s));
  const AutoConfig profile={176,8,168,8,400,16,560,16,0,0,0};
  s->automatic=profile;s->manual_seconds=600;
  s->heater_min_on_seconds=30;s->fan_min_on_seconds=60;
  /* No invented cooling-speed or failover defaults: 0 = uncommissioned.
   * ROM roles stay empty until explicitly bound and saved. */
}
static int valid(const Settings *s)
{
  AutoControl a={0};
  return s->heater_min_on_seconds>=1 && s->heater_min_on_seconds<=600 &&
         s->fan_min_on_seconds>=1 && s->fan_min_on_seconds<=600 &&
         s->manual_seconds>=1 && s->manual_seconds<=3600 && s->alternate<=1 &&
         auto_configure(&a,&s->automatic) && probe_bindings_valid(s->sensor_rom);
}
int settings_encode(const Settings *s, uint8_t b[SETTINGS_SIZE])
{
  if(!valid(s)) return 0;
  memset(b,0,SETTINGS_SIZE);memcpy(b,"CLMT",4);
  put16(b+4,SETTINGS_VERSION);put16(b+6,SETTINGS_SIZE);
  auto_config_write(&s->automatic,b+8);
  b[26]=s->fail_count;b[27]=s->recover_count;b[28]=s->alternate;
  memcpy(b+29,s->device_name,3);put16(b+32,s->manual_seconds);
  put16(b+34,TREND_WINDOW_SECONDS);put16(b+36,AUTO_MIN_RAW16);
  put16(b+38,s->heater_min_on_seconds);put16(b+40,s->fan_min_on_seconds);memcpy(b+42,s->sensor_rom,16);
  put16(b+58,s->automatic.rising_milli_per_min);
  put16(b+60,s->automatic.rate_hyst_milli_per_min);
  put32(b+68,settings_crc32(b,68));return 1;
}
SettingsResult settings_decode(const uint8_t b[SETTINGS_SIZE], Settings *s)
{
  unsigned erased=1;
  for(unsigned i=0;i<SETTINGS_SIZE;++i) if(b[i]!=255) {erased=0;break;}
  if(erased) return SETTINGS_EMPTY;
  uint16_t version=get16(b+4);Settings candidate;
  if(memcmp(b,"CLMT",4)) return SETTINGS_INVALID;
  memset(&candidate,0,sizeof(candidate));
  candidate.heater_min_on_seconds=30;candidate.fan_min_on_seconds=60;
  if(version==SETTINGS_VERSION || version==4) {
    unsigned crc_at=version==4?60:68;
    if(get16(b+6)!=(version==4?64:SETTINGS_SIZE) || get32(b+crc_at)!=settings_crc32(b,crc_at) ||
       get16(b+34)!=TREND_WINDOW_SECONDS || get16(b+36)!=AUTO_MIN_RAW16 ||
       (version==4 && (get16(b+38)!=30 || get16(b+40)!=60 || get16(b+58))) ||
       (version==5 && (get16(b+62) || get32(b+64)))) return SETTINGS_INVALID;
    auto_config_read(&candidate.automatic,b+8);candidate.manual_seconds=get16(b+32);
    memcpy(candidate.device_name,b+29,3);memcpy(candidate.sensor_rom,b+42,16);
    if(version==5) {
      candidate.heater_min_on_seconds=get16(b+38);candidate.fan_min_on_seconds=get16(b+40);
      candidate.automatic.rising_milli_per_min=get16(b+58);
      candidate.automatic.rate_hyst_milli_per_min=get16(b+60);
    }
  } else {
    /* Legacy v1-v3: validate the old contract before converting differences
     * to hysteresis. Target/prediction are discarded; no automatic writes. */
    if(version<1 || version>3 || get16(b+6)!=48 || get32(b+44)!=settings_crc32(b,44) ||
       get16(b+32)!=AUTO_MIN_RAW16 || get16(b+34)!=(version==1?30:TREND_WINDOW_SECONDS) ||
       get16(b+36)!=30 || get16(b+38)!=30 || get16(b+40)!=60 ||
       (version<3 && (b[29] || get16(b+42)))) return SETTINGS_INVALID;
    int16_t v[9];for(unsigned i=0;i<9;++i) v[i]=(int16_t)get16(b+8+2*i);
    if(v[2]>v[0] || v[0]>v[6]) return SETTINGS_INVALID;
    const AutoConfig a={v[1],(int16_t)(v[2]-v[1]),v[3],(int16_t)(v[4]-v[3]),
      v[5],(int16_t)(v[5]-v[6]),v[7],(int16_t)(v[7]-v[8]),0,0,0};
    candidate.automatic=a;candidate.manual_seconds=get16(b+30);
    if(version==3) {candidate.device_name[0]=b[29];candidate.device_name[1]=b[42];candidate.device_name[2]=b[43];}
  }
  candidate.fail_count=b[26];candidate.recover_count=b[27];candidate.alternate=b[28];
  if(!valid(&candidate)) return SETTINGS_INVALID;
  *s=candidate;return SETTINGS_OK;
}
SettingsResult settings_load(SettingsBackend io, Settings *s)
{
  uint8_t record[SETTINGS_SIZE];
  if(!io.read || !io.read(record)) return SETTINGS_IO_ERROR;
  return settings_decode(record,s);
}
SettingsResult settings_save(SettingsBackend io, const Settings *s, unsigned *unchanged)
{
  uint8_t wanted[SETTINGS_SIZE], stored[SETTINGS_SIZE];
  *unchanged=0;
  if(!settings_encode(s,wanted)) return SETTINGS_INVALID;
  if(!io.read || !io.write || !io.read(stored)) return SETTINGS_IO_ERROR;
  if(!memcmp(wanted,stored,SETTINGS_SIZE)) {*unchanged=1;return SETTINGS_OK;}
  if(!io.write(wanted) || !io.read(stored) || memcmp(wanted,stored,SETTINGS_SIZE))
    return SETTINGS_IO_ERROR;
  Settings verify;
  return settings_decode(stored,&verify);
}
