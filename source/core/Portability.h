/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

// the msvc secure crt functions the engine uses, implemented on top of posix everywhere else
#if !defined(_WIN32)

//= INCLUDES =======
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>
//==================

#ifndef _TRUNCATE
#define _TRUNCATE (static_cast<size_t>(-1))
#endif

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif

using errno_t = int;

inline errno_t strncpy_s(char* destination, size_t destination_size, const char* source, size_t count)
{
    if (!destination || destination_size == 0)
    {
        return EINVAL;
    }

    if (!source)
    {
        destination[0] = '\0';
        return EINVAL;
    }

    size_t length = strnlen(source, count == _TRUNCATE ? destination_size - 1 : count);
    if (length >= destination_size)
    {
        length = destination_size - 1;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
    return 0;
}

template<size_t size>
inline errno_t strncpy_s(char (&destination)[size], const char* source, size_t count)
{
    return strncpy_s(destination, size, source, count);
}

inline errno_t strcpy_s(char* destination, size_t destination_size, const char* source)
{
    return strncpy_s(destination, destination_size, source, _TRUNCATE);
}

template<size_t size>
inline errno_t strcpy_s(char (&destination)[size], const char* source)
{
    return strncpy_s(destination, size, source, _TRUNCATE);
}

inline int sprintf_s(char* buffer, size_t buffer_size, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int result = vsnprintf(buffer, buffer_size, format, arguments);
    va_end(arguments);
    return result;
}

template<size_t size>
inline int sprintf_s(char (&buffer)[size], const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

inline errno_t fopen_s(FILE** file, const char* path, const char* mode)
{
    if (!file)
    {
        return EINVAL;
    }

    *file = fopen(path, mode);
    return *file ? 0 : errno;
}

inline errno_t localtime_s(tm* result, const time_t* time)
{
    return localtime_r(time, result) ? 0 : EINVAL;
}

inline errno_t gmtime_s(tm* result, const time_t* time)
{
    return gmtime_r(time, result) ? 0 : EINVAL;
}

inline int _stricmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

inline int _strnicmp(const char* a, const char* b, size_t count)
{
    return strncasecmp(a, b, count);
}

#endif
