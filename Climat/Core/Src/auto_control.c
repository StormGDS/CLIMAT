#include "auto_control.h"
#include "thermal_timing.h"
#include <string.h>
#include <stddef.h>
_Static_assert(sizeof(AutoConfig)==22 && offsetof(AutoConfig,cooling_milli_per_min)==16,"config layout");
void auto_config_write(const AutoConfig *a, uint8_t b[18])
{
  uint16_t v[9];memcpy(v,a,18);
  for(unsigned i=0;i<9;++i) {b[2*i]=(uint8_t)v[i];b[2*i+1]=(uint8_t)(v[i]>>8);}
}
void auto_config_read(AutoConfig *a, const uint8_t b[18])
{
  uint16_t v[9];
  for(unsigned i=0;i<9;++i) v[i]=(uint16_t)(b[2*i]|((uint16_t)b[2*i+1]<<8));
  memcpy(a,v,18);
}
void auto_reset(AutoControl *a)
{
  a->heat=0; a->cool=0; a->cycle=0; a->tracking=0; a->rate_ready=0;
  a->heat2_temperature=0; a->rising_blocked=0; a->cooling_early=0; a->effective_milli=0;
}
int auto_configure(AutoControl *a, const AutoConfig *p)
{
  /* Widen before adding/subtracting, including malicious int16 inputs. */
  int32_t h1=(int32_t)p->heat1_on+p->heat1_hyst;
  int32_t h2=(int32_t)p->heat2_on+p->heat2_hyst;
  int32_t c1=(int32_t)p->cool1_on-p->cool1_hyst;
  int32_t c2=(int32_t)p->cool2_on-p->cool2_hyst;
  if(p->heat1_hyst<=0 || p->heat2_hyst<=0 || p->cool1_hyst<=0 || p->cool2_hyst<=0 ||
     p->heat2_on<AUTO_MIN_RAW16 || p->heat2_on>p->heat1_on || h2>h1 ||
     p->cool1_on>p->cool2_on || c1>c2 || h1>=c1 || p->cool2_on>2000 ||
     (p->rising_milli_per_min && p->rate_hyst_milli_per_min>p->rising_milli_per_min) ||
     (p->cooling_milli_per_min && p->rate_hyst_milli_per_min>p->cooling_milli_per_min)) return 0;
  a->config=*p; a->configured=1; auto_reset(a); return 1;
}
static uint8_t heat_stage(unsigned was, int16_t t, int16_t on, int16_t hyst)
{return (uint8_t)(was ? (int32_t)t<(int32_t)on+hyst : t<=on);}
static uint8_t cool_stage(unsigned was, int16_t t, int16_t on, int16_t hyst)
{return (uint8_t)(was ? (int32_t)t>(int32_t)on-hyst : t>on);}
static uint8_t rate_latch(uint8_t was, unsigned enabled, int32_t rate, int32_t on, uint16_t hyst)
{
  if(!enabled) return 0;
  if(rate>=on) return 1;
  return rate<=on-hyst ? 0 : was;
}
uint8_t auto_sample(AutoControl *a, unsigned source, unsigned valid,
                   int16_t raw16, unsigned trend_valid, int32_t rate_milli,
                   uint8_t actual, uint32_t now)
{
  if(!a->configured || !valid || source>1 || raw16 < -880 || raw16>2000) {
    auto_reset(a);return 0;
  }
  if(!a->tracking || a->source!=source) {
    auto_reset(a);a->tracking=1;a->source=(uint8_t)source;a->source_at=now;
  }
  a->rate_ready=(uint8_t)(trend_valid && (uint32_t)(now-a->source_at)>=TREND_WINDOW_SECONDS*1000U);
  a->effective_milli=(int32_t)raw16*1000/16;
  const AutoConfig *p=&a->config;
  uint8_t h1=heat_stage(a->heat&1,raw16,p->heat1_on,p->heat1_hyst);
  a->heat2_temperature=heat_stage(a->heat2_temperature,raw16,p->heat2_on,p->heat2_hyst);
  /* Inclusive assertion wins at coincident H=0 boundaries: no toggling.
   * No queued OFF: the arbiter consumes the current demand after MIN_ON. */
  a->rising_blocked=rate_latch(a->rising_blocked,a->rate_ready && p->rising_milli_per_min,
      rate_milli,p->rising_milli_per_min,p->rate_hyst_milli_per_min);
  /* -1-d reverses order without overflowing at INT32_MIN. The matching
   * positive threshold is S-1; differences and inclusive boundaries survive. */
  a->cooling_early=rate_latch(a->cooling_early,a->rate_ready && p->cooling_milli_per_min,
      -1-rate_milli,(int32_t)p->cooling_milli_per_min-1,p->rate_hyst_milli_per_min);
  /* Early demand releases when the slope slows; temperature-latched demand
   * remains until on+hysteresis. Actual K1, not pending demand, gates K2. */
  uint8_t h2=(uint8_t)(h1 && (actual&1U) && !a->rising_blocked && (a->heat2_temperature || a->cooling_early));
  a->heat=(uint8_t)(h1 | (h2<<1));
  a->cool=(uint8_t)(cool_stage(a->cool&1,raw16,p->cool1_on,p->cool1_hyst) |
                  (cool_stage(a->cool&2,raw16,p->cool2_on,p->cool2_hyst)<<1));
  if(a->cool) {a->heat=0;a->heat2_temperature=0;}
  if(a->cycle && !a->cool && !(actual&12)) {
    if(a->alternate) a->lead^=1;
    a->cycle=0;
  }
  if(a->cool && (actual&12)) a->cycle=1;
  uint8_t fans=a->cool==3 ? 12 : a->cool ? (uint8_t)(4U<<a->lead) : 0;
  return a->heat | fans;
}
