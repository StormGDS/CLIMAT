#ifndef TINY_FORMAT_H
#define TINY_FORMAT_H
#include <stddef.h>
/* Integer/string subset used by the production console; no libc printf. */
int tiny_snprintf(char *buffer, size_t capacity, const char *format, ...)
  __attribute__((format(printf,3,4)));
#endif
