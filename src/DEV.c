#include "DEV.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <time.h>
#elif defined(__APPLE__)
#include <sys/time.h>
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

float GetCurrentTimeInSeconds() {
#if defined(_WIN32) || defined(_WIN64)
    // Windows 平台
    static LARGE_INTEGER frequency;
    static int frequency_initialized = 0;
    if (!frequency_initialized) {
        QueryPerformanceFrequency(&frequency);
        frequency_initialized = 1;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (float)now.QuadPart / frequency.QuadPart;
#elif defined(__APPLE__)
    // macOS/iOS 平台
    struct timeval now;
    gettimeofday(&now, NULL);
    return (float)now.tv_sec + (float)now.tv_usec / 1000000.0f;
#else
    // Linux 和其他 POSIX 平台
    struct timespec now;
    // 使用 CLOCK_MONOTONIC 来获取单调时间，更适合事件计时
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (float)now.tv_sec + (float)now.tv_nsec / 1000000000.0f;
#endif
}