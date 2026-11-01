#include "tiny_format_selftest.h"
#include "tiny_format.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
uint32_t tiny_format_selftest(void)
{
  uint32_t errors=0;
  const int32_t values[]={INT32_MIN,-10001,-1,0,1,10001,INT32_MAX};
  for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i) {
    for(unsigned capacity=0;capacity<=96;++capacity) {
      char actual[97],reference[97];memset(actual,0x55,sizeof(actual));memset(reference,0x55,sizeof(reference));
      int a=tiny_snprintf(actual,capacity,"[%ld|%lu|%08lX|%02x|%d|%05d|%6u|%s|%c|%%]",
        (long)values[i],(unsigned long)(uint32_t)values[i],(unsigned long)(uint32_t)values[i],
        10U,(int)values[i],-17,99U,"ROM",'Q');
      int r=snprintf(reference,capacity,"[%ld|%lu|%08lX|%02x|%d|%05d|%6u|%s|%c|%%]",
        (long)values[i],(unsigned long)(uint32_t)values[i],(unsigned long)(uint32_t)values[i],
        10U,(int)values[i],-17,99U,"ROM",'Q');
      if(a!=r || memcmp(actual,reference,sizeof(actual))) errors|=1;
    }
  }
  return errors;
}
