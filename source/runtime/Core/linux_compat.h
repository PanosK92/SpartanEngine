#pragma once

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstddef>
#include <cwchar>
#include <cstdarg>
#include <cstdlib>  // for getenv, setenv, strdup
#include <cerrno>   // for errno, EINVAL

// Types
typedef int errno_t;

// Macro constants
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

#ifndef STRUNCATE
#define STRUNCATE 80
#endif

// Safe string functions replacements
inline int strncpy_s(char* dest, size_t destsz, const char* src, size_t count) {
    if (!dest || !src || destsz == 0)
        return EINVAL;

    if (count == _TRUNCATE) {
        size_t src_len = strlen(src);
        size_t copy_len = (src_len < (destsz - 1)) ? src_len : (destsz - 1);
        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
        return (copy_len < src_len) ? STRUNCATE : 0;
    }

    size_t src_len = strlen(src);
    size_t requested = (count < src_len) ? count : src_len;

    if (requested >= destsz) {
        memcpy(dest, src, destsz - 1);
        dest[destsz - 1] = '\0';
        return ERANGE;
    }

    memcpy(dest, src, requested);
    dest[requested] = '\0';
    return 0;
}

inline int strncpy_s(char* dest, size_t destsz, const char* src) {
    return strncpy_s(dest, destsz, src, _TRUNCATE);
}

inline int strcpy_s(char* dest, size_t destsz, const char* src) {
    if (!dest || !src || destsz == 0)
        return EINVAL;

    size_t src_len = strlen(src);
    if (src_len >= destsz) {
        memcpy(dest, src, destsz - 1);
        dest[destsz - 1] = '\0';
        return ERANGE;
    }

    memcpy(dest, src, src_len + 1);
    return 0;
}

inline int strncat_s(char* dest, size_t destsz, const char* src, size_t count) {
    if (!dest || !src || destsz == 0)
        return EINVAL;

    size_t dest_len = strlen(dest);
    if (dest_len >= destsz)
        return ERANGE;

    size_t available = destsz - dest_len - 1;
    if (available == 0)
        return ERANGE;

    size_t src_len = strlen(src);
    size_t requested = (count == _TRUNCATE) ? src_len : count;
    size_t src_to_append = (requested < src_len) ? requested : src_len;
    size_t copy_len = (src_to_append < available) ? src_to_append : available;

    memcpy(dest + dest_len, src, copy_len);
    dest[dest_len + copy_len] = '\0';

    if (copy_len < src_to_append)
        return (count == _TRUNCATE) ? STRUNCATE : ERANGE;

    return 0;
}

inline int sprintf_s(char* buffer, size_t sizeOfBuffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int res = vsnprintf(buffer, sizeOfBuffer, format, args);
    va_end(args);
    return res;
}

inline int vsprintf_s(char* buffer, size_t sizeOfBuffer, const char* format, va_list args) {
    return vsnprintf(buffer, sizeOfBuffer, format, args);
}

inline char* strtok_s(char* str, const char* delimiters, char** context) {
    return strtok_r(str, delimiters, context);
}

inline errno_t localtime_s(struct tm* _tm, const time_t* time) {
    return localtime_r(time, _tm) ? 0 : 1;
}

inline errno_t fopen_s(FILE** pFile, const char* filename, const char* mode) {
    if (!pFile || !filename || !mode) {
        if (pFile) *pFile = nullptr;
        return EINVAL;
    }
    *pFile = fopen(filename, mode);
    return (*pFile != nullptr) ? 0 : errno;
}

inline errno_t _dupenv_s(char** buffer, size_t* numberOfElements, const char* varname) {
    if (!buffer || !varname) {
        if (buffer) *buffer = nullptr;
        if (numberOfElements) *numberOfElements = 0;
        return EINVAL;
    }

    const char* env = getenv(varname);
    if (env) {
        *buffer = strdup(env);  // Allocate copy so caller can free()
        if (numberOfElements) *numberOfElements = strlen(env) + 1;
        return 0;
    }

    *buffer = nullptr;
    if (numberOfElements) *numberOfElements = 0;
    return 0;
}

inline errno_t _putenv_s(const char* varname, const char* value_string) {
    return setenv(varname, value_string, 1) ? errno : 0;
}

// Template overloads for fixed-size arrays (MSVC compatibility)
template <size_t N>
inline int strncpy_s(char (&dest)[N], const char* src, size_t count) {
    return strncpy_s(dest, N, src, count);
}

template <size_t N>
inline int strcpy_s(char (&dest)[N], const char* src) {
    return strcpy_s(dest, N, src);
}

template <size_t N>
inline int strncat_s(char (&dest)[N], const char* src, size_t count) {
    return strncat_s(dest, N, src, count);
}

template <size_t N>
inline int sprintf_s(char (&dest)[N], const char* format, ...) {
    va_list args;
    va_start(args, format);
    int res = vsnprintf(dest, N, format, args);
    va_end(args);
    return res;
}
