/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

// the engine speaks sse/avx2, arm64 translates it to neon through sse2neon
// sse2neon stops at sse4.2, so the few avx and fma intrinsics the engine uses are mapped here
#if defined(__aarch64__) || defined(_M_ARM64)

//= INCLUDES ======
#include <sse2neon.h>
//=================

#define _CMP_EQ_OQ   0x00
#define _CMP_LT_OQ   0x11
#define _CMP_UNORD_Q 0x03

#define _mm_permute_ps(a, imm) _mm_shuffle_ps((a), (a), (imm))

inline __m128 _mm_broadcast_ss(const float* address)
{
    return vdupq_n_f32(*address);
}

// a * b + c
inline __m128 _mm_fmadd_ps(const __m128 a, const __m128 b, const __m128 c)
{
    return vfmaq_f32(c, a, b);
}

// a * b - c
inline __m128 _mm_fmsub_ps(const __m128 a, const __m128 b, const __m128 c)
{
    return vnegq_f32(vfmsq_f32(c, a, b));
}

// c - a * b
inline __m128 _mm_fnmadd_ps(const __m128 a, const __m128 b, const __m128 c)
{
    return vfmsq_f32(c, a, b);
}

// 256 bit vectors are two 128 bit halves
struct __m256
{
    __m128 lo;
    __m128 hi;
};

inline __m256 _mm256_setzero_ps()
{
    return { _mm_setzero_ps(), _mm_setzero_ps() };
}

inline __m256 _mm256_set1_ps(const float value)
{
    return { _mm_set1_ps(value), _mm_set1_ps(value) };
}

inline __m256 _mm256_setr_ps(float a, float b, float c, float d, float e, float f, float g, float h)
{
    return { _mm_setr_ps(a, b, c, d), _mm_setr_ps(e, f, g, h) };
}

inline __m256 _mm256_loadu_ps(const float* address)
{
    return { _mm_loadu_ps(address), _mm_loadu_ps(address + 4) };
}

inline __m256 _mm256_add_ps(const __m256 a, const __m256 b)
{
    return { _mm_add_ps(a.lo, b.lo), _mm_add_ps(a.hi, b.hi) };
}

inline __m256 _mm256_mul_ps(const __m256 a, const __m256 b)
{
    return { _mm_mul_ps(a.lo, b.lo), _mm_mul_ps(a.hi, b.hi) };
}

inline __m256 _mm256_div_ps(const __m256 a, const __m256 b)
{
    return { _mm_div_ps(a.lo, b.lo), _mm_div_ps(a.hi, b.hi) };
}

inline __m256 _mm256_and_ps(const __m256 a, const __m256 b)
{
    return { _mm_and_ps(a.lo, b.lo), _mm_and_ps(a.hi, b.hi) };
}

inline __m256 _mm256_or_ps(const __m256 a, const __m256 b)
{
    return { _mm_or_ps(a.lo, b.lo), _mm_or_ps(a.hi, b.hi) };
}

inline __m256 _mm256_andnot_ps(const __m256 a, const __m256 b)
{
    return { _mm_andnot_ps(a.lo, b.lo), _mm_andnot_ps(a.hi, b.hi) };
}

inline __m256 _mm256_fmadd_ps(const __m256 a, const __m256 b, const __m256 c)
{
    return { _mm_fmadd_ps(a.lo, b.lo, c.lo), _mm_fmadd_ps(a.hi, b.hi, c.hi) };
}

inline __m128 _mm256_castps256_ps128(const __m256 a)
{
    return a.lo;
}

inline __m128 _mm256_extractf128_ps(const __m256 a, const int index)
{
    return index ? a.hi : a.lo;
}

inline int _mm256_movemask_ps(const __m256 a)
{
    return _mm_movemask_ps(a.lo) | (_mm_movemask_ps(a.hi) << 4);
}

inline __m128 cmp_ps_128(const __m128 a, const __m128 b, const int predicate)
{
    switch (predicate)
    {
        case _CMP_EQ_OQ:   return _mm_cmpeq_ps(a, b);
        case _CMP_LT_OQ:   return _mm_cmplt_ps(a, b);
        case _CMP_UNORD_Q: return _mm_cmpunord_ps(a, b);
    }
    return _mm_setzero_ps();
}

inline __m256 _mm256_cmp_ps(const __m256 a, const __m256 b, const int predicate)
{
    return { cmp_ps_128(a.lo, b.lo, predicate), cmp_ps_128(a.hi, b.hi, predicate) };
}

#else

//= INCLUDES =======
#include <immintrin.h>
//==================

#endif
