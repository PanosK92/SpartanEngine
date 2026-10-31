/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==========
#include "Intrinsics.h"
#include <limits>
//=====================

#if !defined(__AVX2__) && !defined(__aarch64__)
#error "spartan math requires avx2 and fma, build with /arch:AVX2 (msvc) or -mavx2 -mfma (gcc/clang), arm64 maps them to neon"
#endif

namespace spartan::math::simd
{
    // partial loads never touch memory past the last component, unused lanes are zero
    inline __m128 load2(const float* p)
    {
        return _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
    }

    inline void store2(float* p, const __m128 v)
    {
        _mm_storel_epi64(reinterpret_cast<__m128i*>(p), _mm_castps_si128(v));
    }

    inline __m128 load3(const float* p)
    {
        return _mm_insert_ps(load2(p), _mm_load_ss(p + 2), 0x20);
    }

    inline void store3(float* p, const __m128 v)
    {
        store2(p, v);
        _mm_store_ss(p + 2, _mm_movehl_ps(v, v));
    }

    inline __m128 load4(const float* p)           { return _mm_loadu_ps(p); }
    inline void store4(float* p, const __m128 v)  { _mm_storeu_ps(p, v); }

    template <int lane>
    inline __m128 splat(const __m128 v)
    {
        return _mm_permute_ps(v, _MM_SHUFFLE(lane, lane, lane, lane));
    }

    inline __m128 sign_mask() { return _mm_set1_ps(-0.0f); }
    inline __m128 abs(const __m128 v) { return _mm_andnot_ps(sign_mask(), v); }
    inline __m128 negate(const __m128 v) { return _mm_xor_ps(v, sign_mask()); }

    // replaces lanes selected by mask with 1.0, keeps padding lanes out of 0/0 in divisions
    template <int mask>
    inline __m128 pad_one(const __m128 v)
    {
        return _mm_blend_ps(v, _mm_set1_ps(1.0f), mask);
    }

    inline float dot2(const __m128 a, const __m128 b)
    {
        const __m128 m = _mm_mul_ps(a, b);
        return _mm_cvtss_f32(_mm_add_ss(m, _mm_movehdup_ps(m)));
    }

    inline float dot3(const __m128 a, const __m128 b)
    {
        const __m128 m = _mm_mul_ps(a, b);
        return _mm_cvtss_f32(_mm_add_ss(_mm_add_ss(m, _mm_movehdup_ps(m)), _mm_movehl_ps(m, m)));
    }

    inline float dot4(const __m128 a, const __m128 b)
    {
        const __m128 m    = _mm_mul_ps(a, b);
        const __m128 sums = _mm_add_ps(m, _mm_movehdup_ps(m));
        return _mm_cvtss_f32(_mm_add_ss(sums, _mm_movehl_ps(sums, sums)));
    }

    // dot product broadcast to every lane
    inline __m128 dot3_splat(const __m128 a, const __m128 b) { return _mm_dp_ps(a, b, 0x7F); }
    inline __m128 dot4_splat(const __m128 a, const __m128 b) { return _mm_dp_ps(a, b, 0xFF); }

