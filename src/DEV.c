#include "DEV.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <unistd.h>
#endif

const bool DEV_MODE = false;

void Log(const char *format, ...) {
  if (DEV_MODE) {
    va_list args;
    va_start(args, format);
#ifdef _WIN32
    vprintf_s(format, args);
#else
    vfprintf(stdout, format, args);
#endif
    va_end(args);
  }
}

void VSleep(int s) {
#ifdef _WIN32
  Sleep(s * 1000);
#else
  sleep(s);
#endif
}