// Basit dosya logu: DLL'in yanindaki utcheat.log'a zaman damgali satirlar yazar.
#pragma once
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <share.h>

namespace ulog {

inline FILE*& file() { static FILE* f = nullptr; return f; }

inline void open(HMODULE self) {
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    char* dot = strrchr(path, '.');
    if (dot) strcpy_s(dot, path + MAX_PATH - dot, ".log");
    file() = _fsopen(path, "w", _SH_DENYWR);
}

inline void write(const char* fmt, ...) {
    FILE* f = file();
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f);
    fflush(f);
}

} // namespace ulog
