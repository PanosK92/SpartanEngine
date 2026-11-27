/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include "OceanWake.h"
#include "components/Terrain.h"
#include "../commands/console/ConsoleCommands.h"
#include "../core/ThreadPool.h"
#include "../rhi/RHI_Vertex.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#if defined(_M_X64) || (defined(__x86_64__) && defined(__AVX2__) && defined(__FMA__) && defined(__F16C__))
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#define SP_OCEAN_WAKE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#define SP_OCEAN_WAKE_NOINLINE __attribute__((noinline))
#else
#define SP_OCEAN_WAKE_NOINLINE
#endif
#define SP_OCEAN_WAKE_X64_SIMD 1
#endif
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        TConsoleVar<int32_t> cvar_wake("ocean.wake", 1, "interactive water, bodies in the sea push waves around, 0 off, 1 on");
        TConsoleVar<float> cvar_strength("ocean.wake_strength", 1.0f, "scales interactive wave forcing from moving and plunging bodies");
        TConsoleVar<float> cvar_damping("ocean.wake_damping", 0.065f, "how fast interactive waves calm down, per second");
        TConsoleVar<float> cvar_depth("ocean.wake_depth", 1.8f, "effective gravity-wave depth in metres, controls wave speed sqrt(g * depth) and kelvin wake angle");

        constexpr int32_t res            = static_cast<int32_t>(OceanWake::resolution);
        constexpr int32_t mask           = res - 1;
        constexpr int32_t tile_shift     = 4;
        constexpr int32_t tile_size      = 1 << tile_shift; // 16x16 cells per sparse simulation tile
        constexpr int32_t tile_res       = res >> tile_shift; // 32x32 tile grid
        constexpr int32_t tile_res_shift = 5;
        constexpr int32_t tile_mask      = tile_res - 1;
        constexpr int32_t tile_count     = tile_res * tile_res;
        static_assert((1 << tile_res_shift) == tile_res, "tile_res_shift must match log2(tile_res)");
        constexpr float cell_size        = OceanWake::cell_size;
        constexpr float inv_cell         = 1.0f / cell_size;
        constexpr float step_dt          = 1.0f / 45.0f; // CFL = sqrt(g*1.8) * (1/45) / 0.25 = 0.373 (1.9x below 0.707 2D limit)
        constexpr uint32_t steps_max     = 3;
        constexpr float gravity          = 9.81f;
        constexpr float inv_gravity      = 1.0f / gravity;
        constexpr float depth_dry        = 0.03f;  // m, shallower cells are beach that reflects waves
        constexpr int32_t sponge_cells   = 32;     // 8 m absorbing border around the 128 m window
        constexpr float sponge_rate      = 5.5f;   // per second at the outer rim
        constexpr float level_rate       = 0.020f; // per second, gentle relaxation toward sea level
        constexpr float dispersion_beta  = 0.10f;  // Boussinesq elliptic dispersion (strictly positive 1 - 8*beta > 0 across entire 2D spectrum)
        constexpr float visc_flux        = 0.0030f;// light viscous shear damping (keeps 1-2 m ripples & Kelvin arms razor-crisp without checkerboard)
        constexpr float visc_height      = 0.0008f;// minimal height smoothing for smooth crests without flattening ripples
        constexpr float foam_diff        = 0.033f; // lateral turbulent diffusion of surface foam
        constexpr float bubble_diff      = 0.060f; // wider turbulent spreading of subsurface bubble wake
        constexpr float foam_life        = 4.0f;   // e-folding lifetime of fresh surface froth (seconds)
        constexpr float bubble_life      = 11.0f;  // e-folding lifetime of lingering turquoise aerated bubble wake (seconds)
        constexpr float turb_life        = 4.8f;   // e-folding lifetime of wake micro-chop agitation (seconds)
        constexpr float height_soft_knee = 0.52f;  // m, linear below this, smoothly saturates above
        constexpr float height_max       = 0.95f;  // m, hard physical ceiling for interactive displacement
        constexpr float tile_sleep_eps   = 1.2e-3f;// energy threshold below which a tile is zeroed and put to sleep
        constexpr uint32_t stale_ticks   = 20;     // frames without a report before a body is removed
        constexpr float pi_val           = 3.14159265359f;
        constexpr float two_pi           = 6.28318530718f;
        static_assert((OceanWake::resolution & (OceanWake::resolution - 1)) == 0, "the toroidal wrap masks, the resolution must be a power of two");

        inline void add_asymptotic(float& target, float amount)
        {
            if (amount <= 0.0f)
            {
                return;
            }
            const float t = min(amount, 0.85f);
            target = min(target + (1.0f - target) * t, 1.0f);
        }

