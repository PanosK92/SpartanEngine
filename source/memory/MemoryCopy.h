// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace spartan
{
    // Sequential writes to mapped upload memory must not read the destination.
    // In particular, generic memcpy can be very slow on write-combined mappings.
    inline void CopyToMappedMemory(void* destination, const void* source, size_t size)
    {
#if defined(_M_X64) || defined(__SSE2__)
        auto* dst = static_cast<uint8_t*>(destination);
        auto* src = static_cast<const uint8_t*>(source);
        if (size >= 256)
        {
            const size_t prefix = (16 - (reinterpret_cast<uintptr_t>(dst) & 15)) & 15;
            std::memcpy(dst, src, prefix);
            dst += prefix; src += prefix; size -= prefix;
            while (size >= 64)
            {
                const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
                const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 16));
                const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 32));
                const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 48));
                _mm_stream_si128(reinterpret_cast<__m128i*>(dst), a);
                _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 16), b);
                _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 32), c);
                _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 48), d);
                dst += 64; src += 64; size -= 64;
            }
            _mm_sfence(); // Publish streaming stores before submitting the copy.
        }
        std::memcpy(dst, src, size);
#else
        std::memcpy(destination, source, size);
#endif
    }
}