    // w lane of the result is zero when both inputs have zero w
    inline __m128 cross3(const __m128 a, const __m128 b)
    {
        const __m128 a_yzx = _mm_permute_ps(a, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 b_yzx = _mm_permute_ps(b, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 c     = _mm_fmsub_ps(a, b_yzx, _mm_mul_ps(a_yzx, b));
        return _mm_permute_ps(c, _MM_SHUFFLE(3, 0, 2, 1));
    }

    // sine and cosine of four angles at once (radians), minimax polynomials with ~1e-7 absolute error after reduction to [-pi/2, pi/2]
    inline void sin_cos(const __m128 angle, __m128& sin_out, __m128& cos_out)
    {
        const __m128 pi        = _mm_set1_ps(3.14159265359f);
        const __m128 one       = _mm_set1_ps(1.0f);

        // reduce to [-pi, pi]
        const __m128 turns = _mm_round_ps(_mm_mul_ps(angle, _mm_set1_ps(0.159154943092f)), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
        __m128 x           = _mm_fnmadd_ps(turns, _mm_set1_ps(6.28318530718f), angle);

        // reflect into [-pi/2, pi/2], sine is unchanged and cosine flips sign
        const __m128 sign      = _mm_and_ps(x, sign_mask());
        const __m128 reflected = _mm_sub_ps(_mm_or_ps(pi, sign), x);
        const __m128 in_range  = _mm_cmple_ps(_mm_andnot_ps(sign_mask(), x), _mm_set1_ps(1.57079632679f));
        x                      = _mm_blendv_ps(reflected, x, in_range);
        const __m128 cos_sign  = _mm_blendv_ps(_mm_set1_ps(-1.0f), one, in_range);
        const __m128 x2        = _mm_mul_ps(x, x);

        __m128 s = _mm_fmadd_ps(_mm_set1_ps(-2.3889859e-08f), x2, _mm_set1_ps(2.7525562e-06f));
        s        = _mm_fmadd_ps(s, x2, _mm_set1_ps(-0.00019840874f));
        s        = _mm_fmadd_ps(s, x2, _mm_set1_ps(0.0083333310f));
        s        = _mm_fmadd_ps(s, x2, _mm_set1_ps(-0.16666667f));
        s        = _mm_fmadd_ps(s, x2, one);
        sin_out  = _mm_mul_ps(s, x);

        __m128 c = _mm_fmadd_ps(_mm_set1_ps(-2.6051615e-07f), x2, _mm_set1_ps(2.4760495e-05f));
        c        = _mm_fmadd_ps(c, x2, _mm_set1_ps(-0.0013888378f));
        c        = _mm_fmadd_ps(c, x2, _mm_set1_ps(0.041666638f));
        c        = _mm_fmadd_ps(c, x2, _mm_set1_ps(-0.5f));
        c        = _mm_fmadd_ps(c, x2, one);
        cos_out  = _mm_mul_ps(c, cos_sign);
    }

    // 2x2 matrices packed row-major in one register, used by the block-wise 4x4 inverse
    // a * b
    inline __m128 mat2_mul(const __m128 a, const __m128 b)
    {
        return _mm_fmadd_ps(a, _mm_permute_ps(b, _MM_SHUFFLE(3, 0, 3, 0)), _mm_mul_ps(_mm_permute_ps(a, _MM_SHUFFLE(2, 3, 0, 1)), _mm_permute_ps(b, _MM_SHUFFLE(1, 2, 1, 2))));
    }

    // adjugate(a) * b
    inline __m128 mat2_adj_mul(const __m128 a, const __m128 b)
    {
        return _mm_fmsub_ps(_mm_permute_ps(a, _MM_SHUFFLE(0, 0, 3, 3)), b, _mm_mul_ps(_mm_permute_ps(a, _MM_SHUFFLE(2, 2, 1, 1)), _mm_permute_ps(b, _MM_SHUFFLE(1, 0, 3, 2))));
    }

    // a * adjugate(b)
    inline __m128 mat2_mul_adj(const __m128 a, const __m128 b)
    {
        return _mm_fmsub_ps(a, _mm_permute_ps(b, _MM_SHUFFLE(0, 3, 0, 3)), _mm_mul_ps(_mm_permute_ps(a, _MM_SHUFFLE(2, 3, 0, 1)), _mm_permute_ps(b, _MM_SHUFFLE(1, 2, 1, 2))));
    }

    // returns a bit per lane, lane 0 in bit 0
    inline int equal_mask(const __m128 a, const __m128 b)   { return _mm_movemask_ps(_mm_cmpeq_ps(a, b)); }
    inline int nan_mask(const __m128 v)                     { return _mm_movemask_ps(_mm_cmpunord_ps(v, v)); }
    inline int finite_mask(const __m128 v)                  { return _mm_movemask_ps(_mm_cmplt_ps(abs(v), _mm_set1_ps(std::numeric_limits<float>::infinity()))); }
}