#if defined(SP_OCEAN_WAKE_X64_SIMD)
        bool detect_avx2_f16c()
        {
#if defined(_MSC_VER)
            int info[4] = {};
            __cpuid(info, 0);
            if (info[0] < 7)
            {
                return false;
            }
            __cpuid(info, 1);
            const bool has_osxsave = (info[2] & (1 << 27)) != 0;
            const bool has_avx     = (info[2] & (1 << 28)) != 0;
            const bool has_f16c    = (info[2] & (1 << 29)) != 0;
            const bool has_fma     = (info[2] & (1 << 12)) != 0;
            if (!has_osxsave || !has_avx || !has_f16c || !has_fma)
            {
                return false;
            }
            const uint64_t xcr0 = _xgetbv(0);
            if ((xcr0 & 0x6u) != 0x6u)
            {
                return false;
            }
            __cpuidex(info, 7, 0);
            return (info[1] & (1 << 5)) != 0; // AVX2
#elif defined(__GNUC__) || defined(__clang__)
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx) || eax < 7)
            {
                return false;
            }
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
            {
                return false;
            }
            const bool has_osxsave = (ecx & (1u << 27)) != 0;
            const bool has_avx     = (ecx & (1u << 28)) != 0;
            const bool has_f16c    = (ecx & (1u << 29)) != 0;
            const bool has_fma     = (ecx & (1u << 12)) != 0;
            if (!has_osxsave || !has_avx || !has_f16c || !has_fma)
            {
                return false;
            }
            uint32_t xcr0_lo = 0, xcr0_hi = 0;
            __asm__ volatile("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
            if ((xcr0_lo & 0x6u) != 0x6u)
            {
                return false;
            }
            if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
            {
                return false;
            }
            return (ebx & (1u << 5)) != 0; // AVX2
#else
            return false;
#endif
        }

        const bool g_has_avx2_f16c = detect_avx2_f16c();

        SP_OCEAN_WAKE_NOINLINE void avx2_pass1_tile(const float* __restrict height_in, float* __restrict eta_buf, int32_t x0, int32_t z0)
        {
            const __m256 v_beta = _mm256_set1_ps(dispersion_beta);
            const __m256 v_m4   = _mm256_set1_ps(-4.0f);
            for (int32_t z = z0; z < z0 + tile_size; z++)
            {
                const int32_t base = z * res + x0;
                for (int32_t ox = 0; ox < tile_size; ox += 8)
                {
                    const int32_t i = base + ox;
                    const __m256 hc = _mm256_loadu_ps(height_in + i);
                    const __m256 hl = _mm256_loadu_ps(height_in + i - 1);
                    const __m256 hr = _mm256_loadu_ps(height_in + i + 1);
                    const __m256 hd = _mm256_loadu_ps(height_in + i - res);
                    const __m256 hu = _mm256_loadu_ps(height_in + i + res);
                    const __m256 sum = _mm256_add_ps(_mm256_add_ps(hl, hr), _mm256_add_ps(hd, hu));
                    const __m256 lap = _mm256_fmadd_ps(v_m4, hc, sum);
                    _mm256_storeu_ps(eta_buf + i, _mm256_fmadd_ps(v_beta, lap, hc));
                }
            }
        }

        SP_OCEAN_WAKE_NOINLINE void avx2_pass2_tile(
            const float* __restrict eta_buf,
            const float* __restrict flux_x_buf,
            const float* __restrict flux_z_buf,
            const float* __restrict speed2_in,
            const float* __restrict sponge_x,
            const float* __restrict sponge_z,
            float* __restrict flux_x_out,
            float* __restrict flux_z_out,
            float damping,
            float dt_dx,
            int32_t x0,
            int32_t z0)
        {
            const __m256 v_damp  = _mm256_set1_ps(damping);
            const __m256 v_dt    = _mm256_set1_ps(step_dt);
            const __m256 v_one   = _mm256_set1_ps(1.0f);
            const __m256 v_zero  = _mm256_setzero_ps();
            const __m256 v_visc  = _mm256_set1_ps(visc_flux);
            const __m256 v_m4    = _mm256_set1_ps(-4.0f);
            const __m256 v_dtdx  = _mm256_set1_ps(dt_dx);

            const __m256 spx0 = _mm256_loadu_ps(sponge_x + x0);
            const __m256 spx1 = _mm256_loadu_ps(sponge_x + x0 + 8);

            for (int32_t z = z0; z < z0 + tile_size; z++)
            {
                const int32_t base = z * res + x0;
                const __m256 spz   = _mm256_set1_ps(sponge_z[z]);
                const __m256 spx_v[2] = { spx0, spx1 };

                for (int32_t k = 0; k < 2; k++)
                {
                    const int32_t i   = base + k * 8;
                    const __m256 rate = _mm256_add_ps(v_damp, _mm256_max_ps(spx_v[k], spz));
                    const __m256 ret  = _mm256_max_ps(_mm256_fnmadd_ps(rate, v_dt, v_one), v_zero);
                    const __m256 si   = _mm256_loadu_ps(speed2_in + i);
                    const __m256 etai = _mm256_loadu_ps(eta_buf + i);

                    const __m256 east = _mm256_min_ps(si, _mm256_loadu_ps(speed2_in + i + 1));
                    const __m256 qx   = _mm256_loadu_ps(flux_x_buf + i);
                    const __m256 sx_n = _mm256_add_ps(_mm256_add_ps(_mm256_loadu_ps(flux_x_buf + i - 1), _mm256_loadu_ps(flux_x_buf + i + 1)),
                                                      _mm256_add_ps(_mm256_loadu_ps(flux_x_buf + i - res), _mm256_loadu_ps(flux_x_buf + i + res)));
                    const __m256 lapx = _mm256_fmadd_ps(v_m4, qx, sx_n);
                    const __m256 gx   = _mm256_sub_ps(_mm256_loadu_ps(eta_buf + i + 1), etai);
                    const __m256 nx   = _mm256_fnmadd_ps(_mm256_mul_ps(east, v_dtdx), gx, _mm256_fmadd_ps(v_visc, lapx, qx));
                    _mm256_storeu_ps(flux_x_out + i, _mm256_mul_ps(ret, nx));

                    const __m256 top  = _mm256_min_ps(si, _mm256_loadu_ps(speed2_in + i + res));
                    const __m256 qz   = _mm256_loadu_ps(flux_z_buf + i);
                    const __m256 sz_n = _mm256_add_ps(_mm256_add_ps(_mm256_loadu_ps(flux_z_buf + i - 1), _mm256_loadu_ps(flux_z_buf + i + 1)),
                                                      _mm256_add_ps(_mm256_loadu_ps(flux_z_buf + i - res), _mm256_loadu_ps(flux_z_buf + i + res)));
                    const __m256 lapz = _mm256_fmadd_ps(v_m4, qz, sz_n);
                    const __m256 gz   = _mm256_sub_ps(_mm256_loadu_ps(eta_buf + i + res), etai);
                    const __m256 nz   = _mm256_fnmadd_ps(_mm256_mul_ps(top, v_dtdx), gz, _mm256_fmadd_ps(v_visc, lapz, qz));
                    _mm256_storeu_ps(flux_z_out + i, _mm256_mul_ps(ret, nz));
                }
            }
        }

        inline __m256 avx2_soft_limit_height(__m256 h)
        {
            const __m256 sign_mask = _mm256_set1_ps(-0.0f);
            const __m256 a         = _mm256_andnot_ps(sign_mask, h);
            const __m256 knee      = _mm256_set1_ps(height_soft_knee);
            const __m256 mask_gt   = _mm256_cmp_ps(a, knee, _CMP_GT_OQ);
            if (_mm256_movemask_ps(mask_gt) == 0)
            {
                return h;
            }
            const __m256 span   = _mm256_set1_ps(height_max - height_soft_knee);
            const __m256 excess = _mm256_sub_ps(a, knee);
            const __m256 capped = _mm256_add_ps(knee, _mm256_mul_ps(span, _mm256_div_ps(excess, _mm256_add_ps(excess, span))));
            const __m256 signed_capped = _mm256_or_ps(capped, _mm256_and_ps(h, sign_mask));
            return _mm256_blendv_ps(h, signed_capped, mask_gt);
        }

        inline float hmax_avx2(__m256 v)
        {
            const __m128 lo = _mm256_castps256_ps128(v);
            const __m128 hi = _mm256_extractf128_ps(v, 1);
            __m128 m        = _mm_max_ps(lo, hi);
            m               = _mm_max_ps(m, _mm_movehl_ps(m, m));
            m               = _mm_max_ss(m, _mm_shuffle_ps(m, m, 1));
            return _mm_cvtss_f32(m);
        }

        SP_OCEAN_WAKE_NOINLINE void avx2_pass3_wave_only_tile(
            const float* __restrict height_in,
            const float* __restrict flux_x_buf,
            const float* __restrict flux_z_buf,
            const float* __restrict sponge_x,
            const float* __restrict sponge_z,
            float* __restrict height_out,
            float* __restrict foam_out,
            float* __restrict bub_out,
            float* __restrict turb_out,
            float dt_dx,
            int32_t x0,
            int32_t z0,
            float& out_peak,
            float& out_energy,
            float& out_aer_peak)
        {
            const __m256 v_dtdx   = _mm256_set1_ps(dt_dx);
            const __m256 v_visc   = _mm256_set1_ps(visc_height);
            const __m256 v_m4     = _mm256_set1_ps(-4.0f);
            const __m256 v_lvl    = _mm256_set1_ps(level_rate);
            const __m256 v_spmul  = _mm256_set1_ps(0.65f);
            const __m256 v_dt     = _mm256_set1_ps(step_dt);
            const __m256 v_one    = _mm256_set1_ps(1.0f);
            const __m256 v_zero   = _mm256_setzero_ps();
            const __m256 v_abs    = _mm256_set1_ps(-0.0f);
            const __m256 v_hgate  = _mm256_set1_ps(0.14f);

            const __m256 spx0     = _mm256_loadu_ps(sponge_x + x0);
            const __m256 spx1     = _mm256_loadu_ps(sponge_x + x0 + 8);
            __m256 v_peak         = v_zero;
            __m256 v_energy       = v_zero;
            float aer_peak        = 0.0f;

            for (int32_t z = z0; z < z0 + tile_size; z++)
            {
                const int32_t base    = z * res + x0;
                const __m256 spz      = _mm256_set1_ps(sponge_z[z]);
                const __m256 spx_v[2] = { spx0, spx1 };

                for (int32_t k = 0; k < 2; k++)
                {
                    const int32_t i  = base + k * 8;
                    const __m256 hc  = _mm256_loadu_ps(height_in + i);
                    const __m256 hl  = _mm256_loadu_ps(height_in + i - 1);
                    const __m256 hr  = _mm256_loadu_ps(height_in + i + 1);
                    const __m256 hd  = _mm256_loadu_ps(height_in + i - res);
                    const __m256 hu  = _mm256_loadu_ps(height_in + i + res);

                    const __m256 fxl = _mm256_loadu_ps(flux_x_buf + i - 1);
                    const __m256 fxi = _mm256_loadu_ps(flux_x_buf + i);
                    const __m256 fzd = _mm256_loadu_ps(flux_z_buf + i - res);
                    const __m256 fzi = _mm256_loadu_ps(flux_z_buf + i);

                    const __m256 rise   = _mm256_mul_ps(_mm256_add_ps(_mm256_sub_ps(fxl, fxi), _mm256_sub_ps(fzd, fzi)), v_dtdx);
                    const __m256 lap_h  = _mm256_fmadd_ps(v_m4, hc, _mm256_add_ps(_mm256_add_ps(hl, hr), _mm256_add_ps(hd, hu)));
                    const __m256 sp     = _mm256_max_ps(spx_v[k], spz);
                    const __m256 settle = _mm256_fmadd_ps(sp, v_spmul, v_lvl);
                    const __m256 retain = _mm256_max_ps(_mm256_fnmadd_ps(settle, v_dt, v_one), v_zero);
                    __m256 h_new        = _mm256_mul_ps(_mm256_fmadd_ps(v_visc, lap_h, _mm256_add_ps(hc, rise)), retain);

                    const __m256 brk_mask = _mm256_and_ps(_mm256_cmp_ps(h_new, v_hgate, _CMP_GT_OQ), _mm256_cmp_ps(rise, v_zero, _CMP_GT_OQ));
                    if (_mm256_movemask_ps(brk_mask) != 0)
                    {
                        alignas(32) float h_arr[8], r_arr[8], hl_arr[8], hr_arr[8], hd_arr[8], hu_arr[8];
                        _mm256_store_ps(h_arr,  h_new);
                        _mm256_store_ps(r_arr,  rise);
                        _mm256_store_ps(hl_arr, hl);
                        _mm256_store_ps(hr_arr, hr);
                        _mm256_store_ps(hd_arr, hd);
                        _mm256_store_ps(hu_arr, hu);
                        for (int32_t lane = 0; lane < 8; lane++)
                        {
                            if (h_arr[lane] > 0.14f && r_arr[lane] > 0.0f)
                            {
                                const float sx = (hr_arr[lane] - hl_arr[lane]) * (0.5f * inv_cell);
                                const float sz = (hu_arr[lane] - hd_arr[lane]) * (0.5f * inv_cell);
                                const float s2 = sx * sx + sz * sz;
                                if (s2 > 0.16f)
                                {
                                    const float slope = sqrtf(s2);
                                    const float steep = clamp((slope - 0.40f) * 2.2f, 0.0f, 1.0f);
                                    const float crest = clamp((h_arr[lane] - 0.14f) / 0.18f, 0.0f, 1.0f);
                                    h_arr[lane]      *= (1.0f - 0.07f * steep);
                                    const float entr  = steep * crest * 0.85f * step_dt;
                                    const float fn    = min(entr, 0.85f);
                                    const float bn    = min(entr * 0.85f, 0.85f);
                                    const float tn    = min(entr * 0.90f, 0.85f);
                                    foam_out[i + lane] = fn;
                                    bub_out[i + lane]  = bn;
                                    turb_out[i + lane] = tn;
                                    aer_peak           = max(aer_peak, max(fn, max(bn, tn)));
                                }
                            }
                        }
                        h_new = _mm256_load_ps(h_arr);
                    }

                    h_new = avx2_soft_limit_height(h_new);
                    _mm256_storeu_ps(height_out + i, h_new);

                    const __m256 ah  = _mm256_andnot_ps(v_abs, h_new);
                    const __m256 afx = _mm256_andnot_ps(v_abs, fxi);
                    const __m256 afz = _mm256_andnot_ps(v_abs, fzi);
                    v_peak           = _mm256_max_ps(v_peak, ah);
                    v_energy         = _mm256_max_ps(v_energy, _mm256_max_ps(ah, _mm256_max_ps(afx, afz)));
                }
            }

            out_peak     = max(hmax_avx2(v_peak), aer_peak);
            out_energy   = max(hmax_avx2(v_energy), aer_peak);
            out_aer_peak = aer_peak;
        }

        inline __m256 avx2_advect_diffuse(const float* __restrict buf, int32_t i, __m256 stay, __m256 w_adv, __m256 west_in, __m256 east_in, __m256 south_in, __m256 north_in, __m256 diff, __m256 decay, __m256 v_m4, __m256 v_zero, __m256 v_one)
        {
            const __m256 c  = _mm256_loadu_ps(buf + i);
            const __m256 l  = _mm256_loadu_ps(buf + i - 1);
            const __m256 r  = _mm256_loadu_ps(buf + i + 1);
            const __m256 d  = _mm256_loadu_ps(buf + i - res);
            const __m256 u  = _mm256_loadu_ps(buf + i + res);
            const __m256 in = _mm256_fmadd_ps(west_in, l, _mm256_fmadd_ps(east_in, r, _mm256_fmadd_ps(south_in, d, _mm256_mul_ps(north_in, u))));
            const __m256 adv = _mm256_fmadd_ps(w_adv, in, _mm256_mul_ps(c, stay));
            const __m256 lap = _mm256_fmadd_ps(v_m4, c, _mm256_add_ps(_mm256_add_ps(l, r), _mm256_add_ps(d, u)));
            const __m256 val = _mm256_mul_ps(_mm256_fmadd_ps(diff, lap, adv), decay);
            return _mm256_min_ps(_mm256_max_ps(val, v_zero), v_one);
        }

        SP_OCEAN_WAKE_NOINLINE void avx2_pass3_full_tile(
            const float* __restrict height_in,
            const float* __restrict flux_x_buf,
            const float* __restrict flux_z_buf,
            const float* __restrict speed2_in,
            const float* __restrict foam_in,
            const float* __restrict bub_in,
            const float* __restrict turb_in,
            const float* __restrict sponge_x,
            const float* __restrict sponge_z,
            float* __restrict height_out,
            float* __restrict foam_out,
            float* __restrict bub_out,
            float* __restrict turb_out,
            float dt_dx,
            float decay_foam,
            float decay_bubble,
            float decay_turb,
            int32_t x0,
            int32_t z0,
            float& out_peak,
            float& out_energy,
            float& out_aer_peak)
        {
            const __m256 v_dtdx    = _mm256_set1_ps(dt_dx);
            const __m256 v_visc    = _mm256_set1_ps(visc_height);
            const __m256 v_m4      = _mm256_set1_ps(-4.0f);
            const __m256 v_lvl     = _mm256_set1_ps(level_rate);
            const __m256 v_spmul   = _mm256_set1_ps(0.65f);
            const __m256 v_spaer   = _mm256_set1_ps(1.2f * step_dt);
            const __m256 v_dt      = _mm256_set1_ps(step_dt);
            const __m256 v_one     = _mm256_set1_ps(1.0f);
            const __m256 v_zero    = _mm256_setzero_ps();
            const __m256 v_abs     = _mm256_set1_ps(-0.0f);
            const __m256 v_hgate   = _mm256_set1_ps(0.14f);
            const __m256 v_invg    = _mm256_set1_ps(inv_gravity);
            const __m256 v_minh    = _mm256_set1_ps(0.25f);
            const __m256 v_fdiff   = _mm256_set1_ps(foam_diff);
            const __m256 v_bdiff   = _mm256_set1_ps(bubble_diff);
            const __m256 v_fdec    = _mm256_set1_ps(decay_foam);
            const __m256 v_bdec    = _mm256_set1_ps(decay_bubble);
            const __m256 v_tdec    = _mm256_set1_ps(decay_turb);

            const __m256 spx0      = _mm256_loadu_ps(sponge_x + x0);
            const __m256 spx1      = _mm256_loadu_ps(sponge_x + x0 + 8);
            __m256 v_peak          = v_zero;
            __m256 v_energy        = v_zero;
            __m256 v_aer_peak      = v_zero;

            for (int32_t z = z0; z < z0 + tile_size; z++)
            {
                const int32_t base    = z * res + x0;
                const __m256 spz      = _mm256_set1_ps(sponge_z[z]);
                const __m256 spx_v[2] = { spx0, spx1 };

                for (int32_t k = 0; k < 2; k++)
                {
                    const int32_t i  = base + k * 8;
                    const __m256 hc  = _mm256_loadu_ps(height_in + i);
                    const __m256 hl  = _mm256_loadu_ps(height_in + i - 1);
                    const __m256 hr  = _mm256_loadu_ps(height_in + i + 1);
                    const __m256 hd  = _mm256_loadu_ps(height_in + i - res);
                    const __m256 hu  = _mm256_loadu_ps(height_in + i + res);

                    const __m256 fxl = _mm256_loadu_ps(flux_x_buf + i - 1);
                    const __m256 fxi = _mm256_loadu_ps(flux_x_buf + i);
                    const __m256 fzd = _mm256_loadu_ps(flux_z_buf + i - res);
                    const __m256 fzi = _mm256_loadu_ps(flux_z_buf + i);

                    const __m256 rise   = _mm256_mul_ps(_mm256_add_ps(_mm256_sub_ps(fxl, fxi), _mm256_sub_ps(fzd, fzi)), v_dtdx);
                    const __m256 lap_h  = _mm256_fmadd_ps(v_m4, hc, _mm256_add_ps(_mm256_add_ps(hl, hr), _mm256_add_ps(hd, hu)));
                    const __m256 sp     = _mm256_max_ps(spx_v[k], spz);
                    const __m256 settle = _mm256_fmadd_ps(sp, v_spmul, v_lvl);
                    const __m256 retain = _mm256_max_ps(_mm256_fnmadd_ps(settle, v_dt, v_one), v_zero);
                    __m256 h_new        = _mm256_mul_ps(_mm256_fmadd_ps(v_visc, lap_h, _mm256_add_ps(hc, rise)), retain);

                    const __m256 west_in  = _mm256_max_ps(fxl, v_zero);
                    const __m256 east_in  = _mm256_max_ps(_mm256_sub_ps(v_zero, fxi), v_zero);
                    const __m256 south_in = _mm256_max_ps(fzd, v_zero);
                    const __m256 north_in = _mm256_max_ps(_mm256_sub_ps(v_zero, fzi), v_zero);
                    const __m256 inflow   = _mm256_add_ps(_mm256_add_ps(west_in, east_in), _mm256_add_ps(south_in, north_in));
                    const __m256 eff_h    = _mm256_max_ps(_mm256_mul_ps(_mm256_loadu_ps(speed2_in + i), v_invg), v_minh);
                    const __m256 w_adv    = _mm256_div_ps(v_dtdx, _mm256_max_ps(eff_h, _mm256_mul_ps(v_dtdx, inflow)));
                    const __m256 stay     = _mm256_fnmadd_ps(w_adv, inflow, v_one);

                    __m256 f_new = avx2_advect_diffuse(foam_in, i, stay, w_adv, west_in, east_in, south_in, north_in, v_fdiff, v_fdec, v_m4, v_zero, v_one);
                    __m256 b_new = avx2_advect_diffuse(bub_in,  i, stay, w_adv, west_in, east_in, south_in, north_in, v_bdiff, v_bdec, v_m4, v_zero, v_one);
                    __m256 t_new = avx2_advect_diffuse(turb_in, i, stay, w_adv, west_in, east_in, south_in, north_in, v_fdiff, v_tdec, v_m4, v_zero, v_one);

                    const __m256 sp_fade = _mm256_max_ps(_mm256_fnmadd_ps(sp, v_spaer, v_one), v_zero);
                    f_new = _mm256_mul_ps(f_new, sp_fade);
                    b_new = _mm256_mul_ps(b_new, sp_fade);
                    t_new = _mm256_mul_ps(t_new, sp_fade);

                    const __m256 brk_mask = _mm256_and_ps(_mm256_cmp_ps(h_new, v_hgate, _CMP_GT_OQ), _mm256_cmp_ps(rise, v_zero, _CMP_GT_OQ));
                    if (_mm256_movemask_ps(brk_mask) != 0)
                    {
                        alignas(32) float h_arr[8], r_arr[8], hl_arr[8], hr_arr[8], hd_arr[8], hu_arr[8];
                        alignas(32) float f_arr[8], b_arr[8], t_arr[8];
                        _mm256_store_ps(h_arr,  h_new);
                        _mm256_store_ps(r_arr,  rise);
                        _mm256_store_ps(hl_arr, hl);
                        _mm256_store_ps(hr_arr, hr);
                        _mm256_store_ps(hd_arr, hd);
                        _mm256_store_ps(hu_arr, hu);
                        _mm256_store_ps(f_arr,  f_new);
                        _mm256_store_ps(b_arr,  b_new);
                        _mm256_store_ps(t_arr,  t_new);
                        for (int32_t lane = 0; lane < 8; lane++)
                        {
                            if (h_arr[lane] > 0.14f && r_arr[lane] > 0.0f)
                            {
                                const float sx = (hr_arr[lane] - hl_arr[lane]) * (0.5f * inv_cell);
                                const float sz = (hu_arr[lane] - hd_arr[lane]) * (0.5f * inv_cell);
                                const float s2 = sx * sx + sz * sz;
                                if (s2 > 0.16f)
                                {
                                    const float slope = sqrtf(s2);
                                    const float steep = clamp((slope - 0.40f) * 2.2f, 0.0f, 1.0f);
                                    const float crest = clamp((h_arr[lane] - 0.14f) / 0.18f, 0.0f, 1.0f);
                                    h_arr[lane]      *= (1.0f - 0.07f * steep);
                                    const float entr  = steep * crest * 0.85f * step_dt;
                                    add_asymptotic(f_arr[lane], entr);
                                    add_asymptotic(b_arr[lane], entr * 0.85f);
                                    add_asymptotic(t_arr[lane], entr * 0.90f);
                                }
                            }
                        }
                        h_new = _mm256_load_ps(h_arr);
                        f_new = _mm256_load_ps(f_arr);
                        b_new = _mm256_load_ps(b_arr);
                        t_new = _mm256_load_ps(t_arr);
                    }

                    h_new = avx2_soft_limit_height(h_new);
                    _mm256_storeu_ps(height_out + i, h_new);
                    _mm256_storeu_ps(foam_out   + i, f_new);
                    _mm256_storeu_ps(bub_out    + i, b_new);
                    _mm256_storeu_ps(turb_out   + i, t_new);

                    const __m256 ah   = _mm256_andnot_ps(v_abs, h_new);
                    const __m256 afx  = _mm256_andnot_ps(v_abs, fxi);
                    const __m256 afz  = _mm256_andnot_ps(v_abs, fzi);
                    const __m256 fb   = _mm256_max_ps(f_new, b_new);
                    const __m256 aer  = _mm256_max_ps(fb, t_new);
                    const __m256 vis  = _mm256_max_ps(ah, fb);
                    v_aer_peak        = _mm256_max_ps(v_aer_peak, aer);
                    v_peak            = _mm256_max_ps(v_peak, vis);
                    v_energy          = _mm256_max_ps(v_energy, _mm256_max_ps(vis, _mm256_max_ps(t_new, _mm256_max_ps(afx, afz))));
                }
            }

            out_peak     = hmax_avx2(v_peak);
            out_energy   = hmax_avx2(v_energy);
            out_aer_peak = hmax_avx2(v_aer_peak);
        }

        SP_OCEAN_WAKE_NOINLINE void avx2_pack_f16_tile(
            const float* __restrict h_in,
            const float* __restrict f_in,
            const float* __restrict b_in,
            const float* __restrict t_in,
            uint16_t* __restrict dst,
            int32_t x0,
            int32_t z0)
        {
            for (int32_t z = z0; z < z0 + tile_size; z++)
            {
                const size_t base = static_cast<size_t>(z) * res + static_cast<size_t>(x0);
                for (int32_t ox = 0; ox < tile_size; ox += 8)
                {
                    const size_t i  = base + static_cast<size_t>(ox);
                    const __m128i h = _mm256_cvtps_ph(_mm256_loadu_ps(h_in + i), _MM_FROUND_TO_NEAREST_INT);
                    const __m128i f = _mm256_cvtps_ph(_mm256_loadu_ps(f_in + i), _MM_FROUND_TO_NEAREST_INT);
                    const __m128i b = _mm256_cvtps_ph(_mm256_loadu_ps(b_in + i), _MM_FROUND_TO_NEAREST_INT);
                    const __m128i t = _mm256_cvtps_ph(_mm256_loadu_ps(t_in + i), _MM_FROUND_TO_NEAREST_INT);

                    const __m128i hf_lo = _mm_unpacklo_epi16(h, f);
                    const __m128i hf_hi = _mm_unpackhi_epi16(h, f);
                    const __m128i bt_lo = _mm_unpacklo_epi16(b, t);
                    const __m128i bt_hi = _mm_unpackhi_epi16(b, t);

                    uint16_t* out = dst + i * 4;
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(out +  0), _mm_unpacklo_epi32(hf_lo, bt_lo));
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(out +  8), _mm_unpackhi_epi32(hf_lo, bt_lo));
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 16), _mm_unpacklo_epi32(hf_hi, bt_hi));
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 24), _mm_unpackhi_epi32(hf_hi, bt_hi));
                }
            }
        }
