#pragma once
#include <cstdio>
#include <cstdarg>

inline void AFFA3_PRINT(const char *fmt, ...)
{
 return;
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
 
}