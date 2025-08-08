#include "DEV.h"
#include <stdio.h>

const bool DEV_MODE = false;

void Log(const char *format, ...) {
  if (DEV_MODE) {
    va_list args;
    va_start(args, format);
    vprintf_s(format, args);
    va_end(args);
  }
}