#endif

        struct Body
        {
            vector<OceanWakeColumn> current;
            vector<OceanWakeColumn> previous;
            bool has_previous = false;
            uint32_t idle     = 0;
            bool reported     = false;
        };

        struct State
        {
            mutex lock;
            vector<float> height;
            vector<float> height_next;
            vector<float> eta_eff;    // dispersive pressure potential
            vector<float> flux_x;     // m^2/s across east face
            vector<float> flux_z;     // m^2/s across north face
            vector<float> flux_x_next;
            vector<float> flux_z_next;
            vector<float> foam;       // fresh surface whitewater froth (0..1)
            vector<float> foam_next;
            vector<float> bubbles;    // lingering subsurface aerated bubble trail (0..1)
            vector<float> bubbles_next;
            vector<float> turbulence; // wake slipstream agitation (0..1)
            vector<float> turb_next;
            vector<float> bed_speed2; // wave speed^2 from terrain bathymetry alone
            vector<float> speed2;     // effective wave speed^2 (0 on dry beach or static obstacle)
            vector<uint8_t> obstacle; // 1 = reflective dry beach or static waterline obstacle

            float sponge_x[res]            = {};
            float sponge_z[res]            = {};
            uint8_t tile_active[tile_count]       = {}; // 1 if tile contains live waves/flux/foam
            uint8_t tile_aeration[tile_count]     = {}; // 1 if tile contains live foam/bubbles/turbulence
            uint8_t tile_has_obstacle[tile_count] = {}; // 1 if tile or its 1-cell halo touches an obstacle
            uint8_t tile_dirty_pack[tile_count]   = {}; // 1 if CPU RGBA16F shadow needs repacking
            uint8_t tile_dirty_gpu[tile_count]    = {}; // bit 0 = tex A dirty, bit 1 = tex B dirty
            unordered_map<uint64_t, Body> bodies;
            unordered_map<uint64_t, OceanWakeObstacle> obstacles;
            bool obstacles_dirty = false;
            bool has_obstacles   = false;

            int32_t origin_x  = 0;
            int32_t origin_z  = 0;
            bool origin_valid = false;
            float sea_level   = numeric_limits<float>::quiet_NaN();
            float depth_key   = 0.0f;
            float accumulator = 0.0f;
            float quiet_time  = 0.0f;
            bool active       = false;
            bool upload_zero  = false;
            bool needs_upload = false;
        };

        State& state()
        {
            static State instance;
            return instance;
        }

        inline size_t cell_index(int32_t cx, int32_t cz)
        {
            return static_cast<size_t>(cz & mask) * res + static_cast<size_t>(cx & mask);
        }

        inline int32_t tile_index(int32_t tx, int32_t tz)
        {
            return (tz & tile_mask) * tile_res + (tx & tile_mask);
        }

        inline void mark_cell_tile_dirty(State& s, int32_t cx, int32_t cz, bool activate)
        {
            const int32_t t = tile_index((cx & mask) >> tile_shift, (cz & mask) >> tile_shift);
            if (activate)
            {
                s.tile_active[t] = 1u;
            }
            s.tile_dirty_pack[t] = 1u;
            s.tile_dirty_gpu[t] |= 3u;
        }

        inline void mark_rect_tiles_active(State& s, int32_t cx_lo, int32_t cx_hi, int32_t cz_lo, int32_t cz_hi, bool aeration = false)
        {
            const int32_t tx0 = cx_lo >> tile_shift;
            const int32_t tx1 = cx_hi >> tile_shift;
            const int32_t tz0 = cz_lo >> tile_shift;
            const int32_t tz1 = cz_hi >> tile_shift;
            for (int32_t tz = tz0; tz <= tz1; tz++)
            {
                const int32_t row = (tz & tile_mask) * tile_res;
                for (int32_t tx = tx0; tx <= tx1; tx++)
                {
                    const int32_t t = row + (tx & tile_mask);
                    s.tile_active[t]     = 1u;
                    if (aeration)
                    {
                        s.tile_aeration[t] = 1u;
                    }
                    s.tile_dirty_pack[t] = 1u;
                    s.tile_dirty_gpu[t] |= 3u;
                }
            }
        }

        template <typename Fn>
        inline void run_tiles(uint32_t count, Fn&& fn)
        {
            if (count <= 40u)
            {
                fn(0u, count);
            }
            else
            {
                ThreadPool::ParallelLoop(forward<Fn>(fn), count);
            }
        }

        inline bool in_window(const State& s, int32_t cx, int32_t cz)
        {
            return s.origin_valid && cx >= s.origin_x && cx < s.origin_x + res && cz >= s.origin_z && cz < s.origin_z + res;
        }

        inline float soft_limit_height(float h)
        {
            const float a = fabsf(h);
            if (a <= height_soft_knee)
            {
                return h;
            }
            const float excess = a - height_soft_knee;
            const float span   = height_max - height_soft_knee;
            const float capped = height_soft_knee + span * (excess / (excess + span));
            return h < 0.0f ? -capped : capped;
        }

        void ensure_cells(State& s)
        {
            if (s.height.empty())
            {
                const size_t count = static_cast<size_t>(res) * res;
                s.height.assign(count, 0.0f);
                s.height_next.assign(count, 0.0f);
                s.eta_eff.assign(count, 0.0f);
                s.flux_x.assign(count, 0.0f);
                s.flux_z.assign(count, 0.0f);
                s.flux_x_next.assign(count, 0.0f);
                s.flux_z_next.assign(count, 0.0f);
                s.foam.assign(count, 0.0f);
                s.foam_next.assign(count, 0.0f);
                s.bubbles.assign(count, 0.0f);
                s.bubbles_next.assign(count, 0.0f);
                s.turbulence.assign(count, 0.0f);
                s.turb_next.assign(count, 0.0f);
                s.bed_speed2.assign(count, 0.0f);
                s.speed2.assign(count, 0.0f);
                s.obstacle.assign(count, 0u);
            }
        }

        float cell_height(const State& s, int32_t cx, int32_t cz)
        {
            return in_window(s, cx, cz) ? s.height[cell_index(cx, cz)] : 0.0f;
        }

        float sample_locked(const State& s, float world_x, float world_z)
        {
            if (!s.active || s.height.empty())
            {
                return 0.0f;
            }

            const float gx   = world_x * inv_cell - 0.5f;
            const float gz   = world_z * inv_cell - 0.5f;
            const float fx0  = floorf(gx);
            const float fz0  = floorf(gz);
            const int32_t x0 = static_cast<int32_t>(fx0);
            const int32_t z0 = static_cast<int32_t>(fz0);
            const float tx   = gx - fx0;
            const float tz   = gz - fz0;
            const float a    = cell_height(s, x0, z0)     * (1.0f - tx) + cell_height(s, x0 + 1, z0)     * tx;
            const float b    = cell_height(s, x0, z0 + 1) * (1.0f - tx) + cell_height(s, x0 + 1, z0 + 1) * tx;
            return a * (1.0f - tz) + b * tz;
        }

        Vector2 sample_velocity_locked(const State& s, float world_x, float world_z)
        {
            if (!s.active || s.flux_x.empty())
            {
                return Vector2::Zero;
            }

            const int32_t cx = static_cast<int32_t>(floorf(world_x * inv_cell));
            const int32_t cz = static_cast<int32_t>(floorf(world_z * inv_cell));
            if (!in_window(s, cx, cz))
            {
                return Vector2::Zero;
            }

            const size_t i = cell_index(cx, cz);
            if (s.obstacle[i] || s.speed2[i] <= 0.0f)
            {
                return Vector2::Zero;
            }

            const float inv_depth = gravity / max(s.speed2[i], 0.5f * gravity);
            const float qx        = 0.5f * (s.flux_x[cell_index(cx - 1, cz)] + s.flux_x[i]);
            const float qz        = 0.5f * (s.flux_z[cell_index(cx, cz - 1)] + s.flux_z[i]);
            return Vector2(clamp(qx * inv_depth, -4.0f, 4.0f), clamp(qz * inv_depth, -4.0f, 4.0f));
        }

        inline void mark_obstacle_tile_halo(State& s, int32_t cx, int32_t cz)
        {
            const int32_t lx  = cx & mask;
            const int32_t lz  = cz & mask;
            const int32_t tx  = lx >> tile_shift;
            const int32_t tz  = lz >> tile_shift;
            s.tile_has_obstacle[tile_index(tx, tz)] = 1u;

            const int32_t rx = lx & (tile_size - 1);
            const int32_t rz = lz & (tile_size - 1);
            if (rx == 0)
            {
                s.tile_has_obstacle[tile_index(tx - 1, tz)] = 1u;
            }
            else if (rx == tile_size - 1)
            {
                s.tile_has_obstacle[tile_index(tx + 1, tz)] = 1u;
            }
            if (rz == 0)
            {
                s.tile_has_obstacle[tile_index(tx, tz - 1)] = 1u;
            }
            else if (rz == tile_size - 1)
            {
                s.tile_has_obstacle[tile_index(tx, tz + 1)] = 1u;
            }
        }

        void stamp_obstacles_locked(State& s, bool has_terrain)
        {
            if (!s.origin_valid || s.speed2.empty())
            {
                return;
            }

            // open-ocean fast path: when there is no terrain and no registered obstacles (and none were previously stamped),
            // bed_speed2 and speed2 are already uniform open-water speed and obstacle[] is all zeros
            if (!has_terrain && s.obstacles.empty() && !s.has_obstacles)
            {
                s.obstacles_dirty = false;
                return;
            }

            memset(s.tile_has_obstacle, 0, sizeof(s.tile_has_obstacle));
            bool any_obstacle = false;

            for (int32_t tz = 0; tz < tile_res; tz++)
            {
                const int32_t z0 = tz << tile_shift;
                for (int32_t tx = 0; tx < tile_res; tx++)
                {
                    const int32_t x0 = tx << tile_shift;
                    for (int32_t z = z0; z < z0 + tile_size; z++)
                    {
                        const size_t row = static_cast<size_t>(z) * res;
                        for (int32_t x = x0; x < x0 + tile_size; x++)
                        {
                            const size_t i = row + static_cast<size_t>(x);
                            const float bs = s.bed_speed2[i];
                            s.speed2[i]    = bs;
                            if (bs > 0.0f)
                            {
                                s.obstacle[i] = 0u;
                            }
                            else
                            {
                                s.obstacle[i] = 1u;
                                any_obstacle  = true;
                                mark_obstacle_tile_halo(s, x, z);
                            }
                        }
                    }
                }
            }

            const float win_min_x = s.origin_x * cell_size;
            const float win_min_z = s.origin_z * cell_size;
            const float win_max_x = win_min_x + OceanWake::window_size;
            const float win_max_z = win_min_z + OceanWake::window_size;
            const float sea_lo    = s.sea_level - 0.6f;
            const float sea_hi    = s.sea_level + 1.2f;

            for (const auto& entry : s.obstacles)
            {
                const OceanWakeObstacle& obs = entry.second;
                if (obs.max_y < sea_lo || obs.min_y > sea_hi)
                {
                    continue;
                }
                if (obs.max_x < win_min_x || obs.min_x > win_max_x || obs.max_z < win_min_z || obs.min_z > win_max_z)
                {
                    continue;
                }

                const int32_t cx0 = max(s.origin_x, static_cast<int32_t>(floorf(obs.min_x * inv_cell)));
                const int32_t cx1 = min(s.origin_x + res - 1, static_cast<int32_t>(floorf(obs.max_x * inv_cell)));
                const int32_t cz0 = max(s.origin_z, static_cast<int32_t>(floorf(obs.min_z * inv_cell)));
                const int32_t cz1 = min(s.origin_z + res - 1, static_cast<int32_t>(floorf(obs.max_z * inv_cell)));
                const float mid_x = 0.5f * (obs.min_x + obs.max_x);
                const float mid_z = 0.5f * (obs.min_z + obs.max_z);
                const float rx    = max(0.5f * (obs.max_x - obs.min_x), 0.15f);
                const float rz    = max(0.5f * (obs.max_z - obs.min_z), 0.15f);

                for (int32_t cz = cz0; cz <= cz1; cz++)
                {
                    const float wz = (cz + 0.5f) * cell_size;
                    const float dz = (wz - mid_z) / rz;
                    for (int32_t cx = cx0; cx <= cx1; cx++)
                    {
                        if (obs.is_circle)
                        {
                            const float wx = (cx + 0.5f) * cell_size;
                            const float dx = (wx - mid_x) / rx;
                            if (dx * dx + dz * dz > 1.0f)
                            {
                                continue;
                            }
                        }
                        const size_t i = cell_index(cx, cz);
                        s.obstacle[i]  = 1u;
                        s.speed2[i]    = 0.0f;
                        s.height[i]    = 0.0f;
                        s.height_next[i] = 0.0f;
                        s.eta_eff[i]   = 0.0f;
                        s.flux_x[i]    = 0.0f;
                        s.flux_z[i]    = 0.0f;
                        s.flux_x_next[i] = 0.0f;
                        s.flux_z_next[i] = 0.0f;
                        any_obstacle   = true;
                        mark_obstacle_tile_halo(s, cx, cz);
                        mark_cell_tile_dirty(s, cx, cz, false);
                        s.needs_upload = true;
                    }
                }
            }
            s.has_obstacles   = any_obstacle;
            s.obstacles_dirty = false;
        }

        void reset_cell(State& s, Terrain* terrain, int32_t cx, int32_t cz)
        {
            const size_t i      = cell_index(cx, cz);
            s.height[i]         = 0.0f;
            s.height_next[i]    = 0.0f;
            s.eta_eff[i]        = 0.0f;
            s.flux_x[i]         = 0.0f;
            s.flux_z[i]         = 0.0f;
            s.flux_x_next[i]    = 0.0f;
            s.flux_z_next[i]    = 0.0f;
            s.flux_x[cell_index(cx - 1, cz)]      = 0.0f;
            s.flux_x_next[cell_index(cx - 1, cz)] = 0.0f;
            s.flux_z[cell_index(cx, cz - 1)]      = 0.0f;
            s.flux_z_next[cell_index(cx, cz - 1)] = 0.0f;
            s.foam[i]           = 0.0f;
            s.foam_next[i]      = 0.0f;
            s.bubbles[i]        = 0.0f;
            s.bubbles_next[i]   = 0.0f;
            s.turbulence[i]     = 0.0f;
            s.turb_next[i]      = 0.0f;

            float depth = s.depth_key;
            float bed   = 0.0f;
            if (terrain && terrain->SampleHeight((cx + 0.5f) * cell_size, (cz + 0.5f) * cell_size, bed))
            {
                depth = s.sea_level - bed;
            }
            const float spd = depth > depth_dry ? gravity * min(depth, s.depth_key) : 0.0f;
            s.bed_speed2[i] = spd;
            s.speed2[i]     = spd;
            s.obstacle[i]   = spd > 0.0f ? 0u : 1u;
        }

        void reset_columns(State& s, Terrain* terrain, int32_t cx0, int32_t cx1)
        {
            for (int32_t cx = cx0; cx < cx1; cx++)
            {
                const int32_t tx = (cx & mask) >> tile_shift;
                for (int32_t tz = 0; tz < tile_res; tz++)
                {
                    const int32_t t = tz * tile_res + tx;
                    if (s.tile_active[t])
                    {
                        s.tile_dirty_pack[t] = 1u;
                        s.tile_dirty_gpu[t] |= 3u;
                        s.needs_upload       = true;
                    }
                }
                for (int32_t cz = s.origin_z; cz < s.origin_z + res; cz++)
                {
                    reset_cell(s, terrain, cx, cz);
                }
            }
        }

        void reset_rows(State& s, Terrain* terrain, int32_t cz0, int32_t cz1)
        {
            for (int32_t cz = cz0; cz < cz1; cz++)
            {
                const int32_t tz = (cz & mask) >> tile_shift;
                for (int32_t tx = 0; tx < tile_res; tx++)
                {
                    const int32_t t = tz * tile_res + tx;
                    if (s.tile_active[t])
                    {
                        s.tile_dirty_pack[t] = 1u;
                        s.tile_dirty_gpu[t] |= 3u;
                        s.needs_upload       = true;
                    }
                }
                for (int32_t cx = s.origin_x; cx < s.origin_x + res; cx++)
                {
                    reset_cell(s, terrain, cx, cz);
                }
            }
        }

        void update_sponge(State& s);

        void move_window(State& s, const Vector3& focus, float sea_level)
        {
            const int32_t target_x       = static_cast<int32_t>(floorf(focus.x * inv_cell)) - res / 2;
            const int32_t target_z       = static_cast<int32_t>(floorf(focus.z * inv_cell)) - res / 2;
            constexpr float stable_depth = 0.42f * cell_size * cell_size / (gravity * step_dt * step_dt);
            const float depth            = clamp(cvar_depth.GetValue(), 0.2f, stable_depth);
            Terrain* terrain             = Terrain::FindActive();

            const bool rebuild = !s.origin_valid || sea_level != s.sea_level || depth != s.depth_key || abs(target_x - s.origin_x) >= res || abs(target_z - s.origin_z) >= res;
            if (rebuild)
            {
                s.sea_level    = sea_level;
                s.depth_key    = depth;
                s.origin_x     = target_x;
                s.origin_z     = target_z;
                s.origin_valid = true;
                memset(s.tile_active,   0, sizeof(s.tile_active));
                memset(s.tile_aeration, 0, sizeof(s.tile_aeration));
                if (!terrain)
                {
                    const float spd = depth > depth_dry ? gravity * depth : 0.0f;
                    fill(s.height.begin(), s.height.end(), 0.0f);
                    fill(s.height_next.begin(), s.height_next.end(), 0.0f);
                    fill(s.eta_eff.begin(), s.eta_eff.end(), 0.0f);
                    fill(s.flux_x.begin(), s.flux_x.end(), 0.0f);
                    fill(s.flux_z.begin(), s.flux_z.end(), 0.0f);
                    fill(s.flux_x_next.begin(), s.flux_x_next.end(), 0.0f);
                    fill(s.flux_z_next.begin(), s.flux_z_next.end(), 0.0f);
                    fill(s.foam.begin(), s.foam.end(), 0.0f);
                    fill(s.foam_next.begin(), s.foam_next.end(), 0.0f);
                    fill(s.bubbles.begin(), s.bubbles.end(), 0.0f);
                    fill(s.bubbles_next.begin(), s.bubbles_next.end(), 0.0f);
                    fill(s.turbulence.begin(), s.turbulence.end(), 0.0f);
                    fill(s.turb_next.begin(), s.turb_next.end(), 0.0f);
                    fill(s.bed_speed2.begin(), s.bed_speed2.end(), spd);
                    fill(s.speed2.begin(), s.speed2.end(), spd);
                    fill(s.obstacle.begin(), s.obstacle.end(), spd > 0.0f ? 0u : 1u);
                }
                else
                {
                    reset_rows(s, terrain, s.origin_z, s.origin_z + res);
                }
                memset(s.tile_dirty_pack, 1u, sizeof(s.tile_dirty_pack));
                memset(s.tile_dirty_gpu,  3u, sizeof(s.tile_dirty_gpu));
                s.needs_upload = true;
                stamp_obstacles_locked(s, terrain != nullptr);
                update_sponge(s);
                return;
            }

            const int32_t slack = 12;
            int32_t dx = target_x - s.origin_x;
            int32_t dz = target_z - s.origin_z;
            dx = abs(dx) > slack ? dx : 0;
            dz = abs(dz) > slack ? dz : 0;
            bool shifted = false;
            if (dx != 0)
            {
                const int32_t old_x = s.origin_x;
                s.origin_x          = target_x;
                shifted             = true;
                if (dx > 0)
                {
                    reset_columns(s, terrain, old_x + res, s.origin_x + res);
                }
                else
                {
                    reset_columns(s, terrain, s.origin_x, old_x);
                }
            }
            if (dz != 0)
            {
                const int32_t old_z = s.origin_z;
                s.origin_z          = target_z;
                shifted             = true;
                if (dz > 0)
                {
                    reset_rows(s, terrain, old_z + res, s.origin_z + res);
                }
                else
                {
                    reset_rows(s, terrain, s.origin_z, old_z);
                }
            }

            if (shifted)
            {
                update_sponge(s);
            }
            if (shifted || s.obstacles_dirty)
            {
                stamp_obstacles_locked(s, terrain != nullptr);
            }
        }

        // band-limited Gaussian volume footprint: exact volume conservation with zero grid-aliasing spikes
        void splat_gaussian_volume(State& s, float x, float z, float signed_volume, float radius)
        {
            if (fabsf(signed_volume) < 1e-6f || !isfinite(x) || !isfinite(z) || !isfinite(signed_volume))
            {
                return;
            }

            const float sigma       = clamp(radius * 0.95f, 0.38f, 2.2f);
            const float sigma2      = sigma * sigma;
            const float inv_2sigma2 = 0.5f / sigma2;
            const float amp         = signed_volume / (two_pi * sigma2); // integral over dA == signed_volume

            const int32_t center_x = static_cast<int32_t>(floorf(x * inv_cell));
            const int32_t center_z = static_cast<int32_t>(floorf(z * inv_cell));
            const int32_t r_cells  = min(static_cast<int32_t>(ceilf(2.5f * sigma * inv_cell)), 11);

            if (center_x + r_cells < s.origin_x || center_x - r_cells >= s.origin_x + res ||
                center_z + r_cells < s.origin_z || center_z - r_cells >= s.origin_z + res)
            {
                return;
            }

            const int32_t cz_lo = max(s.origin_z + 1, center_z - r_cells);
            const int32_t cz_hi = min(s.origin_z + res - 2, center_z + r_cells);
            const int32_t cx_lo = max(s.origin_x + 1, center_x - r_cells);
            const int32_t cx_hi = min(s.origin_x + res - 2, center_x + r_cells);

            bool touched = false;
            for (int32_t cz = cz_lo; cz <= cz_hi; cz++)
            {
                const float dz  = (cz + 0.5f) * cell_size - z;
                const float dz2 = dz * dz;
                for (int32_t cx = cx_lo; cx <= cx_hi; cx++)
                {
                    const size_t i = cell_index(cx, cz);
                    if (s.obstacle[i])
                    {
                        continue;
                    }

                    const float dx = (cx + 0.5f) * cell_size - x;
                    const float r2 = dx * dx + dz2;
                    if (r2 > 6.25f * sigma2)
                    {
                        continue;
                    }

                    const float dh = amp * expf(-r2 * inv_2sigma2);
                    s.height[i]    = soft_limit_height(s.height[i] + dh);
                    touched        = true;
                }
            }
            if (touched)
            {
                mark_rect_tiles_active(s, cx_lo, cx_hi, cz_lo, cz_hi);
                s.needs_upload = true;
            }
        }

        struct BodyKinematics
        {
            float cx             = 0.0f;
            float cz             = 0.0f;
            float vx             = 0.0f;
            float vz             = 0.0f;
            float vy_min         = 0.0f; // most downward plunge velocity (negative)
            float vy_max         = 0.0f; // most upward rise velocity (positive)
            float speed_h        = 0.0f;
            float speed_3        = 0.0f;
            float dir_x          = 0.0f;
            float dir_z          = 0.0f;
            float total_volume   = 0.0f;
            float half_len       = 0.45f;
            float half_beam      = 0.38f;
            float max_aeration   = 0.0f;
            uint32_t wet_count   = 0;
        };

        // coherent hull hydrodynamics: bow parting & Kelvin V-wake forcing, radial impact splashes, and multi-zone aeration
        // computed in the body's own frame so multi-column hulls never cancel internally or oversaturate foam
        void inject_body_hydrodynamics(State& s, const BodyKinematics& bk, float strength, bool entry_impact)
        {
            if (bk.wet_count == 0 || !(bk.total_volume > 0.0f) || !isfinite(bk.cx) || !isfinite(bk.cz))
            {
                return;
            }

            const float abs_vy = max(-bk.vy_min, bk.vy_max);
            if (bk.speed_h < 0.08f && abs_vy < 0.08f && bk.max_aeration < 0.01f && !entry_impact)
            {
                return;
            }

            const float half_len   = clamp(bk.half_len,  0.38f, 4.5f);
            const float half_beam  = clamp(bk.half_beam, 0.34f, 3.0f);
            const float max_extent = max(half_len, half_beam);
            const float inv_len    = 1.0f / half_len;
            const float inv_beam   = 1.0f / half_beam;

            const int32_t center_x = static_cast<int32_t>(floorf(bk.cx * inv_cell));
            const int32_t center_z = static_cast<int32_t>(floorf(bk.cz * inv_cell));
            const int32_t r_cells  = min(static_cast<int32_t>(ceilf((max_extent * 2.4f + 0.6f) * inv_cell)), 24);

            if (center_x + r_cells < s.origin_x || center_x - r_cells >= s.origin_x + res ||
                center_z + r_cells < s.origin_z || center_z - r_cells >= s.origin_z + res)
            {
                return;
            }

            // hull wave-making intensity: scales with displaced volume^0.45 and dynamic Froude/planing speed
            const float v_cap      = min(bk.speed_h, 24.0f);
            const float vol_factor = powf(clamp(bk.total_volume, 0.02f, 10.0f), 0.45f);
            const float hull_flux  = strength * vol_factor * v_cap * (1.0f + 0.045f * v_cap) * 1.65f * step_dt;

            // radial outward splash impulse on plunges and high-speed water entries (shot cubes, cargo drops, dives)
            const float plunge_vel  = max(-bk.vy_min, 0.0f) + (entry_impact ? min(bk.speed_3, 28.0f) * 0.72f : 0.0f);
            const float rise_vel    = max(bk.vy_max, 0.0f);
            const float splash_rad  = clamp(sqrtf(half_len * half_beam), 0.38f, 2.2f);
            const float inv_srad    = 1.0f / splash_rad;
            const float inv_2srad2  = 0.5f / (splash_rad * splash_rad);
            const float radial_flux = strength * (plunge_vel * 6.8f - rise_vel * 1.8f) * vol_factor * step_dt;

            // physical aeration drivers
            const float impact_spd      = max(-bk.vy_min, entry_impact ? bk.speed_3 * 0.72f : 0.0f);
            const float splash_aeration = impact_spd > 0.85f ? clamp((impact_spd - 0.85f) / 6.5f, 0.0f, 1.0f) : 0.0f;
            const float move_aeration   = bk.speed_h > 1.8f ? clamp((bk.speed_h - 1.8f) / 10.0f, 0.0f, 1.0f) : 0.0f;
            const float prop_aeration   = clamp(bk.max_aeration, 0.0f, 1.0f);

            const int32_t cz_lo = max(s.origin_z + 1, center_z - r_cells);
            const int32_t cz_hi = min(s.origin_z + res - 2, center_z + r_cells);
            const int32_t cx_lo = max(s.origin_x + 1, center_x - r_cells);
            const int32_t cx_hi = min(s.origin_x + res - 2, center_x + r_cells);

            for (int32_t cz = cz_lo; cz <= cz_hi; cz++)
            {
                const float dz  = (cz + 0.5f) * cell_size - bk.cz;
                const float dz2 = dz * dz;
                for (int32_t cx = cx_lo; cx <= cx_hi; cx++)
                {
                    const size_t i = cell_index(cx, cz);
                    if (s.obstacle[i])
                    {
                        continue;
                    }

                    const float dx = (cx + 0.5f) * cell_size - bk.cx;
                    const float r2 = dx * dx + dz2;

                    // coherent bow parting, Kelvin V-wake diverging arms, and stern closing inflow
                    if (bk.speed_h >= 0.15f)
                    {
                        const float along  =  dx * bk.dir_x + dz * bk.dir_z;
                        const float across = -dx * bk.dir_z + dz * bk.dir_x;
                        const float u_len  = along  * inv_len;
                        const float u_beam = across * inv_beam;
                        const float ell2   = u_len * u_len + u_beam * u_beam;

                        if (ell2 < 5.76f) // inside 2.4x hull ellipsoid
                        {
                            const float hull_g = expf(-0.55f * ell2);

                            // bow & forward chines push strongly outward; aft transom closes gently inward to feed transverse stern waves
                            const float bow_part    = expf(-0.85f * (u_len - 0.45f) * (u_len - 0.45f));
                            const float stern_close = expf(-1.10f * (u_len + 0.95f) * (u_len + 0.95f));
                            const float side_sign   = across / (fabsf(across) + 0.28f * half_beam);
                            const float push_lat    = hull_flux * hull_g * (bow_part * 1.15f - stern_close * 0.30f) * side_sign;
                            const float push_fwd    = hull_flux * hull_g * (bow_part * 0.48f - stern_close * 0.16f);

                            const float fx = (-bk.dir_z * push_lat + bk.dir_x * push_fwd);
                            const float fz = ( bk.dir_x * push_lat + bk.dir_z * push_fwd);
                            if (!s.obstacle[cell_index(cx + 1, cz)])
                            {
                                s.flux_x[i] += fx;
                            }
                            if (!s.obstacle[cell_index(cx, cz + 1)])
                            {
                                s.flux_z[i] += fz;
                            }

                            // realistic multi-zone hull aeration: crisp bow-shoulder lace + narrow prop-wash over wide turquoise bubbles
                            if (move_aeration > 0.0f || prop_aeration > 0.0f)
                            {
                                const float abs_ub    = fabsf(u_beam);
                                const float chine_env = expf(-3.4f * (abs_ub - 0.92f) * (abs_ub - 0.92f)) * clamp(u_len + 0.55f, 0.0f, 1.0f) * clamp(1.35f - u_len, 0.0f, 1.0f);
                                const float bow_foam  = move_aeration * move_aeration * chine_env * 3.2f * step_dt;

                                const float aft_env    = expf(-1.25f * (u_len + 0.90f) * (u_len + 0.90f));
                                const float prop_core  = expf(-2.60f * u_beam * u_beam) * aft_env;
                                const float bub_plume  = expf(-0.95f * u_beam * u_beam) * aft_env;
                                const float stern_foam = (prop_aeration * 4.0f + move_aeration * move_aeration * 1.3f) * prop_core * step_dt;
                                const float stern_bub  = (prop_aeration * 7.5f + move_aeration * 3.6f) * bub_plume * step_dt;
                                const float stern_turb = (prop_aeration * 5.0f + move_aeration * 3.0f) * hull_g * step_dt;

                                add_asymptotic(s.foam[i],       bow_foam + stern_foam);
                                add_asymptotic(s.bubbles[i],    stern_bub + bow_foam * 1.5f);
                                add_asymptotic(s.turbulence[i], stern_turb);
                            }
                        }
                    }

                    // outward radial splash wave + circular crown aeration on impact/plunge
                    if (r2 < 6.25f * splash_rad * splash_rad)
                    {
                        const float g = expf(-r2 * inv_2srad2);

                        if (fabsf(radial_flux) > 1e-5f && r2 > 1e-4f)
                        {
                            const float inv_r = 1.0f / sqrtf(r2);
                            const float rf    = radial_flux * g * inv_r;
                            if (!s.obstacle[cell_index(cx + 1, cz)])
                            {
                                s.flux_x[i] += dx * rf;
                            }
                            if (!s.obstacle[cell_index(cx, cz + 1)])
                            {
                                s.flux_z[i] += dz * rf;
                            }
                        }

                        if (splash_aeration > 0.0f)
                        {
                            const float r    = sqrtf(r2);
                            const float dr   = (r - 0.90f * splash_rad) * inv_srad;
                            const float ring = expf(-dr * dr * 1.8f);
                            const float s2   = splash_aeration * splash_aeration;
                            const float gain = entry_impact ? 2.4f : 1.0f;
                            add_asymptotic(s.foam[i],       s2 * ring * 6.5f * gain * step_dt);
                            add_asymptotic(s.bubbles[i],    splash_aeration * g * 9.5f * gain * step_dt);
                            add_asymptotic(s.turbulence[i], splash_aeration * g * 9.0f * gain * step_dt);
                        }
                    }
                }
            }

            const bool added_aeration = move_aeration > 0.0f || prop_aeration > 0.0f || splash_aeration > 0.0f;
            mark_rect_tiles_active(s, cx_lo, cx_hi, cz_lo, cz_hi, added_aeration);
            s.needs_upload = true;
        }

        void inject_bodies_substep(State& s, uint32_t step_idx, uint32_t step_count, float sim_dt)
        {
            const float strength = max(cvar_strength.GetValue(), 0.0f);
            if (strength <= 0.0f)
            {
                return;
            }

            const float inv_steps = 1.0f / static_cast<float>(max(step_count, 1u));
            const float f0        = static_cast<float>(step_idx) * inv_steps;
            const float f1        = static_cast<float>(step_idx + 1u) * inv_steps;
            const float f_mid     = 0.5f * (f0 + f1);
            const float back      = (1.0f - f_mid) * sim_dt;
            // fraction of displaced hydrostatic volume that radiates as surface gravity waves
            constexpr float volume_wave_share = 0.65f;

            for (const auto& entry : s.bodies)
            {
                const Body& body = entry.second;
                if (body.current.empty())
                {
                    continue;
                }

                const bool is_entry  = !body.has_previous;
                const bool same_size = body.has_previous && body.previous.size() == body.current.size();

                BodyKinematics bk = {};
                float vx_w = 0.0f;
                float vz_w = 0.0f;
                float max_col_rad = 0.34f;

                if (same_size)
                {
                    for (size_t k = 0; k < body.current.size(); k++)
                    {
                        const OceanWakeColumn& prev = body.previous[k];
                        const OceanWakeColumn& curr = body.current[k];
                        const float dx              = curr.x - prev.x;
                        const float dz              = curr.z - prev.z;
                        const bool continuous       = (dx * dx + dz * dz) < 36.0f;

                        const float v0 = prev.volume + (curr.volume - prev.volume) * f0;
                        const float v1 = prev.volume + (curr.volume - prev.volume) * f1;
                        const float vm = prev.volume + (curr.volume - prev.volume) * f_mid;

                        if (continuous)
                        {
                            const float x0 = prev.x      + dx * f0;
                            const float z0 = prev.z      + dz * f0;
                            const float r0 = prev.radius + (curr.radius - prev.radius) * f0;
                            const float x1 = prev.x      + dx * f1;
                            const float z1 = prev.z      + dz * f1;
                            const float r1 = prev.radius + (curr.radius - prev.radius) * f1;

                            const float dv_scale = strength * volume_wave_share;
                            if ((v1 > 1e-5f || v0 > 1e-5f) && (fabsf(x1 - x0) > 1e-4f || fabsf(z1 - z0) > 1e-4f || fabsf(v1 - v0) > 1e-5f))
                            {
                                if (v1 > 1e-5f)
                                {
                                    splat_gaussian_volume(s, x1, z1,  v1 * dv_scale, r1);
                                }
                                if (v0 > 1e-5f)
                                {
                                    splat_gaussian_volume(s, x0, z0, -v0 * dv_scale, r0);
                                }
                            }
                        }

                        if (vm > 1e-5f)
                        {
                            const float xm  = continuous ? (prev.x        + dx * f_mid) : curr.x;
                            const float zm  = continuous ? (prev.z        + dz * f_mid) : curr.z;
                            const float vxm = continuous ? (prev.vx       + (curr.vx       - prev.vx)       * f_mid) : curr.vx;
                            const float vym = continuous ? (prev.vy       + (curr.vy       - prev.vy)       * f_mid) : curr.vy;
                            const float vzm = continuous ? (prev.vz       + (curr.vz       - prev.vz)       * f_mid) : curr.vz;
                            const float am  = continuous ? (prev.aeration + (curr.aeration - prev.aeration) * f_mid) : curr.aeration;
                            const float rm  = continuous ? (prev.radius   + (curr.radius   - prev.radius)   * f_mid) : curr.radius;

                            bk.cx           += xm * vm;
                            bk.cz           += zm * vm;
                            vx_w            += vxm * vm;
                            vz_w            += vzm * vm;
                            bk.total_volume += vm;
                            bk.vy_min        = min(bk.vy_min, vym);
                            bk.vy_max        = max(bk.vy_max, vym);
                            bk.max_aeration  = max(bk.max_aeration, am);
                            max_col_rad      = max(max_col_rad, rm);
                            bk.wet_count++;
                        }
                    }
                }
                else
                {
                    const float dv_scale = strength * volume_wave_share * inv_steps;
                    for (const OceanWakeColumn& curr : body.current)
                    {
                        if (!(curr.volume > 1e-5f))
                        {
                            continue;
                        }
                        float cx = curr.x;
                        float cz = curr.z;
                        if (back > 0.0f)
                        {
                            cx -= clamp(curr.vx * back, -2.5f, 2.5f);
                            cz -= clamp(curr.vz * back, -2.5f, 2.5f);
                        }
                        splat_gaussian_volume(s, cx, cz, curr.volume * dv_scale, curr.radius);

                        bk.cx           += cx * curr.volume;
                        bk.cz           += cz * curr.volume;
                        vx_w            += curr.vx * curr.volume;
                        vz_w            += curr.vz * curr.volume;
                        bk.total_volume += curr.volume;
                        bk.vy_min        = min(bk.vy_min, curr.vy);
                        bk.vy_max        = max(bk.vy_max, curr.vy);
                        bk.max_aeration  = max(bk.max_aeration, curr.aeration);
                        max_col_rad      = max(max_col_rad, curr.radius);
                        bk.wet_count++;
                    }
                    if (body.has_previous)
                    {
                        for (const OceanWakeColumn& prev : body.previous)
                        {
                            if (prev.volume > 1e-5f)
                            {
                                splat_gaussian_volume(s, prev.x, prev.z, -prev.volume * dv_scale, prev.radius);
                            }
                        }
                    }
                }

                if (bk.wet_count == 0 || !(bk.total_volume > 1e-5f))
                {
                    continue;
                }

                const float inv_vol = 1.0f / bk.total_volume;
                bk.cx      *= inv_vol;
                bk.cz      *= inv_vol;
                bk.vx       = vx_w * inv_vol;
                bk.vz       = vz_w * inv_vol;
                bk.speed_h  = sqrtf(bk.vx * bk.vx + bk.vz * bk.vz);
                const float max_vy = max(-bk.vy_min, bk.vy_max);
                bk.speed_3  = sqrtf(bk.speed_h * bk.speed_h + max_vy * max_vy);
                if (bk.speed_h > 1e-4f)
                {
                    bk.dir_x = bk.vx / bk.speed_h;
                    bk.dir_z = bk.vz / bk.speed_h;
                }

                // measure true projected half-length and half-beam from the wet columns
                float max_along  = max_col_rad;
                float max_across = max_col_rad;
                for (size_t k = 0; k < body.current.size(); k++)
                {
                    const OceanWakeColumn& curr = body.current[k];
                    const float vm = same_size ? (body.previous[k].volume + (curr.volume - body.previous[k].volume) * f_mid) : curr.volume;
                    if (!(vm > 1e-5f))
                    {
                        continue;
                    }
                    float xm = curr.x;
                    float zm = curr.z;
                    if (same_size)
                    {
                        const float dx = curr.x - body.previous[k].x;
                        const float dz = curr.z - body.previous[k].z;
                        if (dx * dx + dz * dz < 36.0f)
                        {
                            xm = body.previous[k].x + dx * f_mid;
                            zm = body.previous[k].z + dz * f_mid;
                        }
                    }
                    const float rx = xm - bk.cx;
                    const float rz = zm - bk.cz;
                    if (bk.speed_h > 1e-4f)
                    {
                        max_along  = max(max_along,  fabsf( rx * bk.dir_x + rz * bk.dir_z) + curr.radius * 0.7f);
                        max_across = max(max_across, fabsf(-rx * bk.dir_z + rz * bk.dir_x) + curr.radius * 0.7f);
                    }
                    else
                    {
                        const float r = sqrtf(rx * rx + rz * rz) + curr.radius * 0.7f;
                        max_along  = max(max_along,  r);
                        max_across = max(max_across, r);
                    }
                }
                bk.half_len  = max_along;
                bk.half_beam = max_across;

                inject_body_hydrodynamics(s, bk, strength, is_entry);
            }
        }

        void update_sponge(State& s)
        {
            for (int32_t i = 0; i < res; i++)
            {
                const int32_t lx = (i - s.origin_x) & mask;
                const int32_t lz = (i - s.origin_z) & mask;
                const int32_t ex = min(lx, res - 1 - lx);
                const int32_t ez = min(lz, res - 1 - lz);
                const float fx   = ex < sponge_cells ? 1.0f - static_cast<float>(ex) / sponge_cells : 0.0f;
                const float fz   = ez < sponge_cells ? 1.0f - static_cast<float>(ez) / sponge_cells : 0.0f;
                // cubic sponge profile absorbs outgoing waves smoothly with zero reflection
                s.sponge_x[i]    = sponge_rate * fx * fx * fx;
                s.sponge_z[i]    = sponge_rate * fz * fz * fz;
            }
        }

        // Boussinesq dispersive wave equation with viscous shear damping, Stokes breaking,
        // Neumann reflection on obstacles, and two-timescale turbulent foam/bubble transport.
        // Uses 32x32 active tiles, a separate aeration mask (skipping foam/bubble/turbulence work on ~85% wave-only tiles),
        // zero-copyback O(1) buffer swaps, and AVX2+FMA interior SIMD kernels.
        float step(State& s)
        {
            // dilate active wave tiles and active aeration tiles by Chebyshev radius 1
            uint16_t work_tiles[tile_count];
            uint8_t aer_work[tile_count];
            uint32_t work_count = 0u;
            for (int32_t tz = 0; tz < tile_res; tz++)
            {
                const int32_t row_m = ((tz - 1) & tile_mask) * tile_res;
                const int32_t row_0 = tz * tile_res;
                const int32_t row_p = ((tz + 1) & tile_mask) * tile_res;
                for (int32_t tx = 0; tx < tile_res; tx++)
                {
                    const int32_t tx_m = (tx - 1) & tile_mask;
                    const int32_t tx_p = (tx + 1) & tile_mask;
                    const uint8_t any  =
                        s.tile_active[row_m + tx_m] | s.tile_active[row_m + tx] | s.tile_active[row_m + tx_p] |
                        s.tile_active[row_0 + tx_m] | s.tile_active[row_0 + tx] | s.tile_active[row_0 + tx_p] |
                        s.tile_active[row_p + tx_m] | s.tile_active[row_p + tx] | s.tile_active[row_p + tx_p];
                    if (any)
                    {
                        const int32_t t = row_0 + tx;
                        work_tiles[work_count++] = static_cast<uint16_t>(t);
                        aer_work[t] =
                            s.tile_aeration[row_m + tx_m] | s.tile_aeration[row_m + tx] | s.tile_aeration[row_m + tx_p] |
                            s.tile_aeration[row_0 + tx_m] | s.tile_aeration[row_0 + tx] | s.tile_aeration[row_0 + tx_p] |
                            s.tile_aeration[row_p + tx_m] | s.tile_aeration[row_p + tx] | s.tile_aeration[row_p + tx_p];
                    }
                    else
                    {
                        aer_work[row_0 + tx] = 0u;
                    }
                }
            }

            if (work_count == 0u)
            {
                return 0.0f;
            }

            const float dt_dx        = step_dt * inv_cell;
            const float damping      = max(cvar_damping.GetValue(), 0.0f);
            const float decay_foam   = expf(-step_dt / foam_life);
            const float decay_bubble = expf(-step_dt / bubble_life);
            const float decay_turb   = expf(-step_dt / turb_life);
            const int32_t seam_x     = (s.origin_x - 1) & mask;
            const int32_t seam_z     = (s.origin_z - 1) & mask;
            const int32_t seam_tx    = seam_x >> tile_shift;
            const int32_t seam_tz    = seam_z >> tile_shift;

            const float* __restrict height_in   = s.height.data();
            float* __restrict height_out        = s.height_next.data();
            float* __restrict eta_buf           = s.eta_eff.data();
            float* __restrict flux_x_buf        = s.flux_x.data();
            float* __restrict flux_z_buf        = s.flux_z.data();
            float* __restrict flux_x_out        = s.flux_x_next.data();
            float* __restrict flux_z_out        = s.flux_z_next.data();
            const float* __restrict foam_in     = s.foam.data();
            float* __restrict foam_out          = s.foam_next.data();
            const float* __restrict bub_in      = s.bubbles.data();
            float* __restrict bub_out           = s.bubbles_next.data();
            const float* __restrict turb_in     = s.turbulence.data();
            float* __restrict turb_out          = s.turb_next.data();
            const float* __restrict speed2_in   = s.speed2.data();
            const uint8_t* __restrict obs_in    = s.obstacle.data();

            // pass 1: dispersive elliptic pressure potential eta_eff = h + beta * Laplacian(h)
            run_tiles(work_count, [&](uint32_t idx_begin, uint32_t idx_end)
            {
                for (uint32_t idx = idx_begin; idx < idx_end; idx++)
                {
                    const int32_t t  = static_cast<int32_t>(work_tiles[idx]);
                    const int32_t tx = t & tile_mask;
                    const int32_t tz = t >> tile_res_shift;
                    const int32_t x0 = tx << tile_shift;
                    const int32_t z0 = tz << tile_shift;

                    if (tx > 0 && tx < tile_res - 1 && tz > 0 && tz < tile_res - 1 && !s.tile_has_obstacle[t])
                    {
#if defined(SP_OCEAN_WAKE_X64_SIMD)
                        if (g_has_avx2_f16c)
                        {
                            avx2_pass1_tile(height_in, eta_buf, x0, z0);
                            continue;
                        }
#endif
                        for (int32_t z = z0; z < z0 + tile_size; z++)
                        {
                            const int32_t base = z * res + x0;
                            for (int32_t ox = 0; ox < tile_size; ox++)
                            {
                                const int32_t i = base + ox;
                                const float hc  = height_in[i];
                                eta_buf[i]      = hc + dispersion_beta * (height_in[i - 1] + height_in[i + 1] + height_in[i - res] + height_in[i + res] - 4.0f * hc);
                            }
                        }
                    }
                    else
                    {
                        for (int32_t z = z0; z < z0 + tile_size; z++)
                        {
                            const int32_t row  = z * res;
                            const int32_t up   = ((z + 1) & mask) * res;
                            const int32_t down = ((z - 1) & mask) * res;
                            for (int32_t x = x0; x < x0 + tile_size; x++)
                            {
                                const int32_t i = row + x;
                                if (obs_in[i])
                                {
                                    eta_buf[i] = 0.0f;
                                    continue;
                                }
                                const float hc  = height_in[i];
                                const int32_t l = row + ((x - 1) & mask);
                                const int32_t r = row + ((x + 1) & mask);
                                const int32_t d = down + x;
                                const int32_t u = up + x;
                                const float hl  = obs_in[l] ? hc : height_in[l];
                                const float hr  = obs_in[r] ? hc : height_in[r];
                                const float hd  = obs_in[d] ? hc : height_in[d];
                                const float hu  = obs_in[u] ? hc : height_in[u];
                                eta_buf[i]      = hc + dispersion_beta * (hl + hr + hd + hu - 4.0f * hc);
                            }
                        }
                    }
                }
            });

            // pass 2: staggered face flux update with pressure gradient, viscous shear damping, and sponge
            run_tiles(work_count, [&](uint32_t idx_begin, uint32_t idx_end)
            {
                for (uint32_t idx = idx_begin; idx < idx_end; idx++)
                {
                    const int32_t t  = static_cast<int32_t>(work_tiles[idx]);
                    const int32_t tx = t & tile_mask;
                    const int32_t tz = t >> tile_res_shift;
                    const int32_t x0 = tx << tile_shift;
                    const int32_t z0 = tz << tile_shift;

                    if (tx > 0 && tx < tile_res - 1 && tz > 0 && tz < tile_res - 1 && !s.tile_has_obstacle[t] && tx != seam_tx && tz != seam_tz)
                    {
#if defined(SP_OCEAN_WAKE_X64_SIMD)
                        if (g_has_avx2_f16c)
                        {
                            avx2_pass2_tile(eta_buf, flux_x_buf, flux_z_buf, speed2_in, s.sponge_x, s.sponge_z, flux_x_out, flux_z_out, damping, dt_dx, x0, z0);
                            continue;
                        }
#endif
                        for (int32_t z = z0; z < z0 + tile_size; z++)
                        {
                            const int32_t base = z * res + x0;
                            const float sp_z   = s.sponge_z[z];
                            for (int32_t ox = 0; ox < tile_size; ox++)
                            {
                                const int32_t i    = base + ox;
                                const float rate   = damping + max(s.sponge_x[x0 + ox], sp_z);
                                const float retain = max(1.0f - rate * step_dt, 0.0f);
                                const float s_i    = speed2_in[i];
                                const float eta_i  = eta_buf[i];

                                const float east = min(s_i, speed2_in[i + 1]);
                                const float qx   = flux_x_buf[i];
                                const float lapx = flux_x_buf[i - 1] + flux_x_buf[i + 1] + flux_x_buf[i - res] + flux_x_buf[i + res] - 4.0f * qx;
                                flux_x_out[i]    = retain * (qx + visc_flux * lapx - east * dt_dx * (eta_buf[i + 1] - eta_i));

                                const float top  = min(s_i, speed2_in[i + res]);
                                const float qz   = flux_z_buf[i];
                                const float lapz = flux_z_buf[i - 1] + flux_z_buf[i + 1] + flux_z_buf[i - res] + flux_z_buf[i + res] - 4.0f * qz;
                                flux_z_out[i]    = retain * (qz + visc_flux * lapz - top * dt_dx * (eta_buf[i + res] - eta_i));
                            }
                        }
                    }
                    else
                    {
                        for (int32_t z = z0; z < z0 + tile_size; z++)
                        {
                            const int32_t row  = z * res;
                            const int32_t up   = ((z + 1) & mask) * res;
                            const int32_t down = ((z - 1) & mask) * res;
                            const float sp_z   = s.sponge_z[z];
                            for (int32_t x = x0; x < x0 + tile_size; x++)
                            {
                                const int32_t i     = row + x;
                                const int32_t left  = row + ((x - 1) & mask);
                                const int32_t right = row + ((x + 1) & mask);
                                const int32_t north = up + x;
                                const int32_t south = down + x;
                                const float rate    = damping + max(s.sponge_x[x], sp_z);
                                const float retain  = max(1.0f - rate * step_dt, 0.0f);

                                const float east = min(speed2_in[i], speed2_in[right]);
                                if (east > 0.0f && x != seam_x)
                                {
                                    const float qx   = flux_x_buf[i];
                                    const float lapx = flux_x_buf[left] + flux_x_buf[right] + flux_x_buf[south] + flux_x_buf[north] - 4.0f * qx;
                                    const float grad = eta_buf[right] - eta_buf[i];
                                    flux_x_out[i]    = retain * (qx + visc_flux * lapx - east * dt_dx * grad);
                                }
                                else
                                {
                                    flux_x_out[i] = 0.0f;
                                }

                                const float top = min(speed2_in[i], speed2_in[north]);
                                if (top > 0.0f && z != seam_z)
                                {
                                    const float qz   = flux_z_buf[i];
                                    const float lapz = flux_z_buf[left] + flux_z_buf[right] + flux_z_buf[south] + flux_z_buf[north] - 4.0f * qz;
                                    const float grad = eta_buf[north] - eta_buf[i];
                                    flux_z_out[i]    = retain * (qz + visc_flux * lapz - top * dt_dx * grad);
                                }
                                else
                                {
                                    flux_z_out[i] = 0.0f;
                                }
                            }
                        }
                    }
                }
            });

            // O(1) swap: inactive tiles are invariant-zero in both buffers, so no memcpy copyback is needed
            s.flux_x.swap(s.flux_x_next);
            s.flux_z.swap(s.flux_z_next);
            flux_x_buf = s.flux_x.data();
            flux_z_buf = s.flux_z.data();

            // pass 3: height conservation, Stokes breaking, and sparse upwind+diffusive foam/bubble transport
            float tile_peaks[tile_count]    = {};
            float tile_energy[tile_count]   = {};
            float tile_aer_peak[tile_count] = {};
            run_tiles(work_count, [&](uint32_t idx_begin, uint32_t idx_end)
            {
                for (uint32_t idx = idx_begin; idx < idx_end; idx++)
                {
                    const int32_t t  = static_cast<int32_t>(work_tiles[idx]);
                    const int32_t tx = t & tile_mask;
                    const int32_t tz = t >> tile_res_shift;
                    const int32_t x0 = tx << tile_shift;
                    const int32_t z0 = tz << tile_shift;
                    const bool do_aer = aer_work[t] != 0u;
                    float local_peak     = 0.0f;
                    float local_energy   = 0.0f;
                    float local_aer_peak = 0.0f;

                    if (tx > 0 && tx < tile_res - 1 && tz > 0 && tz < tile_res - 1 && !s.tile_has_obstacle[t])
                    {
#if defined(SP_OCEAN_WAKE_X64_SIMD)
                        if (g_has_avx2_f16c)
                        {
                            if (do_aer)
                            {
                                avx2_pass3_full_tile(
                                    height_in, flux_x_buf, flux_z_buf, speed2_in, foam_in, bub_in, turb_in, s.sponge_x, s.sponge_z,
                                    height_out, foam_out, bub_out, turb_out,
                                    dt_dx, decay_foam, decay_bubble, decay_turb, x0, z0, local_peak, local_energy, local_aer_peak
                                );
                            }
                            else
                            {
                                avx2_pass3_wave_only_tile(
                                    height_in, flux_x_buf, flux_z_buf, s.sponge_x, s.sponge_z,
                                    height_out, foam_out, bub_out, turb_out,
                                    dt_dx, x0, z0, local_peak, local_energy, local_aer_peak
                                );
                            }
                            tile_peaks[idx]    = local_peak;
                            tile_energy[idx]   = local_energy;
                            tile_aer_peak[idx] = local_aer_peak;
                            continue;
                        }
#endif
                        for (int32_t z = z0; z < z0 + tile_size; z++)
                        {
                            const int32_t base = z * res + x0;
                            const float sp_z   = s.sponge_z[z];
                            for (int32_t ox = 0; ox < tile_size; ox++)
                            {
                                const int32_t i = base + ox;
                                const float hc  = height_in[i];
                                const float hl  = height_in[i - 1];
                                const float hr  = height_in[i + 1];
                                const float hd  = height_in[i - res];
                                const float hu  = height_in[i + res];

                                const float fx_l   = flux_x_buf[i - 1];
                                const float fx_i   = flux_x_buf[i];
                                const float fz_d   = flux_z_buf[i - res];
                                const float fz_i   = flux_z_buf[i];
                                const float rise   = (fx_l - fx_i + fz_d - fz_i) * dt_dx;
                                const float lap_h  = hl + hr + hd + hu - 4.0f * hc;
                                const float sp     = max(s.sponge_x[x0 + ox], sp_z);
                                const float settle = level_rate + sp * 0.65f;
                                float h_new        = (hc + rise + visc_height * lap_h) * max(1.0f - settle * step_dt, 0.0f);

                                float break_entr = 0.0f;
                                if (h_new > 0.14f && rise > 0.0f)
                                {
                                    const float sx = (hr - hl) * (0.5f * inv_cell);
                                    const float sz = (hu - hd) * (0.5f * inv_cell);
                                    const float s2 = sx * sx + sz * sz;
                                    if (s2 > 0.16f)
                                    {
                                        const float slope = sqrtf(s2);
                                        const float steep = clamp((slope - 0.40f) * 2.2f, 0.0f, 1.0f);
                                        const float crest = clamp((h_new - 0.14f) / 0.18f, 0.0f, 1.0f);
                                        h_new            *= (1.0f - 0.07f * steep);
                                        break_entr        = steep * crest * 0.85f * step_dt;
                                    }
                                }
                                h_new         = soft_limit_height(h_new);
                                height_out[i] = h_new;

                                if (do_aer)
                                {
                                    const float west_in  = max(fx_l,  0.0f);
                                    const float east_in  = max(-fx_i, 0.0f);
                                    const float south_in = max(fz_d,  0.0f);
                                    const float north_in = max(-fz_i, 0.0f);
                                    const float inflow   = west_in + east_in + south_in + north_in;
                                    const float eff_h    = max(speed2_in[i] * inv_gravity, 0.25f);
                                    const float w_adv    = dt_dx / max(eff_h, dt_dx * inflow);
                                    const float stay     = 1.0f - w_adv * inflow;

                                    const float fc       = foam_in[i];
                                    const float adv_foam = fc * stay + w_adv * (west_in * foam_in[i - 1] + east_in * foam_in[i + 1] + south_in * foam_in[i - res] + north_in * foam_in[i + res]);
                                    const float lap_foam = foam_in[i - 1] + foam_in[i + 1] + foam_in[i - res] + foam_in[i + res] - 4.0f * fc;
                                    float f_new          = clamp((adv_foam + foam_diff * lap_foam) * decay_foam, 0.0f, 1.0f);

                                    const float bc       = bub_in[i];
                                    const float adv_bub  = bc * stay + w_adv * (west_in * bub_in[i - 1] + east_in * bub_in[i + 1] + south_in * bub_in[i - res] + north_in * bub_in[i + res]);
                                    const float lap_bub  = bub_in[i - 1] + bub_in[i + 1] + bub_in[i - res] + bub_in[i + res] - 4.0f * bc;
                                    float b_new          = clamp((adv_bub + bubble_diff * lap_bub) * decay_bubble, 0.0f, 1.0f);

                                    const float tc       = turb_in[i];
                                    const float adv_turb = tc * stay + w_adv * (west_in * turb_in[i - 1] + east_in * turb_in[i + 1] + south_in * turb_in[i - res] + north_in * turb_in[i + res]);
                                    const float lap_turb = turb_in[i - 1] + turb_in[i + 1] + turb_in[i - res] + turb_in[i + res] - 4.0f * tc;
                                    float t_new          = clamp((adv_turb + foam_diff * lap_turb) * decay_turb, 0.0f, 1.0f);

                                    if (sp > 0.0f)
                                    {
                                        const float sp_fade = max(1.0f - sp * 1.2f * step_dt, 0.0f);
                                        f_new *= sp_fade;
                                        b_new *= sp_fade;
                                        t_new *= sp_fade;
                                    }

                                    if (break_entr > 0.0f)
                                    {
                                        add_asymptotic(f_new, break_entr);
                                        add_asymptotic(b_new, break_entr * 0.85f);
                                        add_asymptotic(t_new, break_entr * 0.90f);
                                    }

                                    foam_out[i]  = f_new;
                                    bub_out[i]   = b_new;
                                    turb_out[i]  = t_new;

                                    const float aer_mag = max(f_new, max(b_new, t_new));
                                    local_aer_peak      = max(local_aer_peak, aer_mag);
                                    const float vis_mag = max(fabsf(h_new), max(f_new, b_new));
                                    local_peak          = max(local_peak, vis_mag);
                                    local_energy        = max(local_energy, max(vis_mag, max(t_new, max(fabsf(fx_i), fabsf(fz_i)))));
                                }
                                else
                                {
                                    float aer_mag = 0.0f;
                                    if (break_entr > 0.0f)
                                    {
                                        const float fn = min(break_entr, 0.85f);
                                        const float bn = min(break_entr * 0.85f, 0.85f);
                                        const float tn = min(break_entr * 0.90f, 0.85f);
                                        foam_out[i]    = fn;
                                        bub_out[i]     = bn;
                                        turb_out[i]    = tn;
                                        aer_mag        = max(fn, max(bn, tn));
                                        local_aer_peak = max(local_aer_peak, aer_mag);
                                    }
                                    const float vis_mag = max(fabsf(h_new), aer_mag);
                                    local_peak          = max(local_peak, vis_mag);
                                    local_energy        = max(local_energy, max(vis_mag, max(fabsf(fx_i), fabsf(fz_i))));
                                }
                            }
                        }
                    }
                    else
                    {
                        for (int32_t z = z0; z < z0 + tile_size; z++)
                        {
                            const int32_t row  = z * res;
                            const int32_t up   = ((z + 1) & mask) * res;
                            const int32_t down = ((z - 1) & mask) * res;
                            const float sp_z   = s.sponge_z[z];
                            for (int32_t x = x0; x < x0 + tile_size; x++)
                            {
                                const int32_t i = row + x;
                                if (obs_in[i])
                                {
                                    height_out[i] = 0.0f;
                                    if (do_aer)
                                    {
                                        foam_out[i] = 0.0f;
                                        bub_out[i]  = 0.0f;
                                        turb_out[i] = 0.0f;
                                    }
                                    continue;
                                }

                                const int32_t left  = row + ((x - 1) & mask);
                                const int32_t right = row + ((x + 1) & mask);
                                const int32_t south = down + x;
                                const int32_t north = up + x;

                                const float hc = height_in[i];
                                const float hl = obs_in[left]  ? hc : height_in[left];
                                const float hr = obs_in[right] ? hc : height_in[right];
                                const float hd = obs_in[south] ? hc : height_in[south];
                                const float hu = obs_in[north] ? hc : height_in[north];

                                const float fx_l   = flux_x_buf[left];
                                const float fx_i   = flux_x_buf[i];
                                const float fz_d   = flux_z_buf[south];
                                const float fz_i   = flux_z_buf[i];
                                const float rise   = (fx_l - fx_i + fz_d - fz_i) * dt_dx;
                                const float lap_h  = hl + hr + hd + hu - 4.0f * hc;
                                const float sp     = max(s.sponge_x[x], sp_z);
                                const float settle = level_rate + sp * 0.65f;
                                float h_new        = (hc + rise + visc_height * lap_h) * max(1.0f - settle * step_dt, 0.0f);

                                float break_entr = 0.0f;
                                if (h_new > 0.14f && rise > 0.0f)
                                {
                                    const float sx = (hr - hl) * (0.5f * inv_cell);
                                    const float sz = (hu - hd) * (0.5f * inv_cell);
                                    const float s2 = sx * sx + sz * sz;
                                    if (s2 > 0.16f)
                                    {
                                        const float slope = sqrtf(s2);
                                        const float steep = clamp((slope - 0.40f) * 2.2f, 0.0f, 1.0f);
                                        const float crest = clamp((h_new - 0.14f) / 0.18f, 0.0f, 1.0f);
                                        h_new            *= (1.0f - 0.07f * steep);
                                        break_entr        = steep * crest * 0.85f * step_dt;
                                    }
                                }
                                h_new         = soft_limit_height(h_new);
                                height_out[i] = h_new;

                                if (do_aer)
                                {
                                    const float west_in  = max(fx_l,  0.0f);
                                    const float east_in  = max(-fx_i, 0.0f);
                                    const float south_in = max(fz_d,  0.0f);
                                    const float north_in = max(-fz_i, 0.0f);
                                    const float inflow   = west_in + east_in + south_in + north_in;
                                    const float eff_h    = max(speed2_in[i] * inv_gravity, 0.25f);
                                    const float w_adv    = dt_dx / max(eff_h, dt_dx * inflow);
                                    const float stay     = 1.0f - w_adv * inflow;

                                    const float fc       = foam_in[i];
                                    const float adv_foam = fc * stay + w_adv * (west_in * foam_in[left] + east_in * foam_in[right] + south_in * foam_in[south] + north_in * foam_in[north]);
                                    const float lap_foam = foam_in[left] + foam_in[right] + foam_in[south] + foam_in[north] - 4.0f * fc;
                                    float f_new          = clamp((adv_foam + foam_diff * lap_foam) * decay_foam, 0.0f, 1.0f);

                                    const float bc       = bub_in[i];
                                    const float adv_bub  = bc * stay + w_adv * (west_in * bub_in[left] + east_in * bub_in[right] + south_in * bub_in[south] + north_in * bub_in[north]);
                                    const float lap_bub  = bub_in[left] + bub_in[right] + bub_in[south] + bub_in[north] - 4.0f * bc;
                                    float b_new          = clamp((adv_bub + bubble_diff * lap_bub) * decay_bubble, 0.0f, 1.0f);

                                    const float tc       = turb_in[i];
                                    const float adv_turb = tc * stay + w_adv * (west_in * turb_in[left] + east_in * turb_in[right] + south_in * turb_in[south] + north_in * turb_in[north]);
                                    const float lap_turb = turb_in[left] + turb_in[right] + turb_in[south] + turb_in[north] - 4.0f * tc;
                                    float t_new          = clamp((adv_turb + foam_diff * lap_turb) * decay_turb, 0.0f, 1.0f);

                                    if (sp > 0.0f)
                                    {
                                        const float sp_fade = max(1.0f - sp * 1.2f * step_dt, 0.0f);
                                        f_new *= sp_fade;
                                        b_new *= sp_fade;
                                        t_new *= sp_fade;
                                    }

                                    if (break_entr > 0.0f)
                                    {
                                        add_asymptotic(f_new, break_entr);
                                        add_asymptotic(b_new, break_entr * 0.85f);
                                        add_asymptotic(t_new, break_entr * 0.90f);
                                    }

                                    foam_out[i]  = f_new;
                                    bub_out[i]   = b_new;
                                    turb_out[i]  = t_new;

                                    const float aer_mag = max(f_new, max(b_new, t_new));
                                    local_aer_peak      = max(local_aer_peak, aer_mag);
                                    const float vis_mag = max(fabsf(h_new), max(f_new, b_new));
                                    local_peak          = max(local_peak, vis_mag);
                                    local_energy        = max(local_energy, max(vis_mag, max(t_new, max(fabsf(fx_i), fabsf(fz_i)))));
                                }
                                else
                                {
                                    float aer_mag = 0.0f;
                                    if (break_entr > 0.0f)
                                    {
                                        const float fn = min(break_entr, 0.85f);
                                        const float bn = min(break_entr * 0.85f, 0.85f);
                                        const float tn = min(break_entr * 0.90f, 0.85f);
                                        foam_out[i]    = fn;
                                        bub_out[i]     = bn;
                                        turb_out[i]    = tn;
                                        aer_mag        = max(fn, max(bn, tn));
                                        local_aer_peak = max(local_aer_peak, aer_mag);
                                    }
                                    const float vis_mag = max(fabsf(h_new), aer_mag);
                                    local_peak          = max(local_peak, vis_mag);
                                    local_energy        = max(local_energy, max(vis_mag, max(fabsf(fx_i), fabsf(fz_i))));
                                }
                            }
                        }
                    }

                    tile_peaks[idx]    = local_peak;
                    tile_energy[idx]   = local_energy;
                    tile_aer_peak[idx] = local_aer_peak;
                }
            });

            // O(1) swap for height and aeration buffers
            s.height.swap(s.height_next);
            s.foam.swap(s.foam_next);
            s.bubbles.swap(s.bubbles_next);
            s.turbulence.swap(s.turb_next);

            // update tile active/aeration flags and zero only the rare tiles going to sleep (both ping & pong buffers stay clean)
            float* h_a  = s.height.data();
            float* h_b  = s.height_next.data();
            float* fx_a = s.flux_x.data();
            float* fx_b = s.flux_x_next.data();
            float* fz_a = s.flux_z.data();
            float* fz_b = s.flux_z_next.data();
            float* f_a  = s.foam.data();
            float* f_b  = s.foam_next.data();
            float* b_a  = s.bubbles.data();
            float* b_b  = s.bubbles_next.data();
            float* t_a  = s.turbulence.data();
            float* t_b  = s.turb_next.data();

            float peak = 0.0f;
            for (uint32_t idx = 0; idx < work_count; idx++)
            {
                const int32_t t      = static_cast<int32_t>(work_tiles[idx]);
                const bool keep_live = tile_energy[idx] >= tile_sleep_eps;
                const bool keep_aer  = tile_aer_peak[idx] >= tile_sleep_eps;
                peak                 = max(peak, tile_peaks[idx]);

                if (keep_live)
                {
                    s.tile_active[t]     = 1u;
                    s.tile_dirty_pack[t] = 1u;
                    s.tile_dirty_gpu[t] |= 3u;

                    if (keep_aer)
                    {
                        s.tile_aeration[t] = 1u;
                    }
                    else if (aer_work[t] || tile_aer_peak[idx] > 0.0f)
                    {
                        const int32_t x0 = (t & tile_mask) << tile_shift;
                        const int32_t z0 = (t >> tile_res_shift) << tile_shift;
                        for (int32_t z = z0; z < z0 + tile_size; z++)
                        {
                            const size_t off = static_cast<size_t>(z) * res + static_cast<size_t>(x0);
                            memset(f_a + off, 0, tile_size * sizeof(float));
                            memset(f_b + off, 0, tile_size * sizeof(float));
                            memset(b_a + off, 0, tile_size * sizeof(float));
                            memset(b_b + off, 0, tile_size * sizeof(float));
                            memset(t_a + off, 0, tile_size * sizeof(float));
                            memset(t_b + off, 0, tile_size * sizeof(float));
                        }
                        s.tile_aeration[t] = 0u;
                    }
                }
                else if (s.tile_active[t] || tile_energy[idx] > 0.0f || aer_work[t])
                {
                    const int32_t x0 = (t & tile_mask) << tile_shift;
                    const int32_t z0 = (t >> tile_res_shift) << tile_shift;
                    for (int32_t z = z0; z < z0 + tile_size; z++)
                    {
                        const size_t off = static_cast<size_t>(z) * res + static_cast<size_t>(x0);
                        memset(h_a     + off, 0, tile_size * sizeof(float));
                        memset(h_b     + off, 0, tile_size * sizeof(float));
                        memset(eta_buf + off, 0, tile_size * sizeof(float));
                        memset(fx_a    + off, 0, tile_size * sizeof(float));
                        memset(fx_b    + off, 0, tile_size * sizeof(float));
                        memset(fz_a    + off, 0, tile_size * sizeof(float));
                        memset(fz_b    + off, 0, tile_size * sizeof(float));
                        memset(f_a     + off, 0, tile_size * sizeof(float));
                        memset(f_b     + off, 0, tile_size * sizeof(float));
                        memset(b_a     + off, 0, tile_size * sizeof(float));
                        memset(b_b     + off, 0, tile_size * sizeof(float));
                        memset(t_a     + off, 0, tile_size * sizeof(float));
                        memset(t_b     + off, 0, tile_size * sizeof(float));
                    }
                    if (s.tile_active[t])
                    {
                        s.tile_active[t]     = 0u;
                        s.tile_dirty_pack[t] = 1u;
                        s.tile_dirty_gpu[t] |= 3u;
                    }
                    s.tile_aeration[t] = 0u;
                }
            }

            s.needs_upload = true;
            return peak;
        }

        void clear_locked(State& s)
        {
            if (!s.active && !s.origin_valid && s.bodies.empty())
            {
                return;
            }
            if ((s.active || s.origin_valid) && !s.height.empty())
            {
                fill(s.height.begin(), s.height.end(), 0.0f);
                fill(s.height_next.begin(), s.height_next.end(), 0.0f);
                fill(s.eta_eff.begin(), s.eta_eff.end(), 0.0f);
                fill(s.flux_x.begin(), s.flux_x.end(), 0.0f);
                fill(s.flux_z.begin(), s.flux_z.end(), 0.0f);
                fill(s.flux_x_next.begin(), s.flux_x_next.end(), 0.0f);
                fill(s.flux_z_next.begin(), s.flux_z_next.end(), 0.0f);
                fill(s.foam.begin(), s.foam.end(), 0.0f);
                fill(s.foam_next.begin(), s.foam_next.end(), 0.0f);
                fill(s.bubbles.begin(), s.bubbles.end(), 0.0f);
                fill(s.bubbles_next.begin(), s.bubbles_next.end(), 0.0f);
                fill(s.turbulence.begin(), s.turbulence.end(), 0.0f);
                fill(s.turb_next.begin(), s.turb_next.end(), 0.0f);
            }
            memset(s.tile_active,       0, sizeof(s.tile_active));
            memset(s.tile_aeration,     0, sizeof(s.tile_aeration));
            memset(s.tile_has_obstacle, 0, sizeof(s.tile_has_obstacle));
            memset(s.tile_dirty_pack,   0, sizeof(s.tile_dirty_pack));
            memset(s.tile_dirty_gpu,    0, sizeof(s.tile_dirty_gpu));
            s.has_obstacles = false;
            s.bodies.clear();
            s.origin_valid  = false;
            s.accumulator   = 0.0f;
            s.quiet_time    = 0.0f;
            s.upload_zero  |= s.active;
            s.needs_upload  = false;
            s.active        = false;
        }
    }

    float OceanWake::SurfaceInfluence(float water_above_top, float footprint)
    {
        return water_above_top > 0.0f ? expf(-two_pi * water_above_top / max(footprint, cell_size)) : 1.0f;
    }

    bool OceanWake::IsEnabled()
    {
        return cvar_wake.GetValue() != 0;
    }

    void OceanWake::Clear()
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        clear_locked(s);
    }

    void OceanWake::Displace(uint64_t id, const OceanWakeColumn* columns, uint32_t count)
    {
        if (!IsEnabled())
        {
            return;
        }
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (count == 0 || !columns)
        {
            s.bodies.erase(id);
            return;
        }

        Body& body = s.bodies[id];
        if (body.reported && body.current.size() == count)
        {
            // preserve peak plunge/impact speed across multiple fixed physics substeps within the same render frame
            for (uint32_t k = 0; k < count; k++)
            {
                const float prev_vy  = body.current[k].vy;
                const float prev_aer = body.current[k].aeration;
                body.current[k]      = columns[k];
                if (fabsf(prev_vy) > fabsf(body.current[k].vy))
                {
                    body.current[k].vy = prev_vy;
                }
                body.current[k].aeration = max(body.current[k].aeration, prev_aer);
            }
        }
        else
        {
            body.current.assign(columns, columns + count);
        }
        body.reported = true;
        body.idle     = 0;
    }

    void OceanWake::RegisterObstacle(uint64_t id, const OceanWakeObstacle& obstacle)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        s.obstacles[id]   = obstacle;
        s.obstacles_dirty = true;
    }

    void OceanWake::UnregisterObstacle(uint64_t id)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (s.obstacles.erase(id) > 0)
        {
            s.obstacles_dirty = true;
        }
    }

    void OceanWake::Tick(float dt, const Vector3& focus, float sea_level)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (!IsEnabled())
        {
            clear_locked(s);
            return;
        }

        const float radius = (res / 2 + 6) * cell_size;
        bool local_source  = false;
        for (auto it = s.bodies.begin(); it != s.bodies.end();)
        {
            const auto reaches_window = [&](const OceanWakeColumn& column)
            {
                return column.volume > 0.0f && fabsf(column.x - focus.x) <= radius && fabsf(column.z - focus.z) <= radius;
            };
            Body& body = it->second;
            if (!any_of(body.current.begin(), body.current.end(), reaches_window) &&
                !any_of(body.previous.begin(), body.previous.end(), reaches_window))
            {
                it = s.bodies.erase(it);
                continue;
            }
            if (!body.reported && ++body.idle > stale_ticks)
            {
                it = s.bodies.erase(it);
                continue;
            }
            local_source |= body.reported;
            ++it;
        }

        s.active |= local_source;
        if (!s.active)
        {
            return;
        }

        ensure_cells(s);
        move_window(s, focus, sea_level);

        s.accumulator = min(s.accumulator + max(dt, 0.0f), step_dt * steps_max);
        uint32_t substeps = 0;
        {
            float temp = s.accumulator;
            while (temp >= step_dt && substeps < steps_max)
            {
                temp -= step_dt;
                substeps++;
            }
        }

        float peak = 0.0f;
        if (substeps > 0)
        {
            const float sim_dt = substeps * step_dt;
            for (uint32_t k = 0; k < substeps; k++)
            {
                s.accumulator -= step_dt;
                inject_bodies_substep(s, k, substeps, sim_dt);
                peak = max(peak, step(s));
            }

            // only advance previous snapshots after substeps actually consumed the motion delta
            for (auto& entry : s.bodies)
            {
                Body& body = entry.second;
                if (body.reported)
                {
                    body.previous     = body.current;
                    body.has_previous = true;
                    body.reported     = false;
                }
            }
        }

        // a calm sea with nothing disturbing it stops simulating and uploading
        s.quiet_time = (s.bodies.empty() && peak < 0.0015f) ? s.quiet_time + dt : 0.0f;
        if (s.quiet_time > 2.5f)
        {
            clear_locked(s);
        }
    }

    float OceanWake::SampleHeight(float world_x, float world_z)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        return sample_locked(s, world_x, world_z);
    }

    void OceanWake::SampleHeights(const float* xz, float* heights, uint32_t count)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (!s.active || s.height.empty())
        {
            fill_n(heights, count, 0.0f);
            return;
        }
        for (uint32_t i = 0; i < count; i++)
        {
            heights[i] = sample_locked(s, xz[i * 2 + 0], xz[i * 2 + 1]);
        }
    }

    Vector2 OceanWake::SampleVelocity(float world_x, float world_z)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        return sample_velocity_locked(s, world_x, world_z);
    }

    void OceanWake::SampleBatch(const float* xz, float* heights, Vector2* velocities, uint32_t count)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (!s.active || s.height.empty())
        {
            fill_n(heights, count, 0.0f);
            if (velocities)
            {
                fill_n(velocities, count, Vector2::Zero);
            }
            return;
        }
        for (uint32_t i = 0; i < count; i++)
        {
            const float wx = xz[i * 2 + 0];
            const float wz = xz[i * 2 + 1];
            heights[i]     = sample_locked(s, wx, wz);
            if (velocities)
            {
                velocities[i] = sample_velocity_locked(s, wx, wz);
            }
        }
    }

    bool OceanWake::CollectUpload(vector<uint16_t>& texels, uint32_t buffer_index, uint32_t& row_start, uint32_t& row_count)
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        if (s.height.empty() || (!s.active && !s.upload_zero))
        {
            return false;
        }

        const size_t total_cells = static_cast<size_t>(res) * res;
        const bool first_alloc   = texels.size() != total_cells * 4;
        if (first_alloc)
        {
            texels.assign(total_cells * 4, 0u);
            memset(s.tile_dirty_pack, 1u, sizeof(s.tile_dirty_pack));
            memset(s.tile_dirty_gpu,  3u, sizeof(s.tile_dirty_gpu));
            s.needs_upload = true;
        }

        const uint8_t gpu_bit = static_cast<uint8_t>(1u << (buffer_index & 1u));

        if (s.upload_zero)
        {
            fill(texels.begin(), texels.end(), 0u);
            memset(s.tile_dirty_pack, 0u, sizeof(s.tile_dirty_pack));
            for (int32_t t = 0; t < tile_count; t++)
            {
                s.tile_dirty_gpu[t] &= static_cast<uint8_t>(~gpu_bit);
            }
            row_start      = 0u;
            row_count      = static_cast<uint32_t>(res);
            s.upload_zero  = false;
            s.needs_upload = false;
            return true;
        }

        if (!s.needs_upload)
        {
            return false;
        }
        s.needs_upload = false;

        // collect tiles whose CPU FP16 shadow needs repacking and find the dirty row band for this ping-pong texture
        uint16_t pack_tiles[tile_count];
        uint32_t pack_count = 0u;
        int32_t min_tz      = tile_res;
        int32_t max_tz      = -1;

        for (int32_t tz = 0; tz < tile_res; tz++)
        {
            const int32_t row = tz * tile_res;
            for (int32_t tx = 0; tx < tile_res; tx++)
            {
                const int32_t t = row + tx;
                if (s.tile_dirty_pack[t])
                {
                    pack_tiles[pack_count++] = static_cast<uint16_t>(t);
                    s.tile_dirty_pack[t]     = 0u;
                }
                if (s.tile_dirty_gpu[t] & gpu_bit)
                {
                    min_tz = min(min_tz, tz);
                    max_tz = max(max_tz, tz);
                }
            }
        }

        if (pack_count > 0u)
        {
            const float* __restrict h_in = s.height.data();
            const float* __restrict f_in = s.foam.data();
            const float* __restrict b_in = s.bubbles.data();
            const float* __restrict t_in = s.turbulence.data();
            uint16_t* __restrict dst     = texels.data();

            run_tiles(pack_count, [&](uint32_t idx_begin, uint32_t idx_end)
            {
                for (uint32_t idx = idx_begin; idx < idx_end; idx++)
                {
                    const int32_t t  = static_cast<int32_t>(pack_tiles[idx]);
                    const int32_t x0 = (t & tile_mask) << tile_shift;
                    const int32_t z0 = (t >> tile_res_shift) << tile_shift;
#if defined(SP_OCEAN_WAKE_X64_SIMD)
                    if (g_has_avx2_f16c)
                    {
                        avx2_pack_f16_tile(h_in, f_in, b_in, t_in, dst, x0, z0);
                        continue;
                    }
#endif
                    for (int32_t z = z0; z < z0 + tile_size; z++)
                    {
                        const size_t base = static_cast<size_t>(z) * res + static_cast<size_t>(x0);
                        for (int32_t ox = 0; ox < tile_size; ox++)
                        {
                            const size_t i = base + static_cast<size_t>(ox);
                            dst[i * 4 + 0] = vertex_pack::float_to_half(h_in[i]);
                            dst[i * 4 + 1] = vertex_pack::float_to_half(f_in[i]);
                            dst[i * 4 + 2] = vertex_pack::float_to_half(b_in[i]);
                            dst[i * 4 + 3] = vertex_pack::float_to_half(t_in[i]);
                        }
                    }
                }
            });
        }

        if (min_tz > max_tz)
        {
            return false;
        }

        for (int32_t tz = min_tz; tz <= max_tz; tz++)
        {
            const int32_t row = tz * tile_res;
            for (int32_t tx = 0; tx < tile_res; tx++)
            {
                s.tile_dirty_gpu[row + tx] &= static_cast<uint8_t>(~gpu_bit);
            }
        }

        row_start = static_cast<uint32_t>(min_tz << tile_shift);
        row_count = static_cast<uint32_t>((max_tz - min_tz + 1) << tile_shift);
        return true;
    }

    Vector4 OceanWake::GetMapping()
    {
        State& s = state();
        lock_guard<mutex> guard(s.lock);
        return Vector4(s.origin_x * cell_size, s.origin_z * cell_size, 1.0f / window_size, s.active && s.origin_valid ? 1.0f : 0.0f);
    }
}
