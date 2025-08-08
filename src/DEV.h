#ifndef DEV_H
#define DEV_H

#include <stdarg.h>
#include <stdbool.h>

extern const bool DEV_MODE;
void Log(const char *format, ...);

#endif
