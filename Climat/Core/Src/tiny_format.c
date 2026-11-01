#include "tiny_format.h"
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
static void put(char *b, size_t size, unsigned *used, char c)
{if(*used+1<size) b[*used]=c;++*used;}
int tiny_snprintf(char *b, size_t size, const char *fmt, ...)
{
  unsigned used=0;va_list args;va_start(args,fmt);
  while(*fmt) {
    if(*fmt!='%') {put(b,size,&used,*fmt++);continue;}
    ++fmt;unsigned width=0,zero=0,is_long=0;
    if(*fmt=='0') {zero=1;++fmt;}
    while(*fmt>='0' && *fmt<='9') width=width*10+(unsigned)(*fmt++-'0');
    if(*fmt=='l') {is_long=1;++fmt;}
    char type=*fmt;if(type) ++fmt;
    if(type=='s') {
      const char *s=va_arg(args,const char *);if(!s) s="(null)";
      while(*s) put(b,size,&used,*s++);
    } else if(type=='c') put(b,size,&used,(char)va_arg(args,int));
    else if(type=='%') put(b,size,&used,'%');
    else if(type=='u' || type=='d' || type=='X' || type=='x') {
      uint32_t n;unsigned negative=0;
      if(type=='d') {
        int32_t signed_n=is_long?(int32_t)va_arg(args,long):(int32_t)va_arg(args,int);
        negative=signed_n<0;n=negative?0U-(uint32_t)signed_n:(uint32_t)signed_n;
      } else n=is_long?(uint32_t)va_arg(args,unsigned long):(uint32_t)va_arg(args,unsigned);
      unsigned base=type=='X'||type=='x'?16:10,count=0;char digits[10];
      const char *alphabet=type=='x'?"0123456789abcdef":"0123456789ABCDEF";
      do {digits[count++]=alphabet[n%base];n/=base;} while(n);
      if(!zero) while(width>count+negative) {put(b,size,&used,' ');--width;}
      if(negative) put(b,size,&used,'-');
      if(zero) while(width>count+negative) {put(b,size,&used,'0');--width;}
      while(count) put(b,size,&used,digits[--count]);
    } else {put(b,size,&used,'?');}
  }
  va_end(args);if(size) b[used<size?used:size-1]=0;return (int)used;
}
