// im_anim.cpp — Dear ImGui animation helpers implementation.
// Author: Soufiane KHIAT
// License: MIT
#include "pch.h"
#include "../../Source/Animation/im_anim.h"
#include "../../Source/imgui.h"
#include "../../Source/imgui_internal.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef IM_ANIM_PRE_19200_COMPATIBILITY
	// ImGuiStoragePair is nested in ImGuiStorage in Pre-1.92.0 versions
#define IMGUI_STORAGE_PAIR ImGuiStoragePair
typedef ImFont FontType;
#else
	// ImGuiStoragePair is in the global namespace since ImGui version 1.92.0 (19200)
#define IMGUI_STORAGE_PAIR ImGuiStoragePair
typedef ImFontBaked FontType;
#endif

static FontType* GetBakedFont(ImFont* font, float font_size) {
#ifdef IM_ANIM_PRE_19200_COMPATIBILITY
	(void)font_size; // suppress unused warning
	return font;
#else
	return font->GetFontBaked(font_size);
#endif
}

ImU32 const ZimaBlue = IM_COL32( 91, 194, 231, 255 );
ImU32 const AgedCopper = IM_COL32( 204, 120, 88, 255 );

// ----------------------------------------------------
// Internal: parameterized easing LUT cache (ImPool)
// ----------------------------------------------------
namespace iam_detail {

// ----------------------------------------------------
// Easing constants - named values for clarity
// ----------------------------------------------------

// Bounce easing constants (derived from Robert Penner's equations)
static float const BOUNCE_N1 = 7.5625f;       // Bounce amplitude multiplier
static float const BOUNCE_D1 = 2.75f;         // Bounce timing divisor

// Back easing constants
static float const BACK_OVERSHOOT = 1.70158f;           // Default overshoot amount (~10% overshoot)
static float const BACK_OVERSHOOT_INOUT = 1.70158f * 1.525f;  // Scaled overshoot for in-out

// Elastic easing defaults
static float const ELASTIC_AMPLITUDE = 1.0f;  // Default amplitude
static float const ELASTIC_PERIOD = 0.3f;     // Default period for in/out
static float const ELASTIC_PERIOD_INOUT = 0.45f;  // Period for in-out variant

// Spring physics defaults
static float const SPRING_MASS = 1.0f;        // Default mass
static float const SPRING_STIFFNESS = 120.0f; // Default stiffness (k)
static float const SPRING_DAMPING = 20.0f;    // Default damping (c)

// Floating point comparison epsilon
static float const EASE_EPSILON = 1e-6f;

struct ease_lut {
	iam_ease_desc		desc = {};
	ImVector<float>		samples;
	int			count = 0;
	ease_lut() { count = 0; }
};

struct ease_lut_pool {
	ImPool<ease_lut>	pool;
	ImGuiStorage		map;
	int					sample_count;
	ease_lut_pool() { sample_count = 129; }

	static ImGuiID hash_desc(iam_ease_desc const& d) { return ImHashData(&d, sizeof(d)); }

	static float bounce_out(float t) {
		if (t < 1.0f / BOUNCE_D1) return BOUNCE_N1 * t * t;
		else if (t < 2.0f / BOUNCE_D1) { t -= 1.5f / BOUNCE_D1; return BOUNCE_N1 * t * t + 0.75f; }
		else if (t < 2.5f / BOUNCE_D1) { t -= 2.25f / BOUNCE_D1; return BOUNCE_N1 * t * t + 0.9375f; }
		else { t -= 2.625f / BOUNCE_D1; return BOUNCE_N1 * t * t + 0.984375f; }
	}

	static float elastic_core(float t, float a, float p) {
		if (t == 0.f || t == 1.f) return t;
		float A = (a <= 0.f ? ELASTIC_AMPLITUDE : a);
		float P = (p <= 0.f ? ELASTIC_PERIOD : p);
		float s = (P / IAM_2PI) * asinf(1.0f / A);
		return -(A * ImPow(2.f, 10.f * (t - 1.f)) * ImSin((t - 1.f - s) * IAM_2PI / P));
	}

	static float back_core(float t, float s) { return t * t * ((s + 1.f) * t - s); }

	static float cubic_bezier_y(float x, float x1, float y1, float x2, float y2) {
		float t = x;
		for (int i = 0; i < 5; ++i) {
			float mt = 1.f - t;
			float bx = 3.f*mt*mt*t*x1 + 3.f*mt*t*t*x2 + t*t*t;
			float dx = 3.f*mt*mt*x1 + 6.f*mt*t*(x2 - x1) + 3.f*t*t*(1.f - x2);
			if (dx != 0.f) t = t - (bx - x) / dx;
			if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
		}
		float mt = 1.f - t;
		float by = 3.f*mt*mt*t*y1 + 3.f*mt*t*t*y2 + t*t*t;
		return by;
	}

	static float spring_unit(float u, float mass, float k, float c, float v0) {
		float m = (mass <= 0.f ? 1.f : mass);
		float wn = ImSqrt(k / m);
		float zeta = c / (2.f * ImSqrt(k * m));
		float t = u;
		if (zeta < 1.f) {
			float wdn = wn * ImSqrt(1.f - zeta*zeta);
			float A = 1.f;
			float B = (zeta * wn * A + v0) / wdn;
			float e = expf(-zeta * wn * t);
			return 1.f - e * (A * ImCos(wdn*t) + B * ImSin(wdn*t));
		} else if (zeta == 1.f) {
			float e = expf(-wn * t);
			return 1.f - e * (1.f + wn * t);
		} else {
			float wd = wn * ImSqrt(zeta*zeta - 1.f);
			float e1 = expf(-(zeta * wn - wd) * t);
			float e2 = expf(-(zeta * wn + wd) * t);
			return 1.f - 0.5f*(e1 + e2);
		}
	}

	void build_lut(ease_lut& lut) {
		iam_ease_desc const& d = lut.desc;
		lut.samples.resize(sample_count);
		for (int i = 0; i < sample_count; ++i) {
			float x = (float)i / (float)(sample_count - 1);
			float y = x;
			switch (d.type) {
				case iam_ease_cubic_bezier:
					y = cubic_bezier_y(x, d.p0, d.p1, d.p2, d.p3);
					break;
				case iam_ease_steps: {
					int n = (int)(d.p0 < 1.f ? 1.f : d.p0);
					int mode = (int)d.p1;
					if (mode == 1)		y = ImFloor(x * n + EASE_EPSILON) / (float)n;
					else if (mode == 2)	{ y = (ImFloor(x * n - 0.5f + EASE_EPSILON) + 0.5f) / (float)n; if (y<0) y=0; if (y>1) y=1; }
					else				y = ImFloor(x * n + EASE_EPSILON) / (float)n;
					break;
				}
				case iam_ease_in_elastic: {
					float a = (d.p0 <= 0.f ? ELASTIC_AMPLITUDE : d.p0), p = (d.p1 <= 0.f ? ELASTIC_PERIOD : d.p1);
					y = 1.f + elastic_core(1.f - x, a, p);
					break;
				}
				case iam_ease_out_elastic: {
					float a = (d.p0 <= 0.f ? ELASTIC_AMPLITUDE : d.p0), p = (d.p1 <= 0.f ? ELASTIC_PERIOD : d.p1);
					y = 1.f - elastic_core(x, a, p);
					break;
				}
				case iam_ease_in_out_elastic: {
					float a = (d.p0 <= 0.f ? ELASTIC_AMPLITUDE : d.p0), p = (d.p1 <= 0.f ? ELASTIC_PERIOD_INOUT : d.p1);
					if (x < 0.5f) y = 0.5f * (1.f + elastic_core(1.f - 2.f*x, a, p));
					else y = 0.5f * (1.f - elastic_core(2.f*x - 1.f, a, p)) + 0.5f;
					break;
				}
				case iam_ease_in_back: {
					float s = (d.p0 == 0.f ? BACK_OVERSHOOT : d.p0);
					y = back_core(x, s);
					break;
				}
				case iam_ease_out_back: {
					float s = (d.p0 == 0.f ? BACK_OVERSHOOT : d.p0);
					y = 1.f - back_core(1.f - x, s);
					break;
				}
				case iam_ease_in_out_back: {
					float s = (d.p0 == 0.f ? BACK_OVERSHOOT_INOUT : d.p0);
					if (x < 0.5f) y = 0.5f * back_core(2.f*x, s);
					else y = 1.f - 0.5f * back_core(2.f*(1.f - x), s);
					break;
				}
				case iam_ease_in_bounce:
					y = 1.f - bounce_out(1.f - x);
					break;
				case iam_ease_out_bounce:
					y = bounce_out(x);
					break;
				case iam_ease_in_out_bounce:
					y = (x < 0.5f) ? (0.5f * (1.f - bounce_out(1.f - 2.f*x))) : (0.5f * bounce_out(2.f*x - 1.f) + 0.5f);
					break;
				case iam_ease_spring:
					y = spring_unit(x, (d.p0<=0.f ? SPRING_MASS : d.p0), (d.p1<=0.f ? SPRING_STIFFNESS : d.p1), (d.p2<=0.f ? SPRING_DAMPING : d.p2), d.p3);
					break;
				default: y = x; break;
			}
			//if (y < 0.f) y = 0.f; if (y > 1.f) y = 1.f;
			lut.samples[i] = y;
		}
	}

	float eval_lut(iam_ease_desc const& d, float t) {
		if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
		ImGuiID key = hash_desc(d);
		int idx = map.GetInt(key, -1);
		ease_lut* lut;
		if (idx == -1) {
			lut = pool.GetOrAddByKey(key);
			lut->desc = d;
			build_lut(*lut);
			map.SetInt(key, pool.GetIndex(lut));
		} else {
			lut = pool.GetByIndex(idx);
		}
		float fi = t * (float)(sample_count - 1);
		int i0 = (int)fi;
		int i1 = i0 + 1;
		if (i1 >= sample_count) i1 = sample_count - 1;
		float frac = fi - (float)i0;
		return lut->samples[i0] + (lut->samples[i1] - lut->samples[i0]) * frac;
	}
};

static ease_lut_pool& ease_lut_pool_singleton() { static ease_lut_pool S; return S; }

// Forward declaration for custom easing (defined later with other globals)
static iam_ease_fn g_custom_ease[16];

// ----------------------------------------------------
// Easing implementation - base functions + transforms
// ----------------------------------------------------

// Easing families (groups of in/out/in_out)
enum ease_family { ease_quad, ease_cubic, ease_quart, ease_quint, ease_sine, ease_expo, ease_circ, ease_back, ease_elastic, ease_bounce };
enum ease_variant { ease_in, ease_out, ease_in_out };

// Derived constants for standalone easing functions
static float const EASE_PI = 3.1415926535f;
static float const BACK_C1 = BACK_OVERSHOOT;  // Alias for back easing formula
static float const BACK_C3 = BACK_C1 + 1.f;   // Back easing cubic coefficient
static float const ELASTIC_C4 = (2.f * EASE_PI) / 3.f;  // Elastic angular frequency

// Base "in" easing functions - all others derived via transforms
static float ease_in_quad(float t)    { return t * t; }
static float ease_in_cubic(float t)   { return t * t * t; }
static float ease_in_quart(float t)   { return t * t * t * t; }
static float ease_in_quint(float t)   { return t * t * t * t * t; }
static float ease_in_sine(float t)    { return 1.f - ImCos((t * EASE_PI) / 2.f); }
static float ease_in_expo(float t)    { return (t == 0.f) ? 0.f : ImPow(2.f, 10.f * t - 10.f); }
static float ease_in_circ(float t)    { return 1.f - ImSqrt(1.f - t * t); }
static float ease_in_back(float t)    { return BACK_C3 * t * t * t - BACK_C1 * t * t; }
static float ease_in_elastic(float t) { return (t == 0.f || t == 1.f) ? t : -ImPow(2.f, 10.f * t - 10.f) * ImSin((t * 10.f - 10.75f) * ELASTIC_C4); }

// Bounce is naturally defined as "out" - special case
static float ease_out_bounce(float t) {
	if (t < 1.f / BOUNCE_D1)     return BOUNCE_N1 * t * t;
	if (t < 2.f / BOUNCE_D1)     { t -= 1.5f / BOUNCE_D1;   return BOUNCE_N1 * t * t + 0.75f; }
	if (t < 2.5f / BOUNCE_D1)    { t -= 2.25f / BOUNCE_D1;  return BOUNCE_N1 * t * t + 0.9375f; }
	t -= 2.625f / BOUNCE_D1; return BOUNCE_N1 * t * t + 0.984375f;
}

// Evaluate base "in" function by family
static float eval_ease_in(int family, float t) {
	switch (family) {
		case ease_quad:    return ease_in_quad(t);
		case ease_cubic:   return ease_in_cubic(t);
		case ease_quart:   return ease_in_quart(t);
		case ease_quint:   return ease_in_quint(t);
		case ease_sine:    return ease_in_sine(t);
		case ease_expo:    return ease_in_expo(t);
		case ease_circ:    return ease_in_circ(t);
		case ease_back:    return ease_in_back(t);
		case ease_elastic: return ease_in_elastic(t);
		case ease_bounce:  return 1.f - ease_out_bounce(1.f - t);  // bounce defined as out
		default:           return t;
	}
}

// Transform: in -> out  =>  out(t) = 1 - in(1 - t)
static float transform_out(int family, float t) {
	return 1.f - eval_ease_in(family, 1.f - t);
}

// Transform: in -> in_out  =>  in_out(t) = t < 0.5 ? in(2t)/2 : 1 - in(2-2t)/2
static float transform_in_out(int family, float t) {
	return (t < 0.5f)
		? eval_ease_in(family, 2.f * t) / 2.f
		: 1.f - eval_ease_in(family, 2.f - 2.f * t) / 2.f;
}

static float eval_preset_internal(int type, float t) {
	t = ImClamp(t, 0.f, 1.f);
	if (type == iam_ease_linear) return t;

	// Decompose type into family and variant
	// Enum layout: linear=0, then groups of 3 (in, out, in_out) for each family
	int idx = type - 1;  // 0-based index after linear
	int family = idx / 3;
	int variant = idx % 3;

	switch (variant) {
		case ease_in:     return eval_ease_in(family, t);
		case ease_out:    return transform_out(family, t);
		case ease_in_out: return transform_in_out(family, t);
		default:          return t;
	}
}

static float eval(iam_ease_desc const& d, float t) {
	switch (d.type) {
		case iam_ease_linear:
		case iam_ease_in_quad:    case iam_ease_out_quad:    case iam_ease_in_out_quad:
		case iam_ease_in_cubic:   case iam_ease_out_cubic:   case iam_ease_in_out_cubic:
		case iam_ease_in_quart:   case iam_ease_out_quart:   case iam_ease_in_out_quart:
		case iam_ease_in_quint:   case iam_ease_out_quint:   case iam_ease_in_out_quint:
		case iam_ease_in_sine:    case iam_ease_out_sine:    case iam_ease_in_out_sine:
		case iam_ease_in_expo:    case iam_ease_out_expo:    case iam_ease_in_out_expo:
		case iam_ease_in_circ:    case iam_ease_out_circ:    case iam_ease_in_out_circ:
		case iam_ease_in_back:    case iam_ease_out_back:    case iam_ease_in_out_back:
		case iam_ease_in_elastic: case iam_ease_out_elastic: case iam_ease_in_out_elastic:
		case iam_ease_in_bounce:  case iam_ease_out_bounce:  case iam_ease_in_out_bounce:
			return eval_preset_internal(d.type, t);
		case iam_ease_custom: {
			int slot = (int)d.p0;
			if (slot >= 0 && slot < 16 && g_custom_ease[slot]) {
				if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
				return g_custom_ease[slot](t);
			}
			return t; // fallback to linear if no callback registered
		}
		default:
			return ease_lut_pool_singleton().eval_lut(d, t);
	}
}

// ----------------------------------------------------
// Color conversions & blending spaces
// ----------------------------------------------------
namespace color {

static float srgb_to_linear1(float c) { return (c <= 0.04045f) ? (c / 12.92f) : ImPow((c + 0.055f) / 1.055f, 2.4f); }
static float linear_to_srgb1(float c) { return (c <= 0.0031308f) ? (12.92f * c) : (1.055f * ImPow(c, 1.f/2.4f) - 0.055f); }

static ImVec4 srgb_to_linear(ImVec4 c) { return ImVec4(srgb_to_linear1(c.x), srgb_to_linear1(c.y), srgb_to_linear1(c.z), c.w); }
static ImVec4 linear_to_srgb(ImVec4 c) { return ImVec4(linear_to_srgb1(c.x), linear_to_srgb1(c.y), linear_to_srgb1(c.z), c.w); }

static ImVec4 hsv_to_srgb(ImVec4 hsv) {
	float H=hsv.x, S=hsv.y, V=hsv.z, A=hsv.w;
	if (S <= 0.f) return ImVec4(V,V,V,A);
	H = ImFmod(H, 1.f); if (H < 0.f) H += 1.f;
	float h = H * 6.f;
	int   i = (int)ImFloor(h);
	float f = h - (float)i;
	float p = V * (1.f - S);
	float q = V * (1.f - S * f);
	float t = V * (1.f - S * (1.f - f));
	float r,g,b;
	switch (i % 6) {
		case 0: r=V; g=t; b=p; break;
		case 1: r=q; g=V; b=p; break;
		case 2: r=p; g=V; b=t; break;
		case 3: r=p; g=q; b=V; break;
		case 4: r=t; g=p; b=V; break;
		default:r=V; g=p; b=q; break;
	}
	return ImVec4(r,g,b,A);
}

static ImVec4 srgb_to_hsv(ImVec4 c) {
	float r=c.x, g=c.y, b=c.z, a=c.w;
	float mx = r > g ? r : g; if (b > mx) mx = b;
	float mn = r < g ? r : g; if (b < mn) mn = b;
	float d = mx - mn;
	float h = 0.f, s = (mx == 0.f) ? 0.f : d / mx, v = mx;
	if (d != 0.f) {
		if (mx == r) h = ImFmod((g - b) / d, 6.f);
		else if (mx == g) h = (b - r) / d + 2.f;
		else h = (r - g) / d + 4.f;
		h /= 6.f; if (h < 0.f) h += 1.f;
	}
	return ImVec4(h,s,v,a);
}

static ImVec4 srgb_to_oklab(ImVec4 c) {
	ImVec4 l = srgb_to_linear(c);
	float lR=l.x, lG=l.y, lB=l.z;
	float l_ = 0.4122214708f*lR + 0.5363325363f*lG + 0.0514459929f*lB;
	float m_ = 0.2119034982f*lR + 0.6806995451f*lG + 0.1073969566f*lB;
	float s_ = 0.0883024619f*lR + 0.2817188376f*lG + 0.6299787005f*lB;
	float l_c = cbrtf(l_), m_c = cbrtf(m_), s_c = cbrtf(s_);
	float L = 0.2104542553f*l_c + 0.7936177850f*m_c - 0.0040720468f*s_c;
	float A = 1.9779984951f*l_c - 2.4285922050f*m_c + 0.4505937099f*s_c;
	float B = 0.0259040371f*l_c + 0.7827717662f*m_c - 0.8086757660f*s_c;
	return ImVec4(L,A,B,c.w);
}

static ImVec4 oklab_to_srgb(ImVec4 L) {
	float l = L.x, a = L.y, b = L.z;
	float l_c = l + 0.3963377774f*a + 0.2158037573f*b;
	float m_c = l - 0.1055613458f*a - 0.0638541728f*b;
	float s_c = l - 0.0894841775f*a - 1.2914855480f*b;
	float l3 = l_c*l_c*l_c, m3 = m_c*m_c*m_c, s3 = s_c*s_c*s_c;
	float R = +4.0767416621f*l3 - 3.3077115913f*m3 + 0.2309699292f*s3;
	float G = -1.2684380046f*l3 + 2.6097574011f*m3 - 0.3413193965f*s3;
	float B = -0.0041960863f*l3 - 0.7034186147f*m3 + 1.7076147010f*s3;
	// Clamp to valid linear sRGB range to avoid NaN from ImPow with negative values
	R = ImClamp(R, 0.0f, 1.0f);
	G = ImClamp(G, 0.0f, 1.0f);
	B = ImClamp(B, 0.0f, 1.0f);
	return linear_to_srgb(ImVec4(R,G,B,L.w));
}

// OKLCH: cylindrical form of OKLAB (L=lightness, C=chroma, H=hue)
static ImVec4 oklab_to_oklch(ImVec4 lab) {
	float L = lab.x, a = lab.y, b = lab.z;
	float C = ImSqrt(a*a + b*b);
	float H = atan2f(b, a) / (2.0f * 3.1415926535f);  // normalize to 0-1
	if (H < 0.0f) H += 1.0f;
	return ImVec4(L, C, H, lab.w);
}

static ImVec4 oklch_to_oklab(ImVec4 lch) {
	float L = lch.x, C = lch.y, H = lch.z;
	float h_rad = H * 2.0f * 3.1415926535f;
	float a = C * ImCos(h_rad);
	float b = C * ImSin(h_rad);
	return ImVec4(L, a, b, lch.w);
}

static ImVec4 srgb_to_oklch(ImVec4 c) { return oklab_to_oklch(srgb_to_oklab(c)); }
static ImVec4 oklch_to_srgb(ImVec4 c) { return oklab_to_srgb(oklch_to_oklab(c)); }

// Convert sRGB to a target color space
static ImVec4 to_space(ImVec4 srgb, int space) {
	switch (space) {
		case iam_col_srgb_linear: return srgb_to_linear(srgb);
		case iam_col_hsv:         return srgb_to_hsv(srgb);
		case iam_col_oklab:       return srgb_to_oklab(srgb);
		case iam_col_oklch:       return srgb_to_oklch(srgb);
		default:                  return srgb;  // iam_col_srgb
	}
}

// Convert from a color space back to sRGB
static ImVec4 from_space(ImVec4 c, int space) {
	switch (space) {
		case iam_col_srgb_linear: return linear_to_srgb(c);
		case iam_col_hsv:         return hsv_to_srgb(c);
		case iam_col_oklab:       return oklab_to_srgb(c);
		case iam_col_oklch:       return oklch_to_srgb(c);
		default:                  return c;  // iam_col_srgb
	}
}

static float lerp1(float a, float b, float t) { return a + (b - a) * t; }
static ImVec4 lerp4(ImVec4 a, ImVec4 b, float t) { return ImVec4(lerp1(a.x,b.x,t), lerp1(a.y,b.y,t), lerp1(a.z,b.z,t), lerp1(a.w,b.w,t)); }

static ImVec4 lerp_color(ImVec4 a_srgb, ImVec4 b_srgb, float t, int space) {
	switch (space) {
		case iam_col_srgb_linear: {
			ImVec4 A = srgb_to_linear(a_srgb), B = srgb_to_linear(b_srgb);
			return linear_to_srgb(lerp4(A,B,t));
		}
		case iam_col_hsv: {
			ImVec4 A = srgb_to_hsv(a_srgb), B = srgb_to_hsv(b_srgb);
			float dh = ImFmod(B.x - A.x + 1.5f, 1.f) - 0.5f;
			ImVec4 H = ImVec4(A.x + dh * t, lerp1(A.y,B.y,t), lerp1(A.z,B.z,t), lerp1(A.w,B.w,t));
			if (H.x < 0.f) H.x += 1.f; if (H.x > 1.f) H.x -= 1.f;
			return hsv_to_srgb(H);
		}
		case iam_col_oklab: {
			ImVec4 A = srgb_to_oklab(a_srgb), B = srgb_to_oklab(b_srgb);
			return oklab_to_srgb(lerp4(A,B,t));
		}
		case iam_col_oklch: {
			ImVec4 A = srgb_to_oklch(a_srgb), B = srgb_to_oklch(b_srgb);
			// L,C interpolate linearly; H uses shortest arc
			float dh = ImFmod(B.z - A.z + 1.5f, 1.f) - 0.5f;
			ImVec4 LCH = ImVec4(lerp1(A.x,B.x,t), lerp1(A.y,B.y,t), A.z + dh * t, lerp1(A.w,B.w,t));
			if (LCH.z < 0.f) LCH.z += 1.f; if (LCH.z > 1.f) LCH.z -= 1.f;
			return oklch_to_srgb(LCH);
		}
		default:
			return lerp4(a_srgb, b_srgb, t);
	}
}

} // namespace color

// ----------------------------------------------------
// Channel state (per key) and pools (ImPool)
// ----------------------------------------------------

static ImGuiID make_key(ImGuiID id, ImGuiID ch) {
	struct { ImGuiID a, b; } k = { id, ch };
	return ImHashData(&k, sizeof(k));
}

// Forward declare global time for channels to use
static double g_global_time = 0.0;

// Minimum duration to avoid division by zero
static float const MIN_DURATION = 1e-6f;

// ----------------------------------------------------
// Channel interpolation traits - define how each type lerps
// ----------------------------------------------------
template<typename T> struct chan_traits;

template<> struct chan_traits<float> {
	static float default_value() { return 0.0f; }
	static float lerp(float a, float b, float k) { return a + (b - a) * k; }
};

template<> struct chan_traits<ImVec2> {
	static ImVec2 default_value() { return ImVec2(0, 0); }
	static ImVec2 lerp(ImVec2 a, ImVec2 b, float k) {
		return ImVec2(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k);
	}
};

template<> struct chan_traits<ImVec4> {
	static ImVec4 default_value() { return ImVec4(1, 1, 1, 1); }
	static ImVec4 lerp(ImVec4 a, ImVec4 b, float k) {
		return ImVec4(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k,
		              a.z + (b.z - a.z) * k, a.w + (b.w - a.w) * k);
	}
};

template<> struct chan_traits<int> {
	static int default_value() { return 0; }
	static int lerp(int a, int b, float k) {
		float v = (float)a + ((float)b - (float)a) * k;
		return (int)ImFloor(v + 0.5f);
	}
};

// ----------------------------------------------------
// Base channel template - shared logic for all channel types
// ----------------------------------------------------
template<typename T>
struct base_chan {
	T		current, start, target;
	float	dur, t;  // t is cached progress for backward compatibility
	double	start_time;
	iam_ease_desc ez;
	int		policy;
	unsigned last_seen_frame;
	unsigned has_pending;
	unsigned sleeping;
	T		pending_target;

	base_chan() {
		current = chan_traits<T>::default_value();
		start = current;
		target = current;
		pending_target = chan_traits<T>::default_value();
		dur = MIN_DURATION;
		t = 1.0f;
		start_time = 0;
		ez = { iam_ease_out_cubic, 0, 0, 0, 0 };
		policy = iam_policy_crossfade;
		last_seen_frame = 0;
		has_pending = 0;
		sleeping = 1;
	}

	void set(T trg, float d, iam_ease_desc const& e, int pol) {
		start = current;
		target = trg;
		dur = (d <= MIN_DURATION ? MIN_DURATION : d);
		start_time = g_global_time;
		t = 0;
		ez = e;
		policy = pol;
		sleeping = 0;
	}

	float progress() {
		if (sleeping) { t = 1.0f; return 1.0f; }
		t = (float)((g_global_time - start_time) / dur);
		if (t < 0.f) t = 0.f;
		else if (t > 1.f) t = 1.f;
		return t;
	}

	T evaluate() {
		if (sleeping) return current;
		progress();
		if (t >= 1.f) { current = target; sleeping = 1; return current; }
		float k = eval(ez, t);
		current = chan_traits<T>::lerp(start, target, k);
		return current;
	}

	void tick(float) { evaluate(); }
};

// Concrete channel types using the base template
using float_chan = base_chan<float>;
using vec2_chan = base_chan<ImVec2>;
using vec4_chan = base_chan<ImVec4>;
using int_chan = base_chan<int>;

// Color channel needs special handling for color space blending
struct color_chan {
	ImVec4	current, start, target;
	float	dur, t;
	double	start_time;
	iam_ease_desc ez;
	int		policy;
	int		space;
	unsigned last_seen_frame;
	unsigned sleeping;

	color_chan() {
		current = ImVec4(1, 1, 1, 1);
		start = current;
		target = current;
		dur = MIN_DURATION;
		t = 1.0f;
		start_time = 0;
		ez = { iam_ease_out_cubic, 0, 0, 0, 0 };
		policy = iam_policy_crossfade;
		space = iam_col_srgb_linear;
		last_seen_frame = 0;
		sleeping = 1;
	}

	void set(ImVec4 trg, float d, iam_ease_desc const& e, int pol, int sp) {
		start = current;
		target = trg;
		dur = (d <= MIN_DURATION ? MIN_DURATION : d);
		start_time = g_global_time;
		t = 0;
		ez = e;
		policy = pol;
		space = sp;
		sleeping = 0;
	}

	float progress() {
		if (sleeping) { t = 1.0f; return 1.0f; }
		t = (float)((g_global_time - start_time) / dur);
		if (t < 0.f) t = 0.f;
		else if (t > 1.f) t = 1.f;
		return t;
	}

	ImVec4 evaluate() {
		if (sleeping) return current;
		progress();
		if (t >= 1.f) { current = target; sleeping = 1; return current; }
		float k = eval(ez, t);
		current = color::lerp_color(start, target, k, space);
		return current;
	}

	void tick(float) { evaluate(); }
};

// Per-type pools
template<typename T>
struct pool_t {
	ImPool<T> pool;
	unsigned frame = 0;
	void begin() { ++frame; }
	T* get(ImGuiID key) { T* c = pool.GetOrAddByKey(key); c->last_seen_frame = frame; return c; }
	T* try_get(ImGuiID key) { return pool.GetByKey(key); }  // Returns nullptr if not found
	bool exists(ImGuiID key) { return pool.GetByKey(key) != nullptr; }
	void gc(unsigned max_age) {
		for (int i = 0; i < pool.GetMapSize(); ++i) {
			if (T* c = pool.TryGetMapData(i)) {
				if (frame - c->last_seen_frame > max_age) {
					ImGuiID k = pool.Map.Data[i].key;
					pool.Remove(k, pool.GetIndex(c));
				}
			}
		}
	}
};

static pool_t<float_chan> g_float;
static pool_t<vec2_chan>  g_vec2;
static pool_t<vec4_chan>  g_vec4;
static pool_t<int_chan>   g_int;
static pool_t<color_chan> g_color;

// Global time scale for slow-motion / fast-forward
static float g_time_scale = 1.0f;

// Global frame counter for oscillators and procedural animations
static unsigned g_frame = 0;

// Lazy initialization - defer channel creation until animation is needed
static bool g_lazy_init_enabled = true;

// Note: g_custom_ease is forward-declared earlier and initialized to nullptr here

// ----------------------------------------------------
// Profiler data structures
// ----------------------------------------------------
static int const PROFILER_MAX_SECTIONS = 64;
static int const PROFILER_MAX_STACK = 16;
static int const PROFILER_HISTORY_SIZE = 120; // 2 seconds at 60fps

struct profiler_section {
	char name[64];
	double start_time;
	double accumulated_time;  // Total time this frame
	int call_count;           // Number of calls this frame
	float history[PROFILER_HISTORY_SIZE];  // History for graph
	int history_idx;
	bool active;

	profiler_section() : start_time(0), accumulated_time(0), call_count(0), history_idx(0), active(false) {
		name[0] = '\0';
		for (int i = 0; i < PROFILER_HISTORY_SIZE; i++) history[i] = 0.0f;
	}
};

struct profiler_state {
	bool enabled = false;
	double frame_start_time = 0;
	double frame_total_time = 0;
	float frame_history[PROFILER_HISTORY_SIZE] = {};
	int frame_history_idx = 0;
	profiler_section sections[PROFILER_MAX_SECTIONS] = {};
	int section_count = 0;
	int stack[PROFILER_MAX_STACK] = {};
	int stack_depth = 0;

	profiler_state() {
		for (int i = 0; i < PROFILER_HISTORY_SIZE; i++) frame_history[i] = 0.0f;
	}

	int find_or_create_section(char const* name) {
		// Find existing section
		for (int i = 0; i < section_count; i++) {
			if (strcmp(sections[i].name, name) == 0) return i;
		}
		// Create new section
		if (section_count >= PROFILER_MAX_SECTIONS) return -1;
		int idx = section_count++;
                strncpy_s(sections[idx].name, sizeof(sections[idx].name), name, _TRUNCATE);
		sections[idx].name[63] = '\0';
		sections[idx].active = true;
		return idx;
	}
};

static profiler_state g_profiler;

static double get_time_ms() {
#ifdef _WIN32
	static bool freq_initialized = false;
	static double inv_freq = 0.0;
	if (!freq_initialized) {
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		inv_freq = 1000.0 / (double)freq.QuadPart;
		freq_initialized = true;
	}
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return (double)counter.QuadPart * inv_freq;
#else
	return (double)ImGui::GetTime() * 1000.0;
#endif
}

} // namespace iam_detail

// Forward declaration for scroll update
static void iam_scroll_update_internal(float dt);

// ----------------------------------------------------
// Public API implementations
// ----------------------------------------------------

void iam_update_begin_frame() {
	iam_detail::g_float.begin();
	iam_detail::g_vec2.begin();
	iam_detail::g_vec4.begin();
	iam_detail::g_int.begin();
	iam_detail::g_color.begin();
	iam_detail::g_frame++;
	// Accumulate global time (scaled)
	iam_detail::g_global_time += ImGui::GetIO().DeltaTime * iam_detail::g_time_scale;
	iam_scroll_update_internal(ImGui::GetIO().DeltaTime);
}

void iam_gc(unsigned int max_age_frames) {
	iam_detail::g_float.gc(max_age_frames);
	iam_detail::g_vec2.gc(max_age_frames);
	iam_detail::g_vec4.gc(max_age_frames);
	iam_detail::g_int.gc(max_age_frames);
	iam_detail::g_color.gc(max_age_frames);
}

void iam_pool_clear() {
	// Clean up all pools
	iam_detail::g_float.pool.Clear();
	iam_detail::g_vec2.pool.Clear();
	iam_detail::g_vec4.pool.Clear();
	iam_detail::g_int.pool.Clear();
	iam_detail::g_color.pool.Clear();
}

void iam_reserve(int cap_float, int cap_vec2, int cap_vec4, int cap_int, int cap_color) {
	if (cap_float  > 0) iam_detail::g_float.pool.Reserve(cap_float);
	if (cap_vec2   > 0) iam_detail::g_vec2.pool.Reserve(cap_vec2);
	if (cap_vec4   > 0) iam_detail::g_vec4.pool.Reserve(cap_vec4);
	if (cap_int    > 0) iam_detail::g_int.pool.Reserve(cap_int);
	if (cap_color  > 0) iam_detail::g_color.pool.Reserve(cap_color);
}

void iam_set_ease_lut_samples(int count) {
	if (count < 9) count = 9;
	iam_detail::ease_lut_pool_singleton().sample_count = count;
}

void iam_set_global_time_scale(float scale) {
	iam_detail::g_time_scale = scale > 0.0f ? scale : 0.0f;
}

float iam_get_global_time_scale() {
	return iam_detail::g_time_scale;
}

// ----------------------------------------------------
// Lazy Initialization API implementations
// ----------------------------------------------------

void iam_set_lazy_init(bool enable) {
	iam_detail::g_lazy_init_enabled = enable;
}

bool iam_is_lazy_init_enabled() {
	return iam_detail::g_lazy_init_enabled;
}

// ----------------------------------------------------
// Profiler API implementations
// ----------------------------------------------------

void iam_profiler_enable(bool enable) {
	iam_detail::g_profiler.enabled = enable;
	if (enable) {
		// Reset all sections when enabling
		for (int i = 0; i < iam_detail::g_profiler.section_count; i++) {
			iam_detail::g_profiler.sections[i].accumulated_time = 0;
			iam_detail::g_profiler.sections[i].call_count = 0;
		}
	}
}

bool iam_profiler_is_enabled() {
	return iam_detail::g_profiler.enabled;
}

void iam_profiler_begin_frame() {
	if (!iam_detail::g_profiler.enabled) return;
	iam_detail::g_profiler.frame_start_time = iam_detail::get_time_ms();
	iam_detail::g_profiler.stack_depth = 0;
	// Reset per-frame accumulators
	for (int i = 0; i < iam_detail::g_profiler.section_count; i++) {
		iam_detail::g_profiler.sections[i].accumulated_time = 0;
		iam_detail::g_profiler.sections[i].call_count = 0;
	}
}

void iam_profiler_end_frame() {
	if (!iam_detail::g_profiler.enabled) return;
	double end_time = iam_detail::get_time_ms();
	iam_detail::g_profiler.frame_total_time = end_time - iam_detail::g_profiler.frame_start_time;

	// Store frame time in history
	int& idx = iam_detail::g_profiler.frame_history_idx;
	iam_detail::g_profiler.frame_history[idx] = (float)iam_detail::g_profiler.frame_total_time;
	idx = (idx + 1) % iam_detail::PROFILER_HISTORY_SIZE;

	// Store section times in history
	for (int i = 0; i < iam_detail::g_profiler.section_count; i++) {
		auto& sec = iam_detail::g_profiler.sections[i];
		sec.history[sec.history_idx] = (float)sec.accumulated_time;
		sec.history_idx = (sec.history_idx + 1) % iam_detail::PROFILER_HISTORY_SIZE;
	}
}

void iam_profiler_begin(char const* name) {
	if (!iam_detail::g_profiler.enabled) return;
	int idx = iam_detail::g_profiler.find_or_create_section(name);
	if (idx < 0) return;

	auto& sec = iam_detail::g_profiler.sections[idx];
	sec.start_time = iam_detail::get_time_ms();
	sec.call_count++;

	// Push to stack
	if (iam_detail::g_profiler.stack_depth < iam_detail::PROFILER_MAX_STACK) {
		iam_detail::g_profiler.stack[iam_detail::g_profiler.stack_depth++] = idx;
	}
}

void iam_profiler_end() {
	if (!iam_detail::g_profiler.enabled) return;
	if (iam_detail::g_profiler.stack_depth <= 0) return;

	int idx = iam_detail::g_profiler.stack[--iam_detail::g_profiler.stack_depth];
	auto& sec = iam_detail::g_profiler.sections[idx];
	double end_time = iam_detail::get_time_ms();
	sec.accumulated_time += end_time - sec.start_time;
}

void iam_register_custom_ease(int slot, iam_ease_fn fn) {
	if (slot >= 0 && slot < 16) {
		iam_detail::g_custom_ease[slot] = fn;
	}
}

iam_ease_fn iam_get_custom_ease(int slot) {
	if (slot >= 0 && slot < 16) {
		return iam_detail::g_custom_ease[slot];
	}
	return nullptr;
}

float iam_eval_preset(int type, float t) {
	return iam_detail::eval_preset_internal(type, t);
}

float iam_tween_float(ImGuiID id, ImGuiID channel_id, float target, float dur, iam_ease_desc const& ez, int policy, float /*dt*/, float init_value) {
	using namespace iam_detail;
	ImGuiID key = make_key(id, channel_id);

	// Lazy init: if channel doesn't exist and target equals init_value, skip channel creation
	float_chan* c = g_float.try_get(key);
	bool const is_new = (c == nullptr);

	if (is_new) {
		if (g_lazy_init_enabled && fabsf(target - init_value) <= 1e-6f) {
			return target;
		}
		c = g_float.get(key);
		c->current = c->start = c->target = init_value;
	}

	// Fast path: sleeping and target unchanged
	if (c->sleeping && fabsf(c->target - target) <= 1e-6f && !c->has_pending) {
		return c->current;
	}

	// Compute current progress
	float t_now = c->sleeping ? 1.0f : (float)((g_global_time - c->start_time) / c->dur);
	bool anim_complete = t_now >= 1.0f;

	bool const change = (c->policy!=policy) || (c->ez.type!=ez.type) ||
	                    (c->ez.p0!=ez.p0) || (c->ez.p1!=ez.p1) || (c->ez.p2!=ez.p2) || (c->ez.p3!=ez.p3) ||
	                    (fabsf(c->target - target) > 1e-6f) || anim_complete;
	if (change) {
		if (policy == iam_policy_queue && !anim_complete && !c->has_pending) {
			c->pending_target = target; c->has_pending = 1;
		}
		else if (policy == iam_policy_cut) {
			c->current = c->start = c->target = target; c->dur = 1e-6f; c->ez = ez; c->policy = policy; c->sleeping = 1;
		}
		else {
			c->evaluate();  // Update current before setting new target
			c->set(target, dur, ez, policy);
		}
	}
	if (anim_complete && c->has_pending) { c->set(c->pending_target, dur, ez, policy); c->has_pending = 0; }
	return c->evaluate();
}

ImVec2 iam_tween_vec2(ImGuiID id, ImGuiID channel_id, ImVec2 target, float dur, iam_ease_desc const& ez, int policy, float /*dt*/, ImVec2 init_value) {
	using namespace iam_detail;
	ImGuiID key = make_key(id, channel_id);

	// Lazy init: if channel doesn't exist and target equals init_value, skip channel creation
	vec2_chan* c = g_vec2.try_get(key);
	bool const is_new = (c == nullptr);

	if (is_new) {
		if (g_lazy_init_enabled && fabsf(target.x - init_value.x) + fabsf(target.y - init_value.y) <= 1e-6f) {
			return target;
		}
		c = g_vec2.get(key);
		c->current = c->start = c->target = init_value;
	}

	if (c->sleeping && fabsf(c->target.x - target.x) + fabsf(c->target.y - target.y) <= 1e-6f && !c->has_pending) {
		return c->current;
	}

	float t_now = c->sleeping ? 1.0f : (float)((g_global_time - c->start_time) / c->dur);
	bool anim_complete = t_now >= 1.0f;

	bool const change = (c->policy!=policy) || (c->ez.type!=ez.type) ||
	                    (c->ez.p0!=ez.p0) || (c->ez.p1!=ez.p1) || (c->ez.p2!=ez.p2) || (c->ez.p3!=ez.p3) ||
	                    (fabsf(c->target.x - target.x) + fabsf(c->target.y - target.y) > 1e-6f) || anim_complete;
	if (change) {
		if (policy == iam_policy_queue && !anim_complete && !c->has_pending) { c->pending_target = target; c->has_pending = 1; }
		else if (policy == iam_policy_cut) { c->current = c->start = c->target = target; c->dur = 1e-6f; c->ez = ez; c->policy = policy; c->sleeping = 1; }
		else { c->evaluate(); c->set(target, dur, ez, policy); }
	}
	if (anim_complete && c->has_pending) { c->set(c->pending_target, dur, ez, policy); c->has_pending = 0; }
	return c->evaluate();
}

ImVec4 iam_tween_vec4(ImGuiID id, ImGuiID channel_id, ImVec4 target, float dur, iam_ease_desc const& ez, int policy, float /*dt*/, ImVec4 init_value) {
	using namespace iam_detail;
	ImGuiID key = make_key(id, channel_id);

	// Lazy init: if channel doesn't exist and target equals init_value, skip channel creation
	vec4_chan* c = g_vec4.try_get(key);
	bool const is_new = (c == nullptr);

	if (is_new) {
		if (g_lazy_init_enabled && fabsf(target.x - init_value.x) + fabsf(target.y - init_value.y) + fabsf(target.z - init_value.z) + fabsf(target.w - init_value.w) <= 1e-6f) {
			return target;
		}
		c = g_vec4.get(key);
		c->current = c->start = c->target = init_value;
	}

	if (c->sleeping && fabsf(c->target.x-target.x)+fabsf(c->target.y-target.y)+fabsf(c->target.z-target.z)+fabsf(c->target.w-target.w) <= 1e-6f && !c->has_pending) {
		return c->current;
	}

	float t_now = c->sleeping ? 1.0f : (float)((g_global_time - c->start_time) / c->dur);
	bool anim_complete = t_now >= 1.0f;

	bool const change = (c->policy!=policy) || (c->ez.type!=ez.type) ||
	                    (c->ez.p0!=ez.p0) || (c->ez.p1!=ez.p1) || (c->ez.p2!=ez.p2) || (c->ez.p3!=ez.p3) ||
	                    (fabsf(c->target.x-target.x)+fabsf(c->target.y-target.y)+fabsf(c->target.z-target.z)+fabsf(c->target.w-target.w) > 1e-6f) || anim_complete;
	if (change) {
		if (policy == iam_policy_queue && !anim_complete && !c->has_pending) { c->pending_target = target; c->has_pending = 1; }
		else if (policy == iam_policy_cut) { c->current = c->start = c->target = target; c->dur = 1e-6f; c->ez = ez; c->policy = policy; c->sleeping = 1; }
		else { c->evaluate(); c->set(target, dur, ez, policy); }
	}
	if (anim_complete && c->has_pending) { c->set(c->pending_target, dur, ez, policy); c->has_pending = 0; }
	return c->evaluate();
}

int iam_tween_int(ImGuiID id, ImGuiID channel_id, int target, float dur, iam_ease_desc const& ez, int policy, float /*dt*/, int init_value) {
	using namespace iam_detail;
	ImGuiID key = make_key(id, channel_id);

	// Lazy init: if channel doesn't exist and target equals init_value, skip channel creation
	int_chan* c = g_int.try_get(key);
	bool const is_new = (c == nullptr);

	if (is_new) {
		if (g_lazy_init_enabled && target == init_value) {
			return target;
		}
		c = g_int.get(key);
		c->current = c->start = c->target = init_value;
	}

	if (c->sleeping && c->target == target && !c->has_pending) { return c->current; }

	float t_now = c->sleeping ? 1.0f : (float)((g_global_time - c->start_time) / c->dur);
	bool anim_complete = t_now >= 1.0f;

	bool const change = (c->policy!=policy) || (c->ez.type!=ez.type) ||
	                    (c->ez.p0!=ez.p0) || (c->ez.p1!=ez.p1) || (c->ez.p2!=ez.p2) || (c->ez.p3!=ez.p3) ||
	                    (c->target != target) || anim_complete;
	if (change) {
		if (policy == iam_policy_queue && !anim_complete && !c->has_pending) { c->pending_target = target; c->has_pending = 1; }
		else if (policy == iam_policy_cut) { c->current = c->start = c->target = target; c->dur = 1e-6f; c->ez = ez; c->policy = policy; c->sleeping = 1; }
		else { c->evaluate(); c->set(target, dur, ez, policy); }
	}
	if (anim_complete && c->has_pending) { c->set(c->pending_target, dur, ez, policy); c->has_pending = 0; }
	return c->evaluate();
}

ImVec4 iam_tween_color(ImGuiID id, ImGuiID channel_id, ImVec4 target_srgb, float dur, iam_ease_desc const& ez, int policy, int color_space, float /*dt*/, ImVec4 init_value) {
	using namespace iam_detail;
	ImGuiID key = make_key(id, channel_id);

	// Lazy init: if channel doesn't exist and target equals init_value, skip channel creation
	color_chan* c = g_color.try_get(key);
	bool const is_new = (c == nullptr);

	if (is_new) {
		if (g_lazy_init_enabled && fabsf(target_srgb.x - init_value.x) + fabsf(target_srgb.y - init_value.y) + fabsf(target_srgb.z - init_value.z) + fabsf(target_srgb.w - init_value.w) <= 1e-6f) {
			return target_srgb;
		}
		c = g_color.get(key);
		c->current = c->start = c->target = init_value;
	}

	if (c->sleeping && (fabsf(c->target.x-target_srgb.x)+fabsf(c->target.y-target_srgb.y)+fabsf(c->target.z-target_srgb.z)+fabsf(c->target.w-target_srgb.w)) <= 1e-6f) { return c->current; }

	float t_now = c->sleeping ? 1.0f : (float)((g_global_time - c->start_time) / c->dur);
	bool anim_complete = t_now >= 1.0f;

	bool const change = (c->policy!=policy) || (c->space != color_space) || (c->ez.type!=ez.type) ||
	                    (c->ez.p0!=ez.p0) || (c->ez.p1!=ez.p1) || (c->ez.p2!=ez.p2) || (c->ez.p3!=ez.p3) ||
	                    (fabsf(c->target.x-target_srgb.x)+fabsf(c->target.y-target_srgb.y)+fabsf(c->target.z-target_srgb.z)+fabsf(c->target.w-target_srgb.w) > 1e-6f) || anim_complete;
	if (change) {
		if (policy == iam_policy_cut) { c->current = c->start = c->target = target_srgb; c->dur = 1e-6f; c->ez = ez; c->policy = policy; c->space = color_space; c->sleeping = 1; }
		else { c->evaluate(); c->set(target_srgb, dur, ez, policy, color_space); }
	}
	return c->evaluate();
}

// ============================================================
// PER-AXIS EASING - Different easing per component
// ============================================================

ImVec2 iam_tween_vec2_per_axis(ImGuiID id, ImGuiID channel_id, ImVec2 target, float dur, iam_ease_per_axis const& ez, int policy, float dt) {
	// Use separate float tweens for each axis with their own easing
	ImGuiID ch_x = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_x"));
	ImGuiID ch_y = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_y"));

	float x = iam_tween_float(id, ch_x, target.x, dur, ez.x, policy, dt);
	float y = iam_tween_float(id, ch_y, target.y, dur, ez.y, policy, dt);

	return ImVec2(x, y);
}

ImVec4 iam_tween_vec4_per_axis(ImGuiID id, ImGuiID channel_id, ImVec4 target, float dur, iam_ease_per_axis const& ez, int policy, float dt) {
	// Use separate float tweens for each axis with their own easing
	ImGuiID ch_x = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_x"));
	ImGuiID ch_y = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_y"));
	ImGuiID ch_z = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_z"));
	ImGuiID ch_w = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_w"));

	float x = iam_tween_float(id, ch_x, target.x, dur, ez.x, policy, dt);
	float y = iam_tween_float(id, ch_y, target.y, dur, ez.y, policy, dt);
	float z = iam_tween_float(id, ch_z, target.z, dur, ez.z, policy, dt);
	float w = iam_tween_float(id, ch_w, target.w, dur, ez.w, policy, dt);

	return ImVec4(x, y, z, w);
}

ImVec4 iam_tween_color_per_axis(ImGuiID id, ImGuiID channel_id, ImVec4 target_srgb, float dur, iam_ease_per_axis const& ez, int policy, int color_space, float dt) {
	// For colors, we apply per-axis easing in the target color space
	// Convert target to working space
	ImVec4 target_work = iam_detail::color::to_space(target_srgb, color_space);

	// Use separate float tweens for each channel with their own easing
	ImGuiID ch_r = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_r"));
	ImGuiID ch_g = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_g"));
	ImGuiID ch_b = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_b"));
	ImGuiID ch_a = ImHashData(&channel_id, sizeof(channel_id), ImHashStr("_pa_a"));

	// Get current values in working space for interpolation
	dt *= iam_detail::g_time_scale;
	ImGuiID key_r = iam_detail::make_key(id, ch_r);
	ImGuiID key_g = iam_detail::make_key(id, ch_g);
	ImGuiID key_b = iam_detail::make_key(id, ch_b);
	ImGuiID key_a = iam_detail::make_key(id, ch_a);

	iam_detail::float_chan* cr = iam_detail::g_float.get(key_r);
	iam_detail::float_chan* cg = iam_detail::g_float.get(key_g);
	iam_detail::float_chan* cb = iam_detail::g_float.get(key_b);
	iam_detail::float_chan* ca = iam_detail::g_float.get(key_a);

	// Check if this is a new animation (target changed)
	bool change_r = fabsf(cr->target - target_work.x) > 1e-6f || cr->t >= 1.0f;
	bool change_g = fabsf(cg->target - target_work.y) > 1e-6f || cg->t >= 1.0f;
	bool change_b = fabsf(cb->target - target_work.z) > 1e-6f || cb->t >= 1.0f;
	bool change_a = fabsf(ca->target - target_work.w) > 1e-6f || ca->t >= 1.0f;

	// Update each channel
	auto update_chan = [&](iam_detail::float_chan* c, float target_val, iam_ease_desc const& e, bool changed) {
		if (changed) {
			if (policy == iam_policy_cut) {
				c->current = c->start = c->target = target_val;
				c->sleeping = 1; c->dur = 1e-6f; c->ez = e; c->policy = policy;
			} else {
				if (c->progress() < 1.0f && dt > 0) c->tick(dt);
				c->set(target_val, dur, e, policy);
				c->tick(dt);
			}
		} else {
			c->tick(dt);
		}
		return c->current;
	};

	float r = update_chan(cr, target_work.x, ez.x, change_r);
	float g = update_chan(cg, target_work.y, ez.y, change_g);
	float b = update_chan(cb, target_work.z, ez.z, change_b);
	float a = update_chan(ca, target_work.w, ez.w, change_a);

	// Convert back to sRGB
	return iam_detail::color::from_space(ImVec4(r, g, b, a), color_space);
}

ImVec2 iam_anchor_size(int space) {
	switch (space) {
		case iam_anchor_window_content: return ImGui::GetContentRegionAvail();
		case iam_anchor_window:         return ImGui::GetWindowSize();
		case iam_anchor_viewport:
#ifdef IMGUI_HAS_VIEWPORT
			return ImGui::GetWindowViewport()->Size;
#else
			return ImGui::GetIO().DisplaySize;
#endif
		case iam_anchor_last_item: {
			ImVec2 mi = ImGui::GetItemRectMin(), ma = ImGui::GetItemRectMax();
			return ImVec2(ma.x - mi.x, ma.y - mi.y);
		}
		default: return ImVec2(0,0);
	}
}

ImVec2 iam_tween_vec2_rel(ImGuiID id, ImGuiID channel_id, ImVec2 percent, ImVec2 px_bias, float dur, iam_ease_desc const& ez, int policy, int anchor_space, float dt) {
	ImVec2 base = iam_anchor_size(anchor_space);
	ImVec2 target = ImVec2(base.x*percent.x + px_bias.x, base.y*percent.y + px_bias.y);
	return iam_tween_vec2(id, channel_id, target, dur, ez, policy, dt);
}

ImVec2 iam_tween_vec2_resolved(ImGuiID id, ImGuiID channel_id, iam_vec2_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, float dt) {
	ImVec2 target = fn ? fn(user) : ImVec2(0,0);
	return iam_tween_vec2(id, channel_id, target, dur, ez, policy, dt);
}

void iam_rebase_vec2(ImGuiID id, ImGuiID channel_id, ImVec2 new_target, float dt) {
	ImGuiID key = iam_detail::make_key(id, channel_id);
	int idx = iam_detail::g_vec2.pool.Map.GetInt(key, -1);
	if (idx == -1) return;
	iam_detail::vec2_chan* c = iam_detail::g_vec2.pool.GetByIndex(idx);
	if (c->progress() < 1.0f && dt > 0) c->tick(dt);
	float remain = (1.0f - (c->progress() < 1.0f ? c->t : 1.0f)) * c->dur;
	c->start = c->current;
	c->target = new_target;
	c->start_time = iam_detail::g_global_time; c->sleeping = 0;
	c->dur = (remain <= 1e-6f ? 1e-6f : remain);
}

// Additional resolved/rel/rebase functions for all types

float iam_tween_float_rel(ImGuiID id, ImGuiID channel_id, float percent, float px_bias, float dur, iam_ease_desc const& ez, int policy, int anchor_space, int axis, float dt) {
	ImVec2 base = iam_anchor_size(anchor_space);
	float target = (axis == 0 ? base.x : base.y) * percent + px_bias;
	return iam_tween_float(id, channel_id, target, dur, ez, policy, dt);
}

ImVec4 iam_tween_vec4_rel(ImGuiID id, ImGuiID channel_id, ImVec4 percent, ImVec4 px_bias, float dur, iam_ease_desc const& ez, int policy, int anchor_space, float dt) {
	ImVec2 base = iam_anchor_size(anchor_space);
	ImVec4 target = ImVec4(base.x*percent.x + px_bias.x, base.y*percent.y + px_bias.y, percent.z + px_bias.z, percent.w + px_bias.w);
	return iam_tween_vec4(id, channel_id, target, dur, ez, policy, dt);
}

ImVec4 iam_tween_color_rel(ImGuiID id, ImGuiID channel_id, ImVec4 percent, ImVec4 px_bias, float dur, iam_ease_desc const& ez, int policy, int color_space, int anchor_space, float dt) {
	// For color, percent/px_bias represent color component modifiers (not spatial anchors)
	(void)anchor_space; // Color doesn't use spatial anchors
	ImVec4 target = ImVec4(percent.x + px_bias.x, percent.y + px_bias.y, percent.z + px_bias.z, percent.w + px_bias.w);
	return iam_tween_color(id, channel_id, target, dur, ez, policy, color_space, dt);
}

float iam_tween_float_resolved(ImGuiID id, ImGuiID channel_id, iam_float_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, float dt) {
	float target = fn ? fn(user) : 0.0f;
	return iam_tween_float(id, channel_id, target, dur, ez, policy, dt);
}

ImVec4 iam_tween_vec4_resolved(ImGuiID id, ImGuiID channel_id, iam_vec4_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, float dt) {
	ImVec4 target = fn ? fn(user) : ImVec4(0,0,0,0);
	return iam_tween_vec4(id, channel_id, target, dur, ez, policy, dt);
}

ImVec4 iam_tween_color_resolved(ImGuiID id, ImGuiID channel_id, iam_color_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, int color_space, float dt) {
	ImVec4 target = fn ? fn(user) : ImVec4(0,0,0,1);
	return iam_tween_color(id, channel_id, target, dur, ez, policy, color_space, dt);
}

int iam_tween_int_resolved(ImGuiID id, ImGuiID channel_id, iam_int_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, float dt) {
	int target = fn ? fn(user) : 0;
	return iam_tween_int(id, channel_id, target, dur, ez, policy, dt);
}

void iam_rebase_float(ImGuiID id, ImGuiID channel_id, float new_target, float dt) {
	ImGuiID key = iam_detail::make_key(id, channel_id);
	int idx = iam_detail::g_float.pool.Map.GetInt(key, -1);
	if (idx == -1) return;
	iam_detail::float_chan* c = iam_detail::g_float.pool.GetByIndex(idx);
	if (c->progress() < 1.0f && dt > 0) c->tick(dt);
	float remain = (1.0f - (c->progress() < 1.0f ? c->t : 1.0f)) * c->dur;
	c->start = c->current;
	c->target = new_target;
	c->start_time = iam_detail::g_global_time; c->sleeping = 0;
	c->dur = (remain <= 1e-6f ? 1e-6f : remain);
}

void iam_rebase_vec4(ImGuiID id, ImGuiID channel_id, ImVec4 new_target, float dt) {
	ImGuiID key = iam_detail::make_key(id, channel_id);
	int idx = iam_detail::g_vec4.pool.Map.GetInt(key, -1);
	if (idx == -1) return;
	iam_detail::vec4_chan* c = iam_detail::g_vec4.pool.GetByIndex(idx);
	if (c->progress() < 1.0f && dt > 0) c->tick(dt);
	float remain = (1.0f - (c->progress() < 1.0f ? c->t : 1.0f)) * c->dur;
	c->start = c->current;
	c->target = new_target;
	c->start_time = iam_detail::g_global_time; c->sleeping = 0;
	c->dur = (remain <= 1e-6f ? 1e-6f : remain);
}

void iam_rebase_color(ImGuiID id, ImGuiID channel_id, ImVec4 new_target, float dt) {
	ImGuiID key = iam_detail::make_key(id, channel_id);
	int idx = iam_detail::g_color.pool.Map.GetInt(key, -1);
	if (idx == -1) return;
	iam_detail::color_chan* c = iam_detail::g_color.pool.GetByIndex(idx);
	if (c->progress() < 1.0f && dt > 0) c->tick(dt);
	float remain = (1.0f - (c->progress() < 1.0f ? c->t : 1.0f)) * c->dur;
	c->start = c->current;
	c->target = new_target;
	c->start_time = iam_detail::g_global_time; c->sleeping = 0;
	c->dur = (remain <= 1e-6f ? 1e-6f : remain);
}

void iam_rebase_int(ImGuiID id, ImGuiID channel_id, int new_target, float dt) {
	ImGuiID key = iam_detail::make_key(id, channel_id);
	int idx = iam_detail::g_int.pool.Map.GetInt(key, -1);
	if (idx == -1) return;
	iam_detail::int_chan* c = iam_detail::g_int.pool.GetByIndex(idx);
	if (c->progress() < 1.0f && dt > 0) c->tick(dt);
	float remain = (1.0f - (c->progress() < 1.0f ? c->t : 1.0f)) * c->dur;
	c->start = c->current;
	c->target = new_target;
	c->start_time = iam_detail::g_global_time; c->sleeping = 0;
	c->dur = (remain <= 1e-6f ? 1e-6f : remain);
}

ImVec4 iam_get_blended_color(ImVec4 a_srgb, ImVec4 b_srgb, float t, int color_space) {
	return iam_detail::color::lerp_color(a_srgb, b_srgb, t, color_space);
}

// ============================================================
// CLIP-BASED ANIMATION SYSTEM IMPLEMENTATION
// ============================================================

namespace iam_clip_detail {

// keyframe data - using plain floats to avoid union constructor issues
struct keyframe {
	ImGuiID		channel;
	float		time;
	int			type;		// iam_channel_type
	int			ease_type;	// iam_ease_type
	int			color_space;// iam_color_space (for iam_chan_color)
	float		bezier[4];
	bool		has_bezier;
	bool		is_spring;
	iam_spring_params spring;
	float		value[4];	// f=value[0], v2=(value[0],value[1]), v4=(value[0..3]), i=*(int*)&value[0], color=(value[0..3])
	float		value_ext[4];// Extended storage for relative types (px_bias for vec4_rel/color_rel)

	// Variation data for repeat with variation feature
	bool				has_variation;
	iam_variation_float	var_float;
	iam_variation_int	var_int;
	iam_variation_vec2	var_vec2;
	iam_variation_vec4	var_vec4;
	iam_variation_color	var_color;

	keyframe() : channel(0), time(0), type(0), ease_type(iam_ease_linear), color_space(iam_col_oklab), has_bezier(false), is_spring(false), has_variation(false) {
		bezier[0] = bezier[1] = bezier[2] = bezier[3] = 0;
		spring = { 1.0f, 120.0f, 20.0f, 0.0f };
		value[0] = value[1] = value[2] = value[3] = 0;
		value_ext[0] = value_ext[1] = value_ext[2] = value_ext[3] = 0;
		memset(&var_float, 0, sizeof(var_float));
		memset(&var_int, 0, sizeof(var_int));
		memset(&var_vec2, 0, sizeof(var_vec2));
		memset(&var_vec4, 0, sizeof(var_vec4));
		memset(&var_color, 0, sizeof(var_color));
	}

	void set_float(float f) { value[0] = f; }
	float get_float() const { return value[0]; }
	void set_vec2(ImVec2 v) { value[0] = v.x; value[1] = v.y; }
	ImVec2 get_vec2() const { return ImVec2(value[0], value[1]); }
	void set_vec4(ImVec4 v) { value[0] = v.x; value[1] = v.y; value[2] = v.z; value[3] = v.w; }
	ImVec4 get_vec4() const { return ImVec4(value[0], value[1], value[2], value[3]); }
	void set_int(int i) { memcpy(&value[0], &i, sizeof(int)); }
	int get_int() const { int i; memcpy(&i, &value[0], sizeof(int)); return i; }
	void set_color(ImVec4 c) { value[0] = c.x; value[1] = c.y; value[2] = c.z; value[3] = c.w; }
	ImVec4 get_color() const { return ImVec4(value[0], value[1], value[2], value[3]); }

	// Relative (anchor) value accessors: float uses value[0]=percent, value[1]=px_bias
	void set_float_rel(float percent, float px_bias) { value[0] = percent; value[1] = px_bias; }
	float get_float_rel_percent() const { return value[0]; }
	float get_float_rel_px_bias() const { return value[1]; }

	// Relative vec2: value[0,1]=percent.xy, value[2,3]=px_bias.xy
	void set_vec2_rel(ImVec2 percent, ImVec2 px_bias) { value[0] = percent.x; value[1] = percent.y; value[2] = px_bias.x; value[3] = px_bias.y; }
	ImVec2 get_vec2_rel_percent() const { return ImVec2(value[0], value[1]); }
	ImVec2 get_vec2_rel_px_bias() const { return ImVec2(value[2], value[3]); }

	// Relative vec4: value[0..3]=percent.xyzw, value_ext[0..3]=px_bias.xyzw
	void set_vec4_rel(ImVec4 percent, ImVec4 px_bias) {
		value[0] = percent.x; value[1] = percent.y; value[2] = percent.z; value[3] = percent.w;
		value_ext[0] = px_bias.x; value_ext[1] = px_bias.y; value_ext[2] = px_bias.z; value_ext[3] = px_bias.w;
	}
	ImVec4 get_vec4_rel_percent() const { return ImVec4(value[0], value[1], value[2], value[3]); }
	ImVec4 get_vec4_rel_px_bias() const { return ImVec4(value_ext[0], value_ext[1], value_ext[2], value_ext[3]); }

	// Relative color: value[0..3]=percent.rgba, value_ext[0..3]=px_bias.rgba
	void set_color_rel(ImVec4 percent, ImVec4 px_bias) {
		value[0] = percent.x; value[1] = percent.y; value[2] = percent.z; value[3] = percent.w;
		value_ext[0] = px_bias.x; value_ext[1] = px_bias.y; value_ext[2] = px_bias.z; value_ext[3] = px_bias.w;
	}
	ImVec4 get_color_rel_percent() const { return ImVec4(value[0], value[1], value[2], value[3]); }
	ImVec4 get_color_rel_px_bias() const { return ImVec4(value_ext[0], value_ext[1], value_ext[2], value_ext[3]); }
};

// iam_track: sorted keyframes for a single channel
struct iam_track {
	ImGuiID				channel;
	int					type;
	int					color_space;  // For iam_chan_color tracks
	ImVector<keyframe>	keys;

	// Anchor-relative support (for key_float_rel, key_vec2_rel)
	bool				is_relative;	// If true, values are percent+px_bias, resolved at get time
	int					anchor_space;	// iam_anchor_space (window_content, window, viewport, etc.)
	int					anchor_axis;	// For float: 0=x, 1=y (ignored for vec2/vec4)

	iam_track() : channel(0), type(0), color_space(iam_col_oklab), is_relative(false), anchor_space(0), anchor_axis(0) {}
};

// Timeline marker
struct iam_marker {
	float					time;
	ImGuiID					marker_id;
	iam_marker_callback		callback;
	void*					user_data;
	iam_marker() : time(0), marker_id(0), callback(nullptr), user_data(nullptr) {}
};

} // namespace iam_clip_detail

// iam_clip_data definition
struct iam_clip_data {
	ImGuiID					id;
	float					delay;
	float					duration;
	int						loop_count;		// -1 = infinite, 0 = no loop, >0 = repeat count
	int						direction;		// iam_direction
	ImVector<iam_clip_detail::iam_track>	iam_tracks;

	// Timeline markers
	ImVector<iam_clip_detail::iam_marker>	markers;

	// Callbacks
	iam_clip_callback		cb_begin;
	iam_clip_callback		cb_update;
	iam_clip_callback		cb_complete;
	void*					cb_begin_user;
	void*					cb_update_user;
	void*					cb_complete_user;

	// Build-time state
	ImVector<iam_clip_detail::keyframe>	build_keys;

	// Seq/par grouping state (build-time)
	struct group_state {
		float	base_time;		// base time offset for this group
		float	max_time;		// max time seen in this group (for seq_end)
		bool	is_seq;			// true = sequential, false = parallel
	};
	ImVector<group_state>	group_stack;
	float					build_time_offset;	// current time offset for keyframes

	// Stagger state
	int						stagger_count;
	float					stagger_delay;
	float					stagger_center_bias;

	// Timing variation per loop iteration
	bool					has_duration_var;
	bool					has_delay_var;
	bool					has_timescale_var;
	iam_variation_float		duration_var;
	iam_variation_float		delay_var;
	iam_variation_float		timescale_var;

	iam_clip_data() : id(0), delay(0), duration(0), loop_count(0), direction(iam_dir_normal),
		cb_begin(nullptr), cb_update(nullptr), cb_complete(nullptr),
		cb_begin_user(nullptr), cb_update_user(nullptr), cb_complete_user(nullptr),
		build_time_offset(0), stagger_count(0), stagger_delay(0), stagger_center_bias(0),
		has_duration_var(false), has_delay_var(false), has_timescale_var(false) {
		memset(&duration_var, 0, sizeof(duration_var));
		memset(&delay_var, 0, sizeof(delay_var));
		memset(&timescale_var, 0, sizeof(timescale_var));
	}
};

// iam_instance_data definition
struct iam_instance_data {
	ImGuiID		inst_id;
	ImGuiID		clip_id;		// Store clip ID instead of pointer to avoid invalidation
	float		time;
	float		time_scale;
	float		weight;
	float		delay_left;
	bool		playing;
	bool		paused;
	bool		begin_called;	// iam_track whether on_begin has been called
	int			dir_sign;
	int			loops_left;
	unsigned	last_seen_frame;

	// Per-channel current values (cached after evaluation)
	ImGuiStorage	values_float;
	ImGuiStorage	values_int;
	// vec2/vec4/color stored as float pairs/quads in separate arrays
	struct vec2_entry { ImGuiID ch = 0; ImVec2 v = {}; };
	struct vec4_entry { ImGuiID ch = 0; ImVec4 v = {}; };
	struct color_entry { ImGuiID ch = 0; ImVec4 v = {}; int color_space = 0; };
	// Relative types need 8 floats: percent(4) + px_bias(4)
	struct vec4_rel_entry { ImGuiID ch = 0; ImVec4 percent = {}; ImVec4 px_bias = {}; };
	struct color_rel_entry { ImGuiID ch = 0; ImVec4 percent = {}; ImVec4 px_bias = {}; int color_space = 0; };
	ImVector<vec2_entry> values_vec2;
	ImVector<vec4_entry> values_vec4;
	ImVector<color_entry> values_color;
	ImVector<vec4_rel_entry> values_vec4_rel;
	ImVector<color_rel_entry> values_color_rel;

	// Layered blending output (written by iam_layer_end)
	ImGuiStorage	blended_float;
	ImGuiStorage	blended_int;
	ImVector<vec2_entry> blended_vec2;
	ImVector<vec4_entry> blended_vec4;
	ImVector<color_entry> blended_color;
	bool			has_blended;	// true if blended values are valid

	// Marker tracking - bitset for triggered markers (reset on loop)
	ImVector<bool>	markers_triggered;
	float			prev_time;		// Previous time for marker crossing detection

	// Animation chaining - next clip to play when this one completes
	ImGuiID		chain_next_clip_id;		// Clip to play next (0 = none)
	ImGuiID		chain_next_inst_id;		// Instance ID for next clip (0 = auto-generate)
	float		chain_delay;			// Delay before starting chained clip

	// Loop variation tracking
	int			current_loop;			// Current loop iteration (0-based), used for variation calculations
	unsigned int var_rng_state;			// RNG state for deterministic variation random

	iam_instance_data() : inst_id(0), clip_id(0), time(0), time_scale(1.0f), weight(1.0f),
		delay_left(0), playing(false), paused(false), begin_called(false), dir_sign(1), loops_left(0), last_seen_frame(0),
		has_blended(false), prev_time(0), chain_next_clip_id(0), chain_next_inst_id(0), chain_delay(0),
		current_loop(0), var_rng_state(12345) {}
};

namespace iam_clip_detail {

// Global clip system state
static struct iam_clip_system {
	ImVector<iam_clip_data>		clips;
	ImVector<iam_instance_data>	instances;
	ImGuiStorage				clip_map;		// clip_id -> index+1
	ImGuiStorage				inst_map;		// inst_id -> index+1
	unsigned					frame_counter;
	bool						initialized;

	iam_clip_system() : frame_counter(0), initialized(false) {}
} g_clip_sys;

static iam_clip_data* find_clip(ImGuiID clip_id) {
	int idx = g_clip_sys.clip_map.GetInt(clip_id, 0);
	if (idx == 0) return nullptr;
	return &g_clip_sys.clips[idx - 1];
}

static iam_instance_data* find_instance(ImGuiID inst_id) {
	int idx = g_clip_sys.inst_map.GetInt(inst_id, 0);
	if (idx == 0) return nullptr;
	return &g_clip_sys.instances[idx - 1];
}

// Evaluate easing for clip keyframes
static float eval_clip_ease(int ease_type, float t, float const* bezier, bool has_bezier) {
	if (has_bezier && ease_type == iam_ease_cubic_bezier) {
		iam_ease_desc d = { ease_type, bezier[0], bezier[1], bezier[2], bezier[3] };
		return iam_detail::eval(d, t);
	}
	return iam_detail::eval_preset_internal(ease_type, t);
}

// Evaluate spring
static float eval_clip_spring(float u, iam_spring_params const& sp) {
	return iam_detail::ease_lut_pool_singleton().spring_unit(u, sp.mass, sp.stiffness, sp.damping, sp.initial_velocity);
}

// ----------------------------------------------------
// Variation evaluation helpers
// ----------------------------------------------------

// Simple xorshift random for deterministic variation
static unsigned int var_xorshift(unsigned int* state) {
	unsigned int x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

// Get random float in [0, 1]
static float var_rand_unit(unsigned int* state) {
	return (float)(var_xorshift(state) & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

// Get random float in [-1, 1]
static float var_rand_signed(unsigned int* state) {
	return var_rand_unit(state) * 2.0f - 1.0f;
}

// Clamp helper
static float var_clampf(float v, float mn, float mx) {
	return v < mn ? mn : (v > mx ? mx : v);
}
static int var_clampi(int v, int mn, int mx) {
	return v < mn ? mn : (v > mx ? mx : v);
}

// Compute float variation delta for given loop index
static float compute_var_float(iam_variation_float const& var, int loop_index, unsigned int* rng_state) {
	if (var.mode == iam_var_none) return 0.0f;

	// Use seed for deterministic random if provided
	unsigned int rng = var.seed != 0 ? (var.seed + (unsigned int)loop_index * 1664525u) : *rng_state;

	float delta = 0.0f;
	switch (var.mode) {
		case iam_var_callback:
			if (var.callback) return var.callback(loop_index, var.user);
			return 0.0f;
		case iam_var_increment:
			delta = var.amount * (float)loop_index;
			break;
		case iam_var_decrement:
			delta = -var.amount * (float)loop_index;
			break;
		case iam_var_multiply:
			// For multiply, return the multiplier (to be applied differently)
			return ImPow(var.amount, (float)loop_index);
		case iam_var_random:
			delta = var_rand_signed(&rng) * var.amount;
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_random_abs:
			delta = var_rand_unit(&rng) * var.amount;
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_pingpong:
			delta = (loop_index % 2 == 0) ? 0.0f : var.amount;
			if (loop_index % 4 >= 2) delta = -delta;
			break;
		default:
			return 0.0f;
	}
	return delta;
}

// Apply float variation to a base value
static float apply_var_float(float base, iam_variation_float const& var, int loop_index, unsigned int* rng_state) {
	if (var.mode == iam_var_none) return base;
	if (var.mode == iam_var_callback && var.callback) {
		return var_clampf(var.callback(loop_index, var.user), var.min_clamp, var.max_clamp);
	}
	if (var.mode == iam_var_multiply) {
		float mult = compute_var_float(var, loop_index, rng_state);
		return var_clampf(base * mult, var.min_clamp, var.max_clamp);
	}
	float delta = compute_var_float(var, loop_index, rng_state);
	return var_clampf(base + delta, var.min_clamp, var.max_clamp);
}

// Apply int variation to a base value
static int apply_var_int(int base, iam_variation_int const& var, int loop_index, unsigned int* rng_state) {
	if (var.mode == iam_var_none) return base;
	if (var.mode == iam_var_callback && var.callback) {
		return var_clampi(var.callback(loop_index, var.user), var.min_clamp, var.max_clamp);
	}

	unsigned int rng = var.seed != 0 ? (var.seed + (unsigned int)loop_index * 1664525u) : *rng_state;
	int delta = 0;
	switch (var.mode) {
		case iam_var_increment:
			delta = var.amount * loop_index;
			break;
		case iam_var_decrement:
			delta = -var.amount * loop_index;
			break;
		case iam_var_multiply:
			return var_clampi((int)(base * ImPow((float)var.amount, (float)loop_index)), var.min_clamp, var.max_clamp);
		case iam_var_random:
			delta = (int)(var_rand_signed(&rng) * (float)var.amount);
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_random_abs:
			delta = (int)(var_rand_unit(&rng) * (float)var.amount);
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_pingpong:
			delta = (loop_index % 2 == 0) ? 0 : var.amount;
			if (loop_index % 4 >= 2) delta = -delta;
			break;
		default:
			return base;
	}
	return var_clampi(base + delta, var.min_clamp, var.max_clamp);
}

// Apply vec2 variation (global or per-axis)
static ImVec2 apply_var_vec2(ImVec2 base, iam_variation_vec2 const& var, int loop_index, unsigned int* rng_state) {
	// Per-axis variation (when global mode is none but per-axis has modes)
	if (var.mode == iam_var_none) {
		if (var.x.mode != iam_var_none || var.y.mode != iam_var_none) {
			return ImVec2(
				apply_var_float(base.x, var.x, loop_index, rng_state),
				apply_var_float(base.y, var.y, loop_index, rng_state)
			);
		}
		return base;
	}

	// Callback
	if (var.mode == iam_var_callback && var.callback) {
		ImVec2 result = var.callback(loop_index, var.user);
		return ImVec2(
			var_clampf(result.x, var.min_clamp.x, var.max_clamp.x),
			var_clampf(result.y, var.min_clamp.y, var.max_clamp.y)
		);
	}

	// Global variation
	unsigned int rng = var.seed != 0 ? (var.seed + (unsigned int)loop_index * 1664525u) : *rng_state;
	ImVec2 result = base;

	switch (var.mode) {
		case iam_var_increment:
			result.x += var.amount.x * (float)loop_index;
			result.y += var.amount.y * (float)loop_index;
			break;
		case iam_var_decrement:
			result.x -= var.amount.x * (float)loop_index;
			result.y -= var.amount.y * (float)loop_index;
			break;
		case iam_var_multiply: {
			float mult = ImPow(var.amount.x, (float)loop_index);
			result.x *= mult;
			result.y *= ImPow(var.amount.y, (float)loop_index);
			break;
		}
		case iam_var_random:
			result.x += var_rand_signed(&rng) * var.amount.x;
			result.y += var_rand_signed(&rng) * var.amount.y;
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_random_abs:
			result.x += var_rand_unit(&rng) * var.amount.x;
			result.y += var_rand_unit(&rng) * var.amount.y;
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_pingpong: {
			float sign = (loop_index % 4 >= 2) ? -1.0f : 1.0f;
			float mult = (loop_index % 2 == 0) ? 0.0f : 1.0f;
			result.x += var.amount.x * mult * sign;
			result.y += var.amount.y * mult * sign;
			break;
		}
	}

	return ImVec2(
		var_clampf(result.x, var.min_clamp.x, var.max_clamp.x),
		var_clampf(result.y, var.min_clamp.y, var.max_clamp.y)
	);
}

// Apply vec4 variation (global or per-axis)
static ImVec4 apply_var_vec4(ImVec4 base, iam_variation_vec4 const& var, int loop_index, unsigned int* rng_state) {
	// Per-axis variation
	if (var.mode == iam_var_none) {
		if (var.x.mode != iam_var_none || var.y.mode != iam_var_none ||
		    var.z.mode != iam_var_none || var.w.mode != iam_var_none) {
			return ImVec4(
				apply_var_float(base.x, var.x, loop_index, rng_state),
				apply_var_float(base.y, var.y, loop_index, rng_state),
				apply_var_float(base.z, var.z, loop_index, rng_state),
				apply_var_float(base.w, var.w, loop_index, rng_state)
			);
		}
		return base;
	}

	// Callback
	if (var.mode == iam_var_callback && var.callback) {
		ImVec4 result = var.callback(loop_index, var.user);
		return ImVec4(
			var_clampf(result.x, var.min_clamp.x, var.max_clamp.x),
			var_clampf(result.y, var.min_clamp.y, var.max_clamp.y),
			var_clampf(result.z, var.min_clamp.z, var.max_clamp.z),
			var_clampf(result.w, var.min_clamp.w, var.max_clamp.w)
		);
	}

	// Global variation
	unsigned int rng = var.seed != 0 ? (var.seed + (unsigned int)loop_index * 1664525u) : *rng_state;
	ImVec4 result = base;

	switch (var.mode) {
		case iam_var_increment:
			result.x += var.amount.x * (float)loop_index;
			result.y += var.amount.y * (float)loop_index;
			result.z += var.amount.z * (float)loop_index;
			result.w += var.amount.w * (float)loop_index;
			break;
		case iam_var_decrement:
			result.x -= var.amount.x * (float)loop_index;
			result.y -= var.amount.y * (float)loop_index;
			result.z -= var.amount.z * (float)loop_index;
			result.w -= var.amount.w * (float)loop_index;
			break;
		case iam_var_multiply:
			result.x *= ImPow(var.amount.x, (float)loop_index);
			result.y *= ImPow(var.amount.y, (float)loop_index);
			result.z *= ImPow(var.amount.z, (float)loop_index);
			result.w *= ImPow(var.amount.w, (float)loop_index);
			break;
		case iam_var_random:
			result.x += var_rand_signed(&rng) * var.amount.x;
			result.y += var_rand_signed(&rng) * var.amount.y;
			result.z += var_rand_signed(&rng) * var.amount.z;
			result.w += var_rand_signed(&rng) * var.amount.w;
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_random_abs:
			result.x += var_rand_unit(&rng) * var.amount.x;
			result.y += var_rand_unit(&rng) * var.amount.y;
			result.z += var_rand_unit(&rng) * var.amount.z;
			result.w += var_rand_unit(&rng) * var.amount.w;
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_pingpong: {
			float sign = (loop_index % 4 >= 2) ? -1.0f : 1.0f;
			float mult = (loop_index % 2 == 0) ? 0.0f : 1.0f;
			result.x += var.amount.x * mult * sign;
			result.y += var.amount.y * mult * sign;
			result.z += var.amount.z * mult * sign;
			result.w += var.amount.w * mult * sign;
			break;
		}
	}

	return ImVec4(
		var_clampf(result.x, var.min_clamp.x, var.max_clamp.x),
		var_clampf(result.y, var.min_clamp.y, var.max_clamp.y),
		var_clampf(result.z, var.min_clamp.z, var.max_clamp.z),
		var_clampf(result.w, var.min_clamp.w, var.max_clamp.w)
	);
}

// Apply color variation (global or per-channel) in the specified color space
// Input and output are in sRGB, variation is applied in the target color space
static ImVec4 apply_var_color(ImVec4 base_srgb, iam_variation_color const& var, int loop_index, unsigned int* rng_state) {
	int color_space = var.color_space;

	// Convert sRGB to working color space
	ImVec4 base = iam_detail::color::to_space(base_srgb, color_space);

	// Per-channel variation (in working color space)
	if (var.mode == iam_var_none) {
		if (var.r.mode != iam_var_none || var.g.mode != iam_var_none ||
		    var.b.mode != iam_var_none || var.a.mode != iam_var_none) {
			ImVec4 result(
				apply_var_float(base.x, var.r, loop_index, rng_state),
				apply_var_float(base.y, var.g, loop_index, rng_state),
				apply_var_float(base.z, var.b, loop_index, rng_state),
				apply_var_float(base.w, var.a, loop_index, rng_state)
			);
			// Convert back to sRGB
			return iam_detail::color::from_space(result, color_space);
		}
		return base_srgb;
	}

	// Callback (assumed to return sRGB directly)
	if (var.mode == iam_var_callback && var.callback) {
		ImVec4 result = var.callback(loop_index, var.user);
		return ImVec4(
			var_clampf(result.x, var.min_clamp.x, var.max_clamp.x),
			var_clampf(result.y, var.min_clamp.y, var.max_clamp.y),
			var_clampf(result.z, var.min_clamp.z, var.max_clamp.z),
			var_clampf(result.w, var.min_clamp.w, var.max_clamp.w)
		);
	}

	// Global variation in working color space
	unsigned int rng = var.seed != 0 ? (var.seed + (unsigned int)loop_index * 1664525u) : *rng_state;
	ImVec4 result = base;

	switch (var.mode) {
		case iam_var_increment:
			result.x += var.amount.x * (float)loop_index;
			result.y += var.amount.y * (float)loop_index;
			result.z += var.amount.z * (float)loop_index;
			result.w += var.amount.w * (float)loop_index;
			break;
		case iam_var_decrement:
			result.x -= var.amount.x * (float)loop_index;
			result.y -= var.amount.y * (float)loop_index;
			result.z -= var.amount.z * (float)loop_index;
			result.w -= var.amount.w * (float)loop_index;
			break;
		case iam_var_multiply:
			result.x *= ImPow(var.amount.x, (float)loop_index);
			result.y *= ImPow(var.amount.y, (float)loop_index);
			result.z *= ImPow(var.amount.z, (float)loop_index);
			result.w *= ImPow(var.amount.w, (float)loop_index);
			break;
		case iam_var_random:
			result.x += var_rand_signed(&rng) * var.amount.x;
			result.y += var_rand_signed(&rng) * var.amount.y;
			result.z += var_rand_signed(&rng) * var.amount.z;
			result.w += var_rand_signed(&rng) * var.amount.w;
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_random_abs:
			result.x += var_rand_unit(&rng) * var.amount.x;
			result.y += var_rand_unit(&rng) * var.amount.y;
			result.z += var_rand_unit(&rng) * var.amount.z;
			result.w += var_rand_unit(&rng) * var.amount.w;
			if (var.seed == 0) *rng_state = rng;
			break;
		case iam_var_pingpong: {
			float sign = (loop_index % 4 >= 2) ? -1.0f : 1.0f;
			float mult = (loop_index % 2 == 0) ? 0.0f : 1.0f;
			result.x += var.amount.x * mult * sign;
			result.y += var.amount.y * mult * sign;
			result.z += var.amount.z * mult * sign;
			result.w += var.amount.w * mult * sign;
			break;
		}
	}

	// Clamp in working space
	result = ImVec4(
		var_clampf(result.x, var.min_clamp.x, var.max_clamp.x),
		var_clampf(result.y, var.min_clamp.y, var.max_clamp.y),
		var_clampf(result.z, var.min_clamp.z, var.max_clamp.z),
		var_clampf(result.w, var.min_clamp.w, var.max_clamp.w)
	);

	// Convert back to sRGB
	return iam_detail::color::from_space(result, color_space);
}

// Find keyframes bracketing time t for a iam_track
static bool find_keys(iam_track const& trk, float t, keyframe const** k0, keyframe const** k1) {
	if (trk.keys.Size == 0) return false;

	// Single keyframe case
	if (trk.keys.Size == 1) {
		*k0 = *k1 = &trk.keys[0];
		return true;
	}

	// Before first keyframe
	if (t <= trk.keys[0].time) {
		*k0 = *k1 = &trk.keys[0];
		return true;
	}

	// After last keyframe
	if (t >= trk.keys[trk.keys.Size - 1].time) {
		*k0 = *k1 = &trk.keys[trk.keys.Size - 1];
		return true;
	}

	// Find bracketing keyframes
	for (int i = 0; i < trk.keys.Size - 1; ++i) {
		if (t >= trk.keys[i].time && t <= trk.keys[i + 1].time) {
			*k0 = &trk.keys[i];
			*k1 = &trk.keys[i + 1];
			return true;
		}
	}

	// Fallback (shouldn't reach here)
	*k0 = *k1 = &trk.keys[trk.keys.Size - 1];
	return true;
}

// Evaluate a iam_track at time t
static void eval_iam_track(iam_track const& trk, float t, iam_instance_data* inst) {
	if (!inst || trk.keys.Size == 0) return;
	keyframe const* k0; keyframe const* k1;
	if (!find_keys(trk, t, &k0, &k1)) return;
	if (!k0 || !k1) return;  // Safety check

	float u = (k1->time == k0->time) ? 1.0f : (t - k0->time) / (k1->time - k0->time);
	float w;
	if (k0->is_spring && trk.type == iam_chan_float) {
		w = eval_clip_spring(u, k0->spring);
	} else {
		w = eval_clip_ease(k0->ease_type, u, k0->bezier, k0->has_bezier);
	}

	// Get current loop index for variation
	int loop_index = inst->current_loop;

	switch (trk.type) {
		case iam_chan_float: {
			float a = k0->get_float(), b = k1->get_float();
			// Apply variation if present
			if (k0->has_variation) {
				a = apply_var_float(a, k0->var_float, loop_index, &inst->var_rng_state);
			}
			if (k1->has_variation) {
				b = apply_var_float(b, k1->var_float, loop_index, &inst->var_rng_state);
			}
			float v = a + (b - a) * w;
			inst->values_float.SetFloat(trk.channel, v);
			break;
		}
		case iam_chan_vec2: {
			ImVec2 a = k0->get_vec2(), b = k1->get_vec2();
			// Apply variation if present
			if (k0->has_variation) {
				a = apply_var_vec2(a, k0->var_vec2, loop_index, &inst->var_rng_state);
			}
			if (k1->has_variation) {
				b = apply_var_vec2(b, k1->var_vec2, loop_index, &inst->var_rng_state);
			}
			ImVec2 v(a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w);
			// Store in vec2 array
			bool found = false;
			for (int i = 0; i < inst->values_vec2.Size; ++i) {
				if (inst->values_vec2[i].ch == trk.channel) {
					inst->values_vec2[i].v = v;
					found = true;
					break;
				}
			}
			if (!found) {
				iam_instance_data::vec2_entry e; e.ch = trk.channel; e.v = v;
				inst->values_vec2.push_back(e);
			}
			break;
		}
		case iam_chan_vec4: {
			ImVec4 a = k0->get_vec4(), b = k1->get_vec4();
			// Apply variation if present
			if (k0->has_variation) {
				a = apply_var_vec4(a, k0->var_vec4, loop_index, &inst->var_rng_state);
			}
			if (k1->has_variation) {
				b = apply_var_vec4(b, k1->var_vec4, loop_index, &inst->var_rng_state);
			}
			ImVec4 v(a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w, a.w + (b.w - a.w) * w);
			bool found = false;
			for (int i = 0; i < inst->values_vec4.Size; ++i) {
				if (inst->values_vec4[i].ch == trk.channel) {
					inst->values_vec4[i].v = v;
					found = true;
					break;
				}
			}
			if (!found) {
				iam_instance_data::vec4_entry e; e.ch = trk.channel; e.v = v;
				inst->values_vec4.push_back(e);
			}
			break;
		}
		case iam_chan_int: {
			int a = k0->get_int(), b = k1->get_int();
			// Apply variation if present
			if (k0->has_variation) {
				a = apply_var_int(a, k0->var_int, loop_index, &inst->var_rng_state);
			}
			if (k1->has_variation) {
				b = apply_var_int(b, k1->var_int, loop_index, &inst->var_rng_state);
			}
			int v = (int)(a + (int)((float)(b - a) * w + 0.5f));
			inst->values_int.SetInt(trk.channel, v);
			break;
		}
		case iam_chan_color: {
			ImVec4 a = k0->get_color(), b = k1->get_color();
			// Apply variation if present
			if (k0->has_variation) {
				a = apply_var_color(a, k0->var_color, loop_index, &inst->var_rng_state);
			}
			if (k1->has_variation) {
				b = apply_var_color(b, k1->var_color, loop_index, &inst->var_rng_state);
			}
			// Blend in the specified color space
			ImVec4 v = iam_detail::color::lerp_color(a, b, w, trk.color_space);
			bool found = false;
			for (int i = 0; i < inst->values_color.Size; ++i) {
				if (inst->values_color[i].ch == trk.channel) {
					inst->values_color[i].v = v;
					inst->values_color[i].color_space = trk.color_space;
					found = true;
					break;
				}
			}
			if (!found) {
				iam_instance_data::color_entry e; e.ch = trk.channel; e.v = v; e.color_space = trk.color_space;
				inst->values_color.push_back(e);
			}
			break;
		}
		case iam_chan_float_rel: {
			// Interpolate percent and px_bias separately, store as vec2 (percent, px_bias)
			float percent_a = k0->get_float_rel_percent(), percent_b = k1->get_float_rel_percent();
			float px_bias_a = k0->get_float_rel_px_bias(), px_bias_b = k1->get_float_rel_px_bias();
			float percent = percent_a + (percent_b - percent_a) * w;
			float px_bias = px_bias_a + (px_bias_b - px_bias_a) * w;
			// Store as vec2 for later resolution (percent in x, px_bias in y)
			ImVec2 v(percent, px_bias);
			bool found = false;
			for (int i = 0; i < inst->values_vec2.Size; ++i) {
				if (inst->values_vec2[i].ch == trk.channel) {
					inst->values_vec2[i].v = v;
					found = true;
					break;
				}
			}
			if (!found) {
				iam_instance_data::vec2_entry e; e.ch = trk.channel; e.v = v;
				inst->values_vec2.push_back(e);
			}
			break;
		}
		case iam_chan_vec2_rel: {
			// Interpolate percent and px_bias separately, store as vec4 (percent.xy, px_bias.xy)
			ImVec2 percent_a = k0->get_vec2_rel_percent(), percent_b = k1->get_vec2_rel_percent();
			ImVec2 px_bias_a = k0->get_vec2_rel_px_bias(), px_bias_b = k1->get_vec2_rel_px_bias();
			ImVec2 percent(percent_a.x + (percent_b.x - percent_a.x) * w, percent_a.y + (percent_b.y - percent_a.y) * w);
			ImVec2 px_bias(px_bias_a.x + (px_bias_b.x - px_bias_a.x) * w, px_bias_a.y + (px_bias_b.y - px_bias_a.y) * w);
			// Store as vec4 (percent.x, percent.y, px_bias.x, px_bias.y)
			ImVec4 v(percent.x, percent.y, px_bias.x, px_bias.y);
			bool found = false;
			for (int i = 0; i < inst->values_vec4.Size; ++i) {
				if (inst->values_vec4[i].ch == trk.channel) {
					inst->values_vec4[i].v = v;
					found = true;
					break;
				}
			}
			if (!found) {
				iam_instance_data::vec4_entry e; e.ch = trk.channel; e.v = v;
				inst->values_vec4.push_back(e);
			}
			break;
		}
		case iam_chan_vec4_rel: {
			// Interpolate percent and px_bias separately
			ImVec4 percent_a = k0->get_vec4_rel_percent(), percent_b = k1->get_vec4_rel_percent();
			ImVec4 px_bias_a = k0->get_vec4_rel_px_bias(), px_bias_b = k1->get_vec4_rel_px_bias();
			ImVec4 percent(
				percent_a.x + (percent_b.x - percent_a.x) * w,
				percent_a.y + (percent_b.y - percent_a.y) * w,
				percent_a.z + (percent_b.z - percent_a.z) * w,
				percent_a.w + (percent_b.w - percent_a.w) * w
			);
			ImVec4 px_bias(
				px_bias_a.x + (px_bias_b.x - px_bias_a.x) * w,
				px_bias_a.y + (px_bias_b.y - px_bias_a.y) * w,
				px_bias_a.z + (px_bias_b.z - px_bias_a.z) * w,
				px_bias_a.w + (px_bias_b.w - px_bias_a.w) * w
			);
			bool found = false;
			for (int i = 0; i < inst->values_vec4_rel.Size; ++i) {
				if (inst->values_vec4_rel[i].ch == trk.channel) {
					inst->values_vec4_rel[i].percent = percent;
					inst->values_vec4_rel[i].px_bias = px_bias;
					found = true;
					break;
				}
			}
			if (!found) {
				iam_instance_data::vec4_rel_entry e;
				e.ch = trk.channel;
				e.percent = percent;
				e.px_bias = px_bias;
				inst->values_vec4_rel.push_back(e);
			}
			break;
		}
		case iam_chan_color_rel: {
			// Interpolate percent and px_bias separately (color blending not applied to percent/bias)
			ImVec4 percent_a = k0->get_color_rel_percent(), percent_b = k1->get_color_rel_percent();
			ImVec4 px_bias_a = k0->get_color_rel_px_bias(), px_bias_b = k1->get_color_rel_px_bias();
			ImVec4 percent(
				percent_a.x + (percent_b.x - percent_a.x) * w,
				percent_a.y + (percent_b.y - percent_a.y) * w,
				percent_a.z + (percent_b.z - percent_a.z) * w,
				percent_a.w + (percent_b.w - percent_a.w) * w
			);
			ImVec4 px_bias(
				px_bias_a.x + (px_bias_b.x - px_bias_a.x) * w,
				px_bias_a.y + (px_bias_b.y - px_bias_a.y) * w,
				px_bias_a.z + (px_bias_b.z - px_bias_a.z) * w,
				px_bias_a.w + (px_bias_b.w - px_bias_a.w) * w
			);
			bool found = false;
			for (int i = 0; i < inst->values_color_rel.Size; ++i) {
				if (inst->values_color_rel[i].ch == trk.channel) {
					inst->values_color_rel[i].percent = percent;
					inst->values_color_rel[i].px_bias = px_bias;
					inst->values_color_rel[i].color_space = trk.color_space;
					found = true;
					break;
				}
			}
			if (!found) {
				iam_instance_data::color_rel_entry e;
				e.ch = trk.channel;
				e.percent = percent;
				e.px_bias = px_bias;
				e.color_space = trk.color_space;
				inst->values_color_rel.push_back(e);
			}
			break;
		}
	}
}

// Sort keyframes by time
static int cmp_keyframe(void const* a, void const* b) {
	keyframe const* A = (keyframe const*)a;
	keyframe const* B = (keyframe const*)b;
	if (A->time < B->time) return -1;
	if (A->time > B->time) return 1;
	return 0;
}

} // namespace iam_clip_detail

// ----------------------------------------------------
// iam_clip class implementation
// ----------------------------------------------------

// Helper to get clip data by ID (safe lookup)
static iam_clip_data* get_clip_data(ImGuiID clip_id) {
	using namespace iam_clip_detail;
	return find_clip(clip_id);
}

iam_clip iam_clip::begin(ImGuiID clip_id) {
	using namespace iam_clip_detail;
	if (!g_clip_sys.initialized) {
		iam_clip_init();
	}

	// Find or create clip
	int idx = g_clip_sys.clip_map.GetInt(clip_id, 0);
	iam_clip_data* clip;
	if (idx == 0) {
		g_clip_sys.clips.push_back(iam_clip_data());
		clip = &g_clip_sys.clips.back();
		clip->id = clip_id;
		g_clip_sys.clip_map.SetInt(clip_id, g_clip_sys.clips.Size);
	} else {
		clip = &g_clip_sys.clips[idx - 1];
	}

	// Reset for building
	clip->build_keys.clear();
	clip->iam_tracks.clear();
	clip->group_stack.clear();
	clip->duration = 0;
	clip->delay = 0;
	clip->loop_count = 0;
	clip->direction = iam_dir_normal;
	clip->build_time_offset = 0;
	clip->stagger_count = 0;
	clip->stagger_delay = 0;
	clip->stagger_center_bias = 0;

	// Reset timing variation
	clip->has_duration_var = false;
	clip->has_delay_var = false;
	clip->has_timescale_var = false;
	memset(&clip->duration_var, 0, sizeof(clip->duration_var));
	memset(&clip->delay_var, 0, sizeof(clip->delay_var));
	memset(&clip->timescale_var, 0, sizeof(clip->timescale_var));

	return iam_clip(clip_id);
}

// Helper to compute actual keyframe time and update group state
static float compute_key_time(iam_clip_data* clip, float time) {
	float actual_time = time + clip->build_time_offset;
	// Update max_time in current group if any
	if (clip->group_stack.Size > 0) {
		iam_clip_data::group_state& gs = clip->group_stack.back();
		if (actual_time > gs.max_time) gs.max_time = actual_time;
	}
	return actual_time;
}

iam_clip& iam_clip::key_float(ImGuiID channel, float time, float value, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_float;
	k.ease_type = ease_type;
	k.set_float(value);
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_vec2(ImGuiID channel, float time, ImVec2 value, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_vec2;
	k.ease_type = ease_type;
	k.set_vec2(value);
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_vec4(ImGuiID channel, float time, ImVec4 value, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_vec4;
	k.ease_type = ease_type;
	k.set_vec4(value);
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_int(ImGuiID channel, float time, int value, int ease_type) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_int;
	k.ease_type = ease_type;
	k.set_int(value);
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_color(ImGuiID channel, float time, ImVec4 value, int color_space, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_color;
	k.ease_type = ease_type;
	k.color_space = color_space;
	k.set_color(value);
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_float_spring(ImGuiID channel, float time, float target, iam_spring_params const& spring) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_float;
	k.ease_type = iam_ease_spring;
	k.is_spring = true;
	k.spring = spring;
	k.set_float(target);
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

// Anchor-relative keyframes
iam_clip& iam_clip::key_float_rel(ImGuiID channel, float time, float percent, float px_bias, int anchor_space, int axis, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_float_rel;
	k.ease_type = ease_type;
	k.set_float_rel(percent, px_bias);
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	// Store anchor info in the keyframe for track setup during end()
	// We abuse color_space field temporarily to store anchor_space | (axis << 8)
	k.color_space = anchor_space | (axis << 8);
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_vec2_rel(ImGuiID channel, float time, ImVec2 percent, ImVec2 px_bias, int anchor_space, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_vec2_rel;
	k.ease_type = ease_type;
	k.set_vec2_rel(percent, px_bias);
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	// Store anchor info
	k.color_space = anchor_space;
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_vec4_rel(ImGuiID channel, float time, ImVec4 percent, ImVec4 px_bias, int anchor_space, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_vec4_rel;
	k.ease_type = ease_type;
	k.set_vec4_rel(percent, px_bias);
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	// Store anchor info
	k.color_space = anchor_space;
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_color_rel(ImGuiID channel, float time, ImVec4 percent, ImVec4 px_bias, int color_space, int anchor_space, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_color_rel;
	k.ease_type = ease_type;
	k.color_space = color_space;
	k.set_color_rel(percent, px_bias);
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	// Store anchor_space in a different way - use high bits of color_space
	// color_space uses low 8 bits, anchor_space in bits 8-15
	k.color_space = (color_space & 0xFF) | ((anchor_space & 0xFF) << 8);
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

// Keyframes with repeat variation
iam_clip& iam_clip::key_float_var(ImGuiID channel, float time, float value, iam_variation_float const& var, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_float;
	k.ease_type = ease_type;
	k.set_float(value);
	k.has_variation = true;
	k.var_float = var;
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_vec2_var(ImGuiID channel, float time, ImVec2 value, iam_variation_vec2 const& var, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_vec2;
	k.ease_type = ease_type;
	k.set_vec2(value);
	k.has_variation = true;
	k.var_vec2 = var;
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_vec4_var(ImGuiID channel, float time, ImVec4 value, iam_variation_vec4 const& var, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_vec4;
	k.ease_type = ease_type;
	k.set_vec4(value);
	k.has_variation = true;
	k.var_vec4 = var;
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_int_var(ImGuiID channel, float time, int value, iam_variation_int const& var, int ease_type) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_int;
	k.ease_type = ease_type;
	k.set_int(value);
	k.has_variation = true;
	k.var_int = var;
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::key_color_var(ImGuiID channel, float time, ImVec4 value, iam_variation_color const& var, int color_space, int ease_type, float const* bezier4) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	float actual_time = compute_key_time(clip, time);
	iam_clip_detail::keyframe k;
	k.channel = channel;
	k.time = actual_time;
	k.type = iam_chan_color;
	k.ease_type = ease_type;
	k.color_space = color_space;
	k.set_color(value);
	k.has_variation = true;
	k.var_color = var;
	if (bezier4) {
		k.has_bezier = true;
		k.bezier[0] = bezier4[0]; k.bezier[1] = bezier4[1];
		k.bezier[2] = bezier4[2]; k.bezier[3] = bezier4[3];
	}
	clip->build_keys.push_back(k);
	if (actual_time > clip->duration) clip->duration = actual_time;
	return *this;
}

iam_clip& iam_clip::seq_begin() {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	iam_clip_data::group_state gs;
	gs.base_time = clip->build_time_offset;
	gs.max_time = clip->build_time_offset;
	gs.is_seq = true;
	clip->group_stack.push_back(gs);
	return *this;
}

iam_clip& iam_clip::seq_end() {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip || clip->group_stack.Size == 0) return *this;
	iam_clip_data::group_state gs = clip->group_stack.back();
	clip->group_stack.pop_back();
	// After seq_end, the time offset advances to max_time seen in the group
	if (gs.is_seq) {
		clip->build_time_offset = gs.max_time;
	}
	return *this;
}

iam_clip& iam_clip::par_begin() {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	iam_clip_data::group_state gs;
	gs.base_time = clip->build_time_offset;
	gs.max_time = clip->build_time_offset;
	gs.is_seq = false;
	clip->group_stack.push_back(gs);
	return *this;
}

iam_clip& iam_clip::par_end() {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip || clip->group_stack.Size == 0) return *this;
	iam_clip_data::group_state gs = clip->group_stack.back();
	clip->group_stack.pop_back();
	// After par_end, time offset advances to max_time (all parallel tracks complete)
	clip->build_time_offset = gs.max_time;
	return *this;
}

iam_clip& iam_clip::set_loop(bool loop, int direction, int loop_count) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	clip->direction = direction;
	clip->loop_count = loop ? loop_count : 0;
	return *this;
}

iam_clip& iam_clip::set_delay(float delay_seconds) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	clip->delay = delay_seconds;
	return *this;
}

iam_clip& iam_clip::set_stagger(int count, float each_delay, float from_center_bias) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	clip->stagger_count = count > 0 ? count : 1;
	clip->stagger_delay = each_delay;
	clip->stagger_center_bias = ImClamp(from_center_bias, 0.0f, 1.0f);
	return *this;
}

iam_clip& iam_clip::set_duration_var(iam_variation_float const& var) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	clip->has_duration_var = true;
	clip->duration_var = var;
	return *this;
}

iam_clip& iam_clip::set_delay_var(iam_variation_float const& var) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	clip->has_delay_var = true;
	clip->delay_var = var;
	return *this;
}

iam_clip& iam_clip::set_timescale_var(iam_variation_float const& var) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	clip->has_timescale_var = true;
	clip->timescale_var = var;
	return *this;
}

iam_clip& iam_clip::on_begin(iam_clip_callback cb, void* user) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	clip->cb_begin = cb;
	clip->cb_begin_user = user;
	return *this;
}

iam_clip& iam_clip::on_update(iam_clip_callback cb, void* user) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	clip->cb_update = cb;
	clip->cb_update_user = user;
	return *this;
}

iam_clip& iam_clip::on_complete(iam_clip_callback cb, void* user) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;
	clip->cb_complete = cb;
	clip->cb_complete_user = user;
	return *this;
}

// Auto-generate unique marker IDs
static ImGuiID generate_marker_id() {
	static unsigned s_marker_counter = 0;
	return ImHashData(&(++s_marker_counter), sizeof(s_marker_counter));
}

iam_clip& iam_clip::marker(float time, ImGuiID marker_id, iam_marker_callback cb, void* user) {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return *this;

	iam_clip_detail::iam_marker m;
	m.time = time + clip->build_time_offset;  // Apply current time offset
	m.marker_id = marker_id;
	m.callback = cb;
	m.user_data = user;
	clip->markers.push_back(m);

	// Update duration if marker is past current end
	if (m.time > clip->duration) {
		clip->duration = m.time;
	}

	return *this;
}

iam_clip& iam_clip::marker(float time, iam_marker_callback cb, void* user) {
	return marker(time, generate_marker_id(), cb, user);
}

void iam_clip::end() {
	iam_clip_data* clip = get_clip_data(m_clip_id);
	if (!clip) return;
	using namespace iam_clip_detail;

	// Sort keyframes by time
	if (clip->build_keys.Size > 1) {
		qsort(clip->build_keys.Data, clip->build_keys.Size, sizeof(keyframe), cmp_keyframe);
	}

	// Build iam_tracks by grouping keyframes by (channel, type)
	for (int i = 0; i < clip->build_keys.Size; ++i) {
		keyframe const& k = clip->build_keys[i];

		// Find existing iam_track
		iam_track* trk = nullptr;
		for (int t = 0; t < clip->iam_tracks.Size; ++t) {
			if (clip->iam_tracks[t].channel == k.channel && clip->iam_tracks[t].type == k.type) {
				trk = &clip->iam_tracks[t];
				break;
			}
		}

		// Create new iam_track if needed
		if (!trk) {
			clip->iam_tracks.push_back(iam_track());
			trk = &clip->iam_tracks.back();
			trk->channel = k.channel;
			trk->type = k.type;
			if (k.type == iam_chan_color) {
				trk->color_space = k.color_space;
			}
			// Set up anchor info for relative tracks
			if (k.type == iam_chan_float_rel) {
				trk->is_relative = true;
				trk->anchor_space = k.color_space & 0xFF;
				trk->anchor_axis = (k.color_space >> 8) & 0xFF;
			} else if (k.type == iam_chan_vec2_rel) {
				trk->is_relative = true;
				trk->anchor_space = k.color_space;
				trk->anchor_axis = 0;  // Not used for vec2
			} else if (k.type == iam_chan_vec4_rel) {
				trk->is_relative = true;
				trk->anchor_space = k.color_space;
				trk->anchor_axis = 0;  // Not used for vec4
			} else if (k.type == iam_chan_color_rel) {
				trk->is_relative = true;
				trk->color_space = k.color_space & 0xFF;  // Extract color_space from low bits
				trk->anchor_space = (k.color_space >> 8) & 0xFF;  // Extract anchor_space from high bits
				trk->anchor_axis = 0;
			}
		}

		trk->keys.push_back(k);
	}

	// Clear build data
	clip->build_keys.clear();

	// Sort markers by time
	if (clip->markers.Size > 1) {
		for (int i = 0; i < clip->markers.Size - 1; ++i) {
			for (int j = i + 1; j < clip->markers.Size; ++j) {
				if (clip->markers[j].time < clip->markers[i].time) {
					iam_marker tmp = clip->markers[i];
					clip->markers[i] = clip->markers[j];
					clip->markers[j] = tmp;
				}
			}
		}
	}
}

// ----------------------------------------------------
// iam_instance class implementation
// ----------------------------------------------------

// Helper to get instance data by ID (safe lookup)
static iam_instance_data* get_instance_data(ImGuiID inst_id) {
	using namespace iam_clip_detail;
	return find_instance(inst_id);
}

bool iam_instance::valid() const {
	return m_inst_id != 0 && get_instance_data(m_inst_id) != nullptr;
}

void iam_instance::pause() {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (inst) inst->paused = true;
}

void iam_instance::resume() {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (inst) inst->paused = false;
}

void iam_instance::stop() {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (inst) { inst->playing = false; inst->time = 0; }
}

void iam_instance::destroy() {
	using namespace iam_clip_detail;
	int idx = g_clip_sys.inst_map.GetInt(m_inst_id, 0);
	if (idx == 0) return;
	// Clear the instance data
	iam_instance_data* inst = &g_clip_sys.instances[idx - 1];
	inst->inst_id = 0;
	inst->clip_id = 0;
	inst->playing = false;
	inst->values_float.Clear();
	inst->values_vec2.clear();
	inst->values_vec4.clear();
	inst->values_int.Clear();
	// Remove from map
	g_clip_sys.inst_map.SetInt(m_inst_id, 0);
	m_inst_id = 0;
}

void iam_instance::seek(float time) {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (!inst) return;
	iam_clip_data* clip = get_clip_data(inst->clip_id);
	if (!clip) return;
	float dur = clip->duration;
	if (time < 0) time = 0;
	if (time > dur) time = dur;
	inst->time = time;
}

void iam_instance::set_time_scale(float scale) {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (inst) inst->time_scale = scale;
}

void iam_instance::set_weight(float weight) {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (inst) inst->weight = weight;
}

// Animation chaining
static ImGuiID generate_chain_instance_id() {
	static unsigned s_chain_counter = 0;
	return ImHashData(&(++s_chain_counter), sizeof(s_chain_counter));
}

iam_instance& iam_instance::then(ImGuiID next_clip_id) {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (inst) {
		inst->chain_next_clip_id = next_clip_id;
		inst->chain_next_inst_id = generate_chain_instance_id();  // Auto-generate instance ID
	}
	return *this;
}

iam_instance& iam_instance::then(ImGuiID next_clip_id, ImGuiID next_instance_id) {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (inst) {
		inst->chain_next_clip_id = next_clip_id;
		inst->chain_next_inst_id = next_instance_id;
	}
	return *this;
}

iam_instance& iam_instance::then_delay(float delay) {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (inst) {
		inst->chain_delay = delay;
	}
	return *this;
}

float iam_instance::time() const {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	return inst ? inst->time : 0.0f;
}

float iam_instance::duration() const {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (!inst) return 0.0f;
	iam_clip_data* clip = get_clip_data(inst->clip_id);
	return clip ? clip->duration : 0.0f;
}

bool iam_instance::is_playing() const {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	return inst ? inst->playing : false;
}

bool iam_instance::is_paused() const {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	return inst ? inst->paused : false;
}

bool iam_instance::get_float(ImGuiID channel, float* out) const {
	using namespace iam_clip_detail;
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (!inst || !out) return false;

	// Check if this channel is a relative float track (stored as vec2: percent, px_bias)
	iam_clip_data* clip = find_clip(inst->clip_id);
	if (clip) {
		for (int t = 0; t < clip->iam_tracks.Size; ++t) {
			iam_track& trk = clip->iam_tracks[t];
			if (trk.channel == channel && trk.type == iam_chan_float_rel) {
				// Find the stored percent/px_bias in values_vec2
				for (int i = 0; i < inst->values_vec2.Size; ++i) {
					if (inst->values_vec2[i].ch == channel) {
						float percent = inst->values_vec2[i].v.x;
						float px_bias = inst->values_vec2[i].v.y;
						// Resolve anchor
						ImVec2 anchor = iam_anchor_size(trk.anchor_space);
						float base = (trk.anchor_axis == 0) ? anchor.x : anchor.y;
						*out = base * percent + px_bias;
						return true;
					}
				}
				*out = 0.0f;
				return false;
			}
		}
	}

	// Normal float channel
	*out = inst->values_float.GetFloat(channel, 0.0f);
	return true;
}

bool iam_instance::get_vec2(ImGuiID channel, ImVec2* out) const {
	using namespace iam_clip_detail;
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (!inst || !out) return false;

	// Check if this channel is a relative vec2 track (stored as vec4: percent.xy, px_bias.xy)
	iam_clip_data* clip = find_clip(inst->clip_id);
	if (clip) {
		for (int t = 0; t < clip->iam_tracks.Size; ++t) {
			iam_track& trk = clip->iam_tracks[t];
			if (trk.channel == channel && trk.type == iam_chan_vec2_rel) {
				// Find the stored percent/px_bias in values_vec4
				for (int i = 0; i < inst->values_vec4.Size; ++i) {
					if (inst->values_vec4[i].ch == channel) {
						ImVec4& v = inst->values_vec4[i].v;
						ImVec2 percent(v.x, v.y);
						ImVec2 px_bias(v.z, v.w);
						// Resolve anchor
						ImVec2 anchor = iam_anchor_size(trk.anchor_space);
						*out = ImVec2(anchor.x * percent.x + px_bias.x, anchor.y * percent.y + px_bias.y);
						return true;
					}
				}
				*out = ImVec2(0, 0);
				return false;
			}
		}
	}

	// Normal vec2 channel
	for (int i = 0; i < inst->values_vec2.Size; ++i) {
		if (inst->values_vec2[i].ch == channel) {
			*out = inst->values_vec2[i].v;
			return true;
		}
	}
	*out = ImVec2(0, 0);
	return false;
}

bool iam_instance::get_vec4(ImGuiID channel, ImVec4* out) const {
	using namespace iam_clip_detail;
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (!inst || !out) return false;

	// Check if this channel is a relative vec4 track
	iam_clip_data* clip = find_clip(inst->clip_id);
	if (clip) {
		for (int t = 0; t < clip->iam_tracks.Size; ++t) {
			iam_track& trk = clip->iam_tracks[t];
			if (trk.channel == channel && trk.type == iam_chan_vec4_rel) {
				// Find the stored percent/px_bias in values_vec4_rel
				for (int i = 0; i < inst->values_vec4_rel.Size; ++i) {
					if (inst->values_vec4_rel[i].ch == channel) {
						ImVec4 percent = inst->values_vec4_rel[i].percent;
						ImVec4 px_bias = inst->values_vec4_rel[i].px_bias;
						// Resolve anchor - x,y use anchor dimensions, z,w pass through
						ImVec2 anchor = iam_anchor_size(trk.anchor_space);
						*out = ImVec4(
							anchor.x * percent.x + px_bias.x,
							anchor.y * percent.y + px_bias.y,
							percent.z + px_bias.z,  // z,w not anchor-relative
							percent.w + px_bias.w
						);
						return true;
					}
				}
				*out = ImVec4(0, 0, 0, 0);
				return false;
			}
		}
	}

	// Normal vec4 channel
	for (int i = 0; i < inst->values_vec4.Size; ++i) {
		if (inst->values_vec4[i].ch == channel) {
			*out = inst->values_vec4[i].v;
			return true;
		}
	}
	*out = ImVec4(0, 0, 0, 0);
	return false;
}

bool iam_instance::get_int(ImGuiID channel, int* out) const {
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (!inst || !out) return false;
	*out = inst->values_int.GetInt(channel, 0);
	return true;
}

bool iam_instance::get_color(ImGuiID channel, ImVec4* out, int color_space) const {
	using namespace iam_clip_detail;
	iam_instance_data* inst = get_instance_data(m_inst_id);
	if (!inst || !out) return false;

	// Check if this channel is a relative color track
	iam_clip_data* clip = find_clip(inst->clip_id);
	if (clip) {
		for (int t = 0; t < clip->iam_tracks.Size; ++t) {
			iam_track& trk = clip->iam_tracks[t];
			if (trk.channel == channel && trk.type == iam_chan_color_rel) {
				// Find the stored percent/px_bias in values_color_rel
				for (int i = 0; i < inst->values_color_rel.Size; ++i) {
					if (inst->values_color_rel[i].ch == channel) {
						ImVec4 percent = inst->values_color_rel[i].percent;
						ImVec4 px_bias = inst->values_color_rel[i].px_bias;
						// Resolve anchor - apply to all 4 components (RGBA)
						ImVec2 anchor = iam_anchor_size(trk.anchor_space);
						// For color, use anchor.x for R,B and anchor.y for G,A (or could be all same)
						// Most common use: all components scale the same way
						*out = ImVec4(
							anchor.x * percent.x + px_bias.x,
							anchor.y * percent.y + px_bias.y,
							anchor.x * percent.z + px_bias.z,
							anchor.y * percent.w + px_bias.w
						);
						return true;
					}
				}
				*out = ImVec4(0, 0, 0, 1);
				return false;
			}
		}
	}

	// Normal color channel
	for (int i = 0; i < inst->values_color.Size; ++i) {
		if (inst->values_color[i].ch == channel) {
			// If requested color space differs from stored, convert
			if (color_space != inst->values_color[i].color_space) {
				// Convert from stored sRGB to requested color space and back
				// Note: values are stored in sRGB after lerp_color
				*out = inst->values_color[i].v;
			} else {
				*out = inst->values_color[i].v;
			}
			return true;
		}
	}
	*out = ImVec4(0, 0, 0, 1);
	return false;
}

// ----------------------------------------------------
// Clip System API implementation
// ----------------------------------------------------

void iam_clip_init(int initial_clip_cap, int initial_inst_cap) {
	using namespace iam_clip_detail;
	if (g_clip_sys.initialized) return;
	g_clip_sys.clips.reserve(initial_clip_cap);
	g_clip_sys.instances.reserve(initial_inst_cap);
	g_clip_sys.initialized = true;
}

void iam_clip_shutdown() {
	using namespace iam_clip_detail;
	g_clip_sys.clips.clear();
	g_clip_sys.instances.clear();
	g_clip_sys.clip_map.Clear();
	g_clip_sys.inst_map.Clear();
	g_clip_sys.initialized = false;
}

void iam_clip_update(float dt) {
	using namespace iam_clip_detail;
	g_clip_sys.frame_counter++;

	// Apply global time scale
	dt *= iam_detail::g_time_scale;

	// Safety: clamp dt to reasonable range
	if (dt < 0.0f) dt = 0.0f;
	if (dt > 1.0f) dt = 1.0f;

	for (int i = 0; i < g_clip_sys.instances.Size; ++i) {
		iam_instance_data* inst = &g_clip_sys.instances[i];
		iam_clip_data* clip = find_clip(inst->clip_id);
		if (!inst->playing || inst->paused || !clip) continue;

		// Use local copy of dt for this instance to avoid affecting other instances
		float inst_dt = dt;

		// Handle delay
		if (inst->delay_left > 0.0f) {
			inst->delay_left -= inst_dt;
			if (inst->delay_left > 0.0f) {
				// Still evaluate tracks at t=0 so values are readable during delay
				for (int tr = 0; tr < clip->iam_tracks.Size; ++tr) {
					eval_iam_track(clip->iam_tracks[tr], 0.0f, inst);
				}
				inst->last_seen_frame = g_clip_sys.frame_counter;
				continue;
			}
			inst_dt = -inst->delay_left;
			inst->delay_left = 0.0f;
			// Call on_begin when delay expires
			if (!inst->begin_called && clip->cb_begin) {
				inst->begin_called = true;
				clip->cb_begin(inst->inst_id, clip->cb_begin_user);
			}
		}

		// Call on_begin on first frame if no delay
		if (!inst->begin_called && clip->cb_begin) {
			inst->begin_called = true;
			clip->cb_begin(inst->inst_id, clip->cb_begin_user);
		}

		float t = inst->time;
		float dts = inst_dt * (inst->time_scale <= 0.0f ? 1.0f : inst->time_scale);
		t += dts * (float)inst->dir_sign;

		// Apply duration variation if present
		float dur = clip->duration;
		if (clip->has_duration_var) {
			dur = apply_var_float(clip->duration, clip->duration_var, inst->current_loop, &inst->var_rng_state);
			if (dur < 0.001f) dur = 0.001f;  // Minimum duration
		}
		bool done = false;

		if (dur <= 0.0f) { inst->time = 0.0f; continue; }

		// Handle looping (with safety limit to prevent infinite loops)
		int const MAX_LOOP_ITERS = 1000;
		int loop_iters = 0;
		if (clip->direction == iam_dir_alternate) {
			while ((t < 0.0f || t > dur) && loop_iters < MAX_LOOP_ITERS) {
				if (clip->loop_count == 0 && inst->loops_left == 0) { done = true; break; }
				if (inst->loops_left > 0) inst->loops_left--;
				inst->dir_sign = -inst->dir_sign;
				if (t < 0.0f) t = -t;
				if (t > dur) t = 2*dur - t;
				loop_iters++;
			}
		} else if (clip->direction == iam_dir_reverse) {
			while (t < 0.0f && loop_iters < MAX_LOOP_ITERS) {
				if (clip->loop_count == 0 && inst->loops_left == 0) { done = true; break; }
				if (inst->loops_left > 0) inst->loops_left--;
				t += dur;
				loop_iters++;
			}
			while (t > dur && loop_iters < MAX_LOOP_ITERS) { t -= dur; loop_iters++; }
		} else { // normal
			while (t > dur && loop_iters < MAX_LOOP_ITERS) {
				if (clip->loop_count == 0 && inst->loops_left == 0) { done = true; break; }
				if (inst->loops_left > 0) inst->loops_left--;
				t -= dur;
				loop_iters++;
			}
			while (t < 0.0f && loop_iters < MAX_LOOP_ITERS) { t += dur; loop_iters++; }
		}
		// Safety clamp
		if (t < 0.0f) t = 0.0f;
		if (t > dur) t = dur;

		// Reset markers on loop and increment loop counter for variation
		if (loop_iters > 0) {
			inst->current_loop += loop_iters;
			for (int m = 0; m < inst->markers_triggered.Size; m++) {
				inst->markers_triggered[m] = false;
			}
			// Reset prev_time to avoid triggering all markers at once after loop
			inst->prev_time = (inst->dir_sign > 0) ? 0.0f : dur;

			// Apply timing variations for new loop iteration
			if (clip->has_timescale_var) {
				float new_scale = apply_var_float(1.0f, clip->timescale_var, inst->current_loop, &inst->var_rng_state);
				inst->time_scale = new_scale > 0.0f ? new_scale : 1.0f;
			}
			if (clip->has_delay_var) {
				float loop_delay = apply_var_float(0.0f, clip->delay_var, inst->current_loop, &inst->var_rng_state);
				if (loop_delay > 0.0f) {
					inst->delay_left = loop_delay;
				}
			}
		}

		if (done) {
			inst->playing = false;
			inst->time = (inst->dir_sign > 0) ? dur : 0.0f;
			// Evaluate final frame
			for (int tr = 0; tr < clip->iam_tracks.Size; ++tr) {
				eval_iam_track(clip->iam_tracks[tr], inst->time, inst);
			}
			inst->last_seen_frame = g_clip_sys.frame_counter;
			if (clip->cb_complete)
				clip->cb_complete(inst->inst_id, clip->cb_complete_user);

			// Start chained clip if any
			if (inst->chain_next_clip_id != 0) {
				ImGuiID next_clip = inst->chain_next_clip_id;
				ImGuiID next_inst = inst->chain_next_inst_id;
				float chain_delay = inst->chain_delay;

				// Clear the chain to prevent re-triggering
				inst->chain_next_clip_id = 0;
				inst->chain_next_inst_id = 0;
				inst->chain_delay = 0;

				// Play the chained clip
				iam_instance next = iam_play(next_clip, next_inst);
				if (next.valid() && chain_delay > 0) {
					// Apply chain delay
					iam_instance_data* next_data = find_instance(next_inst);
					if (next_data) {
						next_data->delay_left += chain_delay;
					}
				}
			}
			continue;
		}

		// Check for markers crossed between prev_time and t
		float prev_t = inst->prev_time;
		inst->time = t;

		// Initialize markers_triggered if needed
		if (inst->markers_triggered.Size != clip->markers.Size) {
			inst->markers_triggered.resize(clip->markers.Size);
			for (int m = 0; m < inst->markers_triggered.Size; m++) {
				inst->markers_triggered[m] = false;
			}
		}

		// Fire markers that were crossed
		// Handle both forward and backward playback
		float t_min = (prev_t < t) ? prev_t : t;
		float t_max = (prev_t < t) ? t : prev_t;
		for (int m = 0; m < clip->markers.Size; m++) {
			iam_marker const& marker = clip->markers[m];
			if (!inst->markers_triggered[m] && marker.time >= t_min && marker.time <= t_max) {
				inst->markers_triggered[m] = true;
				if (marker.callback) {
					marker.callback(inst->inst_id, marker.marker_id, marker.time, marker.user_data);
				}
			}
		}

		inst->prev_time = t;

		// Evaluate all iam_tracks
		for (int tr = 0; tr < clip->iam_tracks.Size; ++tr) {
			eval_iam_track(clip->iam_tracks[tr], t, inst);
		}

		if (clip->cb_update)
			clip->cb_update(inst->inst_id, clip->cb_update_user);

		inst->last_seen_frame = g_clip_sys.frame_counter;
	}
}

void iam_clip_gc(unsigned int max_age_frames) {
	using namespace iam_clip_detail;
	for (int i = 0; i < g_clip_sys.instances.Size; ++i) {
		iam_instance_data* inst = &g_clip_sys.instances[i];
		if (g_clip_sys.frame_counter - inst->last_seen_frame > max_age_frames) {
			g_clip_sys.inst_map.SetInt(inst->inst_id, 0);
			// Swap with last and remove
			g_clip_sys.instances[i] = g_clip_sys.instances[g_clip_sys.instances.Size - 1];
			g_clip_sys.instances.pop_back();
			// Update swapped instance's map entry
			if (i < g_clip_sys.instances.Size) {
				g_clip_sys.inst_map.SetInt(g_clip_sys.instances[i].inst_id, i + 1);
			}
			i--;
		}
	}
}

iam_instance iam_play(ImGuiID clip_id, ImGuiID instance_id) {
	using namespace iam_clip_detail;
	if (!g_clip_sys.initialized) iam_clip_init();

	iam_clip_data* clip = find_clip(clip_id);
	if (!clip) return iam_instance(0);

	int idx = g_clip_sys.inst_map.GetInt(instance_id, 0);
	iam_instance_data* inst;
	if (idx == 0) {
		g_clip_sys.instances.push_back(iam_instance_data());
		inst = &g_clip_sys.instances.back();
		inst->inst_id = instance_id;
		g_clip_sys.inst_map.SetInt(instance_id, g_clip_sys.instances.Size);
	} else {
		inst = &g_clip_sys.instances[idx - 1];
	}

	inst->clip_id = clip_id;  // Store ID instead of pointer
	inst->time = 0.0f;
	inst->time_scale = 1.0f;
	inst->weight = 1.0f;
	inst->delay_left = clip->delay;
	inst->playing = true;
	inst->paused = false;
	inst->begin_called = false;  // Reset so on_begin will be called
	inst->dir_sign = (clip->direction == iam_dir_reverse) ? -1 : 1;
	inst->loops_left = clip->loop_count;
	inst->last_seen_frame = g_clip_sys.frame_counter;

	// Initialize marker tracking
	inst->prev_time = (inst->dir_sign > 0) ? 0.0f : clip->duration;
	inst->markers_triggered.resize(clip->markers.Size);
	for (int m = 0; m < inst->markers_triggered.Size; m++) {
		inst->markers_triggered[m] = false;
	}

	// Reset chaining (can be set after iam_play using .then())
	inst->chain_next_clip_id = 0;
	inst->chain_next_inst_id = 0;
	inst->chain_delay = 0;

	// Reset variation state
	inst->current_loop = 0;
	inst->var_rng_state = 12345 + instance_id;  // Deterministic but unique per instance

	// Evaluate initial frame immediately so values are available right away
	float initial_time = (inst->dir_sign > 0) ? 0.0f : clip->duration;
	for (int tr = 0; tr < clip->iam_tracks.Size; ++tr) {
		eval_iam_track(clip->iam_tracks[tr], initial_time, inst);
	}

	return iam_instance(instance_id);  // Return iam_instance with ID
}

iam_instance iam_get_instance(ImGuiID instance_id) {
	using namespace iam_clip_detail;
	iam_instance_data* inst = find_instance(instance_id);
	return inst ? iam_instance(instance_id) : iam_instance(0);
}

float iam_clip_duration(ImGuiID clip_id) {
	using namespace iam_clip_detail;
	iam_clip_data* clip = find_clip(clip_id);
	return clip ? clip->duration : 0.0f;
}

bool iam_clip_exists(ImGuiID clip_id) {
	using namespace iam_clip_detail;
	return find_clip(clip_id) != nullptr;
}

// Stagger helpers
float iam_stagger_delay(ImGuiID clip_id, int index) {
	using namespace iam_clip_detail;
	iam_clip_data* clip = find_clip(clip_id);
	if (!clip || clip->stagger_count <= 1) return 0.0f;

	int count = clip->stagger_count;
	float delay = clip->stagger_delay;
	float bias = clip->stagger_center_bias;

	// Calculate delay based on index and center bias
	// bias = 0: start from beginning (index 0 has 0 delay)
	// bias = 1: start from center (center indices have 0 delay, edges have max delay)
	// bias = 0.5: mixed
	if (bias <= 0.0f) {
		// Simple linear stagger from start
		return (float)index * delay;
	} else {
		// Stagger from center
		float center = (float)(count - 1) * 0.5f;
		float dist_from_center = fabsf((float)index - center);
		float max_dist = center;
		if (max_dist > 0.0f) {
			float linear_delay = (float)index * delay;
			float center_delay = dist_from_center * delay * 2.0f / (float)count * (float)(count - 1);
			return linear_delay * (1.0f - bias) + center_delay * bias;
		}
	}
	return 0.0f;
}

iam_instance iam_play_stagger(ImGuiID clip_id, ImGuiID instance_id, int index) {
	using namespace iam_clip_detail;
	if (!g_clip_sys.initialized) iam_clip_init();

	iam_clip_data* clip = find_clip(clip_id);
	if (!clip) return iam_instance(0);

	// Play with base delay + stagger delay
	iam_instance inst = iam_play(clip_id, instance_id);
	if (inst.valid()) {
		iam_instance_data* inst_data = find_instance(instance_id);
		if (inst_data) {
			inst_data->delay_left = clip->delay + iam_stagger_delay(clip_id, index);
		}
	}
	return inst;
}

// Layering support - blends multiple instance outputs into one
static struct {
	ImGuiID target_id;
	float total_weight;
	// Accumulated weighted values
	ImGuiStorage acc_float;
	ImGuiStorage acc_int;
	ImVector<iam_instance_data::vec2_entry> acc_vec2;
	ImVector<iam_instance_data::vec4_entry> acc_vec4;
	// Weight sums per channel (for normalization)
	ImGuiStorage weight_float;
	ImGuiStorage weight_int;
	ImVector<iam_instance_data::vec2_entry> weight_vec2;  // stores weight in v.x
	ImVector<iam_instance_data::vec4_entry> weight_vec4;  // stores weight in v.x
} g_layer_state = { 0, 0 };

void iam_layer_begin(ImGuiID instance_id) {
	g_layer_state.target_id = instance_id;
	g_layer_state.total_weight = 0.0f;
	g_layer_state.acc_float.Clear();
	g_layer_state.acc_int.Clear();
	g_layer_state.acc_vec2.clear();
	g_layer_state.acc_vec4.clear();
	g_layer_state.weight_float.Clear();
	g_layer_state.weight_int.Clear();
	g_layer_state.weight_vec2.clear();
	g_layer_state.weight_vec4.clear();
}

void iam_layer_add(iam_instance inst, float weight) {
	using namespace iam_clip_detail;
	if (!inst.valid() || weight <= 0.0f) return;

	iam_instance_data* src = find_instance(inst.id());
	if (!src) return;

	g_layer_state.total_weight += weight;

	// Accumulate float values
	for (int i = 0; i < src->values_float.Data.Size; ++i) {
		IMGUI_STORAGE_PAIR& p = src->values_float.Data[i];
		ImGuiID ch = p.key;
		float val = *(float*)&p.val_i;
		float acc = g_layer_state.acc_float.GetFloat(ch, 0.0f);
		float w = g_layer_state.weight_float.GetFloat(ch, 0.0f);
		g_layer_state.acc_float.SetFloat(ch, acc + val * weight);
		g_layer_state.weight_float.SetFloat(ch, w + weight);
	}

	// Accumulate int values
	for (int i = 0; i < src->values_int.Data.Size; ++i) {
		IMGUI_STORAGE_PAIR& p = src->values_int.Data[i];
		ImGuiID ch = p.key;
		int val = p.val_i;
		float acc = (float)g_layer_state.acc_int.GetInt(ch, 0);
		float w = g_layer_state.weight_int.GetFloat(ch, 0.0f);
		g_layer_state.acc_int.SetInt(ch, (int)(acc + (float)val * weight));
		g_layer_state.weight_int.SetFloat(ch, w + weight);
	}

	// Accumulate vec2 values
	for (int i = 0; i < src->values_vec2.Size; ++i) {
		iam_instance_data::vec2_entry& e = src->values_vec2[i];
		// Find or create accumulator entry
		int found = -1;
		for (int j = 0; j < g_layer_state.acc_vec2.Size; ++j) {
			if (g_layer_state.acc_vec2[j].ch == e.ch) { found = j; break; }
		}
		if (found < 0) {
			iam_instance_data::vec2_entry acc_e = { e.ch, ImVec2(0, 0) };
			iam_instance_data::vec2_entry w_e = { e.ch, ImVec2(0, 0) };
			g_layer_state.acc_vec2.push_back(acc_e);
			g_layer_state.weight_vec2.push_back(w_e);
			found = g_layer_state.acc_vec2.Size - 1;
		}
		g_layer_state.acc_vec2[found].v.x += e.v.x * weight;
		g_layer_state.acc_vec2[found].v.y += e.v.y * weight;
		g_layer_state.weight_vec2[found].v.x += weight;
	}

	// Accumulate vec4 values
	for (int i = 0; i < src->values_vec4.Size; ++i) {
		iam_instance_data::vec4_entry& e = src->values_vec4[i];
		int found = -1;
		for (int j = 0; j < g_layer_state.acc_vec4.Size; ++j) {
			if (g_layer_state.acc_vec4[j].ch == e.ch) { found = j; break; }
		}
		if (found < 0) {
			iam_instance_data::vec4_entry acc_e = { e.ch, ImVec4(0, 0, 0, 0) };
			iam_instance_data::vec4_entry w_e = { e.ch, ImVec4(0, 0, 0, 0) };
			g_layer_state.acc_vec4.push_back(acc_e);
			g_layer_state.weight_vec4.push_back(w_e);
			found = g_layer_state.acc_vec4.Size - 1;
		}
		g_layer_state.acc_vec4[found].v.x += e.v.x * weight;
		g_layer_state.acc_vec4[found].v.y += e.v.y * weight;
		g_layer_state.acc_vec4[found].v.z += e.v.z * weight;
		g_layer_state.acc_vec4[found].v.w += e.v.w * weight;
		g_layer_state.weight_vec4[found].v.x += weight;
	}
}

void iam_layer_end(ImGuiID instance_id) {
	using namespace iam_clip_detail;
	if (g_layer_state.target_id != instance_id) return;
	if (g_layer_state.total_weight <= 0.0f) return;

	iam_instance_data* target = find_instance(instance_id);
	if (!target) return;

	// Normalize and write blended values
	target->blended_float.Clear();
	target->blended_int.Clear();
	target->blended_vec2.clear();
	target->blended_vec4.clear();

	// Floats
	for (int i = 0; i < g_layer_state.acc_float.Data.Size; ++i) {
		IMGUI_STORAGE_PAIR& p = g_layer_state.acc_float.Data[i];
		float w = g_layer_state.weight_float.GetFloat(p.key, 1.0f);
		float val = *(float*)&p.val_i / (w > 0.0f ? w : 1.0f);
		target->blended_float.SetFloat(p.key, val);
	}

	// Ints
	for (int i = 0; i < g_layer_state.acc_int.Data.Size; ++i) {
		IMGUI_STORAGE_PAIR& p = g_layer_state.acc_int.Data[i];
		float w = g_layer_state.weight_int.GetFloat(p.key, 1.0f);
		int val = (int)((float)p.val_i / (w > 0.0f ? w : 1.0f));
		target->blended_int.SetInt(p.key, val);
	}

	// Vec2s
	for (int i = 0; i < g_layer_state.acc_vec2.Size; ++i) {
		iam_instance_data::vec2_entry& e = g_layer_state.acc_vec2[i];
		float w = g_layer_state.weight_vec2[i].v.x;
		if (w <= 0.0f) w = 1.0f;
		iam_instance_data::vec2_entry out = { e.ch, ImVec2(e.v.x / w, e.v.y / w) };
		target->blended_vec2.push_back(out);
	}

	// Vec4s
	for (int i = 0; i < g_layer_state.acc_vec4.Size; ++i) {
		iam_instance_data::vec4_entry& e = g_layer_state.acc_vec4[i];
		float w = g_layer_state.weight_vec4[i].v.x;
		if (w <= 0.0f) w = 1.0f;
		iam_instance_data::vec4_entry out = { e.ch, ImVec4(e.v.x / w, e.v.y / w, e.v.z / w, e.v.w / w) };
		target->blended_vec4.push_back(out);
	}

	target->has_blended = true;
	g_layer_state.target_id = 0;
}

bool iam_get_blended_float(ImGuiID instance_id, ImGuiID channel, float* out) {
	using namespace iam_clip_detail;
	iam_instance_data* inst = find_instance(instance_id);
	if (!inst || !inst->has_blended || !out) return false;
	// Check if channel exists in blended data
	for (int i = 0; i < inst->blended_float.Data.Size; ++i) {
		if (inst->blended_float.Data[i].key == channel) {
			*out = *(float*)&inst->blended_float.Data[i].val_i;
			return true;
		}
	}
	return false;
}

bool iam_get_blended_vec2(ImGuiID instance_id, ImGuiID channel, ImVec2* out) {
	using namespace iam_clip_detail;
	iam_instance_data* inst = find_instance(instance_id);
	if (!inst || !inst->has_blended || !out) return false;
	for (int i = 0; i < inst->blended_vec2.Size; ++i) {
		if (inst->blended_vec2[i].ch == channel) {
			*out = inst->blended_vec2[i].v;
			return true;
		}
	}
	return false;
}

bool iam_get_blended_vec4(ImGuiID instance_id, ImGuiID channel, ImVec4* out) {
	using namespace iam_clip_detail;
	iam_instance_data* inst = find_instance(instance_id);
	if (!inst || !inst->has_blended || !out) return false;
	for (int i = 0; i < inst->blended_vec4.Size; ++i) {
		if (inst->blended_vec4[i].ch == channel) {
			*out = inst->blended_vec4[i].v;
			return true;
		}
	}
	return false;
}

bool iam_get_blended_int(ImGuiID instance_id, ImGuiID channel, int* out) {
	using namespace iam_clip_detail;
	iam_instance_data* inst = find_instance(instance_id);
	if (!inst || !inst->has_blended || !out) return false;
	for (int i = 0; i < inst->blended_int.Data.Size; ++i) {
		if (inst->blended_int.Data[i].key == channel) {
			*out = inst->blended_int.Data[i].val_i;
			return true;
		}
	}
	return false;
}

// Persistence - binary format
// Header: "IAMC" (4 bytes) + version (4 bytes) + clip_id (4 bytes)
// Clip data: duration, delay, loop_count, direction, stagger params
// Tracks: count + for each: channel, type, num_keys, keys...

static char const IAM_CLIP_MAGIC[4] = { 'I', 'A', 'M', 'C' };
static int const IAM_CLIP_VERSION = 3;

iam_result iam_clip_save(ImGuiID clip_id, char const* path) {
	using namespace iam_clip_detail;
	iam_clip_data* clip = find_clip(clip_id);
	if (!clip) return iam_err_not_found;
	if (!path) return iam_err_bad_arg;

	FILE* f = nullptr;
        if (fopen_s(&f, path, "wb") != 0 || !f) return iam_err_bad_arg;

	// Write header
	fwrite(IAM_CLIP_MAGIC, 1, 4, f);
	int version = IAM_CLIP_VERSION;
	fwrite(&version, sizeof(int), 1, f);
	fwrite(&clip_id, sizeof(ImGuiID), 1, f);

	// Write clip properties
	fwrite(&clip->duration, sizeof(float), 1, f);
	fwrite(&clip->delay, sizeof(float), 1, f);
	fwrite(&clip->loop_count, sizeof(int), 1, f);
	fwrite(&clip->direction, sizeof(int), 1, f);
	fwrite(&clip->stagger_count, sizeof(int), 1, f);
	fwrite(&clip->stagger_delay, sizeof(float), 1, f);
	fwrite(&clip->stagger_center_bias, sizeof(float), 1, f);

	// Write tracks
	int track_count = clip->iam_tracks.Size;
	fwrite(&track_count, sizeof(int), 1, f);

	for (int t = 0; t < track_count; ++t) {
		iam_track& trk = clip->iam_tracks[t];
		fwrite(&trk.channel, sizeof(ImGuiID), 1, f);
		fwrite(&trk.type, sizeof(int), 1, f);

		int key_count = trk.keys.Size;
		fwrite(&key_count, sizeof(int), 1, f);

		for (int k = 0; k < key_count; ++k) {
			keyframe& kf = trk.keys[k];
			fwrite(&kf.time, sizeof(float), 1, f);
			fwrite(&kf.ease_type, sizeof(int), 1, f);
			// Write bools as int to avoid size/alignment issues
			int has_bezier_i = kf.has_bezier ? 1 : 0;
			int is_spring_i = kf.is_spring ? 1 : 0;
			fwrite(&has_bezier_i, sizeof(int), 1, f);
			fwrite(kf.bezier, sizeof(float), 4, f);
			fwrite(&is_spring_i, sizeof(int), 1, f);
			fwrite(&kf.spring.mass, sizeof(float), 1, f);
			fwrite(&kf.spring.stiffness, sizeof(float), 1, f);
			fwrite(&kf.spring.damping, sizeof(float), 1, f);
			fwrite(&kf.spring.initial_velocity, sizeof(float), 1, f);
			fwrite(kf.value, sizeof(float), 4, f);
		}
	}

	// Note: callbacks cannot be serialized

	fclose(f);
	return iam_ok;
}

iam_result iam_clip_load(char const* path, ImGuiID* out_clip_id) {
	using namespace iam_clip_detail;
	if (!path || !out_clip_id) return iam_err_bad_arg;

	FILE* f = nullptr;
        if (fopen_s(&f, path, "rb") != 0 || !f) return iam_err_not_found;

	// Read and verify header
	char magic[4];
	if (fread(magic, 1, 4, f) != 4 || memcmp(magic, IAM_CLIP_MAGIC, 4) != 0) {
		fclose(f);
		return iam_err_bad_arg;
	}

	int version;
	if (fread(&version, sizeof(int), 1, f) != 1 || version != IAM_CLIP_VERSION) {
		fclose(f);
		return iam_err_bad_arg;
	}

	ImGuiID clip_id;
	if (fread(&clip_id, sizeof(ImGuiID), 1, f) != 1) {
		fclose(f);
		return iam_err_bad_arg;
	}

	// Initialize system if needed
	if (!g_clip_sys.initialized) iam_clip_init();

	// Create or get clip
	int idx = g_clip_sys.clip_map.GetInt(clip_id, 0);
	iam_clip_data* clip;
	if (idx == 0) {
		g_clip_sys.clips.push_back(iam_clip_data());
		clip = &g_clip_sys.clips.back();
		clip->id = clip_id;
		g_clip_sys.clip_map.SetInt(clip_id, g_clip_sys.clips.Size);
	} else {
		clip = &g_clip_sys.clips[idx - 1];
		clip->iam_tracks.clear();
	}

	// Read clip properties
	fread(&clip->duration, sizeof(float), 1, f);
	fread(&clip->delay, sizeof(float), 1, f);
	fread(&clip->loop_count, sizeof(int), 1, f);
	fread(&clip->direction, sizeof(int), 1, f);
	fread(&clip->stagger_count, sizeof(int), 1, f);
	fread(&clip->stagger_delay, sizeof(float), 1, f);
	fread(&clip->stagger_center_bias, sizeof(float), 1, f);

	// Read tracks
	int track_count;
	if (fread(&track_count, sizeof(int), 1, f) != 1) {
		fclose(f);
		return iam_err_bad_arg;
	}

	for (int t = 0; t < track_count; ++t) {
		// Add track to clip first, then work with it directly to avoid ImVector copy issues
		clip->iam_tracks.push_back(iam_track());
		iam_track& trk = clip->iam_tracks.back();

		fread(&trk.channel, sizeof(ImGuiID), 1, f);
		fread(&trk.type, sizeof(int), 1, f);

		int key_count;
		fread(&key_count, sizeof(int), 1, f);

		for (int k = 0; k < key_count; ++k) {
			keyframe kf;
			fread(&kf.time, sizeof(float), 1, f);
			fread(&kf.ease_type, sizeof(int), 1, f);
			// Read bools as int to match save format
			int has_bezier_i, is_spring_i;
			fread(&has_bezier_i, sizeof(int), 1, f);
			fread(kf.bezier, sizeof(float), 4, f);
			fread(&is_spring_i, sizeof(int), 1, f);
			fread(&kf.spring.mass, sizeof(float), 1, f);
			fread(&kf.spring.stiffness, sizeof(float), 1, f);
			fread(&kf.spring.damping, sizeof(float), 1, f);
			fread(&kf.spring.initial_velocity, sizeof(float), 1, f);
			fread(kf.value, sizeof(float), 4, f);
			kf.has_bezier = (has_bezier_i != 0);
			kf.is_spring = (is_spring_i != 0);
			kf.channel = trk.channel;
			kf.type = trk.type;
			trk.keys.push_back(kf);
		}
	}

	fclose(f);
	*out_clip_id = clip_id;
	return iam_ok;
}

// ----------------------------------------------------
// Oscillators
// ----------------------------------------------------
namespace iam_osc_detail {

struct osc_state {
	float time;
	unsigned last_frame;
	osc_state() : time(0), last_frame(0) {}
};

static ImGuiStorage g_osc_map;

static osc_state* get_osc(ImGuiID id) {
	osc_state* s = (osc_state*)g_osc_map.GetVoidPtr(id);
	if (!s) {
		s = IM_NEW(osc_state)();
		g_osc_map.SetVoidPtr(id, s);
	}
	return s;
}

static float eval_wave(int wave_type, float t) {
	// t is in [0, 1) representing one period
	t = t - ImFloor(t); // wrap to [0, 1)
	switch (wave_type) {
		case iam_wave_sine:
			return ImSin(t * 2.0f * IM_PI);
		case iam_wave_triangle:
			return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
		case iam_wave_sawtooth:
			return 2.0f * t - 1.0f;
		case iam_wave_square:
			return (t < 0.5f) ? 1.0f : -1.0f;
		default:
			return ImSin(t * 2.0f * IM_PI);
	}
}

} // namespace iam_osc_detail

float iam_oscillate(ImGuiID id, float amplitude, float frequency, int wave_type, float phase, float dt) {
	using namespace iam_osc_detail;
	dt *= iam_detail::g_time_scale;
	osc_state* s = get_osc(id);
	if (s->last_frame != iam_detail::g_frame) {
		s->time += dt;
		s->last_frame = iam_detail::g_frame;
	}
	float t = s->time * frequency + phase;
	return amplitude * eval_wave(wave_type, t);
}int iam_oscillate_int(ImGuiID id, int amplitude, float frequency, int wave_type, float phase, float dt) {	float result = iam_oscillate(id, (float)amplitude, frequency, wave_type, phase, dt);	return (int)(result + 0.5f * (result > 0 ? 1 : -1));  // Round to nearest int
}

ImVec2 iam_oscillate_vec2(ImGuiID id, ImVec2 amplitude, ImVec2 frequency, int wave_type, ImVec2 phase, float dt) {
	using namespace iam_osc_detail;
	dt *= iam_detail::g_time_scale;
	osc_state* s = get_osc(id);
	if (s->last_frame != iam_detail::g_frame) {
		s->time += dt;
		s->last_frame = iam_detail::g_frame;
	}
	float tx = s->time * frequency.x + phase.x;
	float ty = s->time * frequency.y + phase.y;
	return ImVec2(amplitude.x * eval_wave(wave_type, tx), amplitude.y * eval_wave(wave_type, ty));
}

ImVec4 iam_oscillate_vec4(ImGuiID id, ImVec4 amplitude, ImVec4 frequency, int wave_type, ImVec4 phase, float dt) {
	using namespace iam_osc_detail;
	dt *= iam_detail::g_time_scale;
	osc_state* s = get_osc(id);
	if (s->last_frame != iam_detail::g_frame) {
		s->time += dt;
		s->last_frame = iam_detail::g_frame;
	}
	float tx = s->time * frequency.x + phase.x;
	float ty = s->time * frequency.y + phase.y;
	float tz = s->time * frequency.z + phase.z;
	float tw = s->time * frequency.w + phase.w;
	return ImVec4(
		amplitude.x * eval_wave(wave_type, tx),
		amplitude.y * eval_wave(wave_type, ty),
		amplitude.z * eval_wave(wave_type, tz),
		amplitude.w * eval_wave(wave_type, tw)
	);
}

ImVec4 iam_oscillate_color(ImGuiID id, ImVec4 base_color, ImVec4 amplitude, float frequency, int wave_type, float phase, int color_space, float dt) {
	using namespace iam_osc_detail;
	dt *= iam_detail::g_time_scale;
	osc_state* s = get_osc(id);
	if (s->last_frame != iam_detail::g_frame) {
		s->time += dt;
		s->last_frame = iam_detail::g_frame;
	}
	float t = s->time * frequency + phase;
	float wave = eval_wave(wave_type, t);

	// Convert base color to target color space, apply oscillation, convert back
	ImVec4 working;
	switch (color_space) {
		case iam_col_srgb_linear:
			working = iam_detail::color::srgb_to_linear(base_color);
			working.x += amplitude.x * wave;
			working.y += amplitude.y * wave;
			working.z += amplitude.z * wave;
			working.w += amplitude.w * wave;
			return iam_detail::color::linear_to_srgb(working);
		case iam_col_hsv:
			working = iam_detail::color::srgb_to_hsv(base_color);
			working.x = ImFmod(working.x + amplitude.x * wave + 1.0f, 1.0f);
			working.y = ImClamp(working.y + amplitude.y * wave, 0.0f, 1.0f);
			working.z = ImClamp(working.z + amplitude.z * wave, 0.0f, 1.0f);
			working.w = ImClamp(working.w + amplitude.w * wave, 0.0f, 1.0f);
			return iam_detail::color::hsv_to_srgb(working);
		case iam_col_oklab:
			working = iam_detail::color::srgb_to_oklab(base_color);
			working.x += amplitude.x * wave;
			working.y += amplitude.y * wave;
			working.z += amplitude.z * wave;
			working.w += amplitude.w * wave;
			return iam_detail::color::oklab_to_srgb(working);
		case iam_col_oklch:
			working = iam_detail::color::srgb_to_oklch(base_color);
			working.x += amplitude.x * wave;
			working.y += amplitude.y * wave;
			working.z = ImFmod(working.z + amplitude.z * wave + 1.0f, 1.0f);
			working.w += amplitude.w * wave;
			return iam_detail::color::oklch_to_srgb(working);
		default: // iam_col_srgb
			return ImVec4(
				ImClamp(base_color.x + amplitude.x * wave, 0.0f, 1.0f),
				ImClamp(base_color.y + amplitude.y * wave, 0.0f, 1.0f),
				ImClamp(base_color.z + amplitude.z * wave, 0.0f, 1.0f),
				ImClamp(base_color.w + amplitude.w * wave, 0.0f, 1.0f)
			);
	}
}

// ----------------------------------------------------
// Shake/Wiggle
// ----------------------------------------------------
namespace iam_shake_detail {

struct shake_state {
	float time_since_trigger;
	float noise_time;
	unsigned last_frame;
	bool triggered;
	// Simple noise state
	float noise_val[4];
	int noise_idx;
	shake_state() : time_since_trigger(0), noise_time(0), last_frame(0), triggered(false), noise_idx(0) {
		for (int i = 0; i < 4; i++) noise_val[i] = 0;
	}
};

static ImGuiStorage g_shake_map;

static shake_state* get_shake(ImGuiID id) {
	shake_state* s = (shake_state*)g_shake_map.GetVoidPtr(id);
	if (!s) {
		s = IM_NEW(shake_state)();
		g_shake_map.SetVoidPtr(id, s);
	}
	return s;
}

// Simple pseudo-random based on ID and time
static float hash_noise(unsigned int seed) {
	seed = (seed ^ 61) ^ (seed >> 16);
	seed = seed + (seed << 3);
	seed = seed ^ (seed >> 4);
	seed = seed * 0x27d4eb2d;
	seed = seed ^ (seed >> 15);
	return ((float)(seed & 0xFFFF) / 32768.0f) - 1.0f; // [-1, 1]
}

} // namespace iam_shake_detail

void iam_trigger_shake(ImGuiID id) {
	using namespace iam_shake_detail;
	shake_state* s = get_shake(id);
	s->triggered = true;
	s->time_since_trigger = 0;
}

float iam_shake(ImGuiID id, float intensity, float frequency, float decay_time, float dt) {
	using namespace iam_shake_detail;
	dt *= iam_detail::g_time_scale;
	shake_state* s = get_shake(id);

	if (s->last_frame != iam_detail::g_frame) {
		if (s->triggered) {
			s->time_since_trigger += dt;
		}
		s->noise_time += dt;
		s->last_frame = iam_detail::g_frame;
	}

	if (!s->triggered || s->time_since_trigger >= decay_time) {
		s->triggered = false;
		return 0.0f;
	}

	// Decay factor
	float decay = 1.0f - (s->time_since_trigger / decay_time);
	decay = decay * decay; // quadratic decay

	// Generate noise at frequency
	float period = 1.0f / frequency;
	int sample = (int)(s->noise_time / period);
	float frac = (s->noise_time - sample * period) / period;

	// Interpolate between noise samples
	float n0 = hash_noise((unsigned int)id + sample);
	float n1 = hash_noise((unsigned int)id + sample + 1);
	float noise = n0 + (n1 - n0) * frac; // linear interpolation

	return noise * intensity * decay;
}int iam_shake_int(ImGuiID id, int intensity, float frequency, float decay_time, float dt) {	float result = iam_shake(id, (float)intensity, frequency, decay_time, dt);	return (int)(result + 0.5f * (result > 0 ? 1 : -1));
}

ImVec2 iam_shake_vec2(ImGuiID id, ImVec2 intensity, float frequency, float decay_time, float dt) {
	ImGuiID id_x = id;
	ImGuiID id_y = id ^ 0x12345678;
	return ImVec2(
		iam_shake(id_x, intensity.x, frequency, decay_time, dt),
		iam_shake(id_y, intensity.y, frequency, decay_time, dt)
	);
}

ImVec4 iam_shake_vec4(ImGuiID id, ImVec4 intensity, float frequency, float decay_time, float dt) {
	ImGuiID id_x = id;
	ImGuiID id_y = id ^ 0x12345678;
	ImGuiID id_z = id ^ 0x23456789;
	ImGuiID id_w = id ^ 0x3456789A;
	return ImVec4(
		iam_shake(id_x, intensity.x, frequency, decay_time, dt),
		iam_shake(id_y, intensity.y, frequency, decay_time, dt),
		iam_shake(id_z, intensity.z, frequency, decay_time, dt),
		iam_shake(id_w, intensity.w, frequency, decay_time, dt)
	);
}

ImVec4 iam_shake_color(ImGuiID id, ImVec4 base_color, ImVec4 intensity, float frequency, float decay_time, int color_space, float dt) {
	ImVec4 shake = iam_shake_vec4(id, intensity, frequency, decay_time, dt);

	// Apply shake offset in the specified color space
	ImVec4 working;
	switch (color_space) {
		case iam_col_srgb_linear:
			working = iam_detail::color::srgb_to_linear(base_color);
			working.x += shake.x;
			working.y += shake.y;
			working.z += shake.z;
			working.w += shake.w;
			return iam_detail::color::linear_to_srgb(working);
		case iam_col_hsv:
			working = iam_detail::color::srgb_to_hsv(base_color);
			working.x = ImFmod(working.x + shake.x + 1.0f, 1.0f);
			working.y = ImClamp(working.y + shake.y, 0.0f, 1.0f);
			working.z = ImClamp(working.z + shake.z, 0.0f, 1.0f);
			working.w = ImClamp(working.w + shake.w, 0.0f, 1.0f);
			return iam_detail::color::hsv_to_srgb(working);
		case iam_col_oklab:
			working = iam_detail::color::srgb_to_oklab(base_color);
			working.x += shake.x;
			working.y += shake.y;
			working.z += shake.z;
			working.w += shake.w;
			return iam_detail::color::oklab_to_srgb(working);
		case iam_col_oklch:
			working = iam_detail::color::srgb_to_oklch(base_color);
			working.x += shake.x;
			working.y += shake.y;
			working.z = ImFmod(working.z + shake.z + 1.0f, 1.0f);
			working.w += shake.w;
			return iam_detail::color::oklch_to_srgb(working);
		default: // iam_col_srgb
			return ImVec4(
				ImClamp(base_color.x + shake.x, 0.0f, 1.0f),
				ImClamp(base_color.y + shake.y, 0.0f, 1.0f),
				ImClamp(base_color.z + shake.z, 0.0f, 1.0f),
				ImClamp(base_color.w + shake.w, 0.0f, 1.0f)
			);
	}
}

float iam_wiggle(ImGuiID id, float amplitude, float frequency, float dt) {
	using namespace iam_shake_detail;
	dt *= iam_detail::g_time_scale;
	shake_state* s = get_shake(id);

	if (s->last_frame != iam_detail::g_frame) {
		s->noise_time += dt;
		s->last_frame = iam_detail::g_frame;
	}

	// Generate smooth continuous noise
	float period = 1.0f / frequency;
	int sample = (int)(s->noise_time / period);
	float frac = (s->noise_time - sample * period) / period;

	// Smoothstep interpolation for smoother movement
	float t = frac * frac * (3.0f - 2.0f * frac);

	float n0 = hash_noise((unsigned int)id + sample);
	float n1 = hash_noise((unsigned int)id + sample + 1);

	return amplitude * (n0 + (n1 - n0) * t);
}int iam_wiggle_int(ImGuiID id, int amplitude, float frequency, float dt) {	float result = iam_wiggle(id, (float)amplitude, frequency, dt);	return (int)(result + 0.5f * (result > 0 ? 1 : -1));
}

ImVec2 iam_wiggle_vec2(ImGuiID id, ImVec2 amplitude, float frequency, float dt) {
	ImGuiID id_x = id;
	ImGuiID id_y = id ^ 0x12345678;
	return ImVec2(
		iam_wiggle(id_x, amplitude.x, frequency, dt),
		iam_wiggle(id_y, amplitude.y, frequency, dt)
	);
}

ImVec4 iam_wiggle_vec4(ImGuiID id, ImVec4 amplitude, float frequency, float dt) {
	ImGuiID id_x = id;
	ImGuiID id_y = id ^ 0x12345678;
	ImGuiID id_z = id ^ 0x23456789;
	ImGuiID id_w = id ^ 0x3456789A;
	return ImVec4(
		iam_wiggle(id_x, amplitude.x, frequency, dt),
		iam_wiggle(id_y, amplitude.y, frequency, dt),
		iam_wiggle(id_z, amplitude.z, frequency, dt),
		iam_wiggle(id_w, amplitude.w, frequency, dt)
	);
}

ImVec4 iam_wiggle_color(ImGuiID id, ImVec4 base_color, ImVec4 amplitude, float frequency, int color_space, float dt) {
	ImVec4 wiggle = iam_wiggle_vec4(id, amplitude, frequency, dt);

	// Apply wiggle offset in the specified color space
	ImVec4 working;
	switch (color_space) {
		case iam_col_srgb_linear:
			working = iam_detail::color::srgb_to_linear(base_color);
			working.x += wiggle.x;
			working.y += wiggle.y;
			working.z += wiggle.z;
			working.w += wiggle.w;
			return iam_detail::color::linear_to_srgb(working);
		case iam_col_hsv:
			working = iam_detail::color::srgb_to_hsv(base_color);
			working.x = ImFmod(working.x + wiggle.x + 1.0f, 1.0f);
			working.y = ImClamp(working.y + wiggle.y, 0.0f, 1.0f);
			working.z = ImClamp(working.z + wiggle.z, 0.0f, 1.0f);
			working.w = ImClamp(working.w + wiggle.w, 0.0f, 1.0f);
			return iam_detail::color::hsv_to_srgb(working);
		case iam_col_oklab:
			working = iam_detail::color::srgb_to_oklab(base_color);
			working.x += wiggle.x;
			working.y += wiggle.y;
			working.z += wiggle.z;
			working.w += wiggle.w;
			return iam_detail::color::oklab_to_srgb(working);
		case iam_col_oklch:
			working = iam_detail::color::srgb_to_oklch(base_color);
			working.x += wiggle.x;
			working.y += wiggle.y;
			working.z = ImFmod(working.z + wiggle.z + 1.0f, 1.0f);
			working.w += wiggle.w;
			return iam_detail::color::oklch_to_srgb(working);
		default: // iam_col_srgb
			return ImVec4(
				ImClamp(base_color.x + wiggle.x, 0.0f, 1.0f),
				ImClamp(base_color.y + wiggle.y, 0.0f, 1.0f),
				ImClamp(base_color.z + wiggle.z, 0.0f, 1.0f),
				ImClamp(base_color.w + wiggle.w, 0.0f, 1.0f)
			);
	}
}

// ----------------------------------------------------
// Scroll Animation
// ----------------------------------------------------
namespace iam_scroll_detail {

struct scroll_anim {
	ImGuiID window_id;
	float start_x, start_y;
	float target_x, target_y;
	float duration;
	float elapsed;
	iam_ease_desc ease;
	bool active_x, active_y;
	unsigned last_frame;
};

static ImVector<scroll_anim> g_scroll_anims;

static scroll_anim* find_or_create(ImGuiID window_id) {
	for (int i = 0; i < g_scroll_anims.Size; i++) {
		if (g_scroll_anims[i].window_id == window_id) {
			return &g_scroll_anims[i];
		}
	}
	scroll_anim sa;
	sa.window_id = window_id;
	sa.active_x = sa.active_y = false;
	sa.elapsed = 0;
	sa.last_frame = 0;
	g_scroll_anims.push_back(sa);
	return &g_scroll_anims.back();
}

} // namespace iam_scroll_detail

void iam_scroll_to_y(float target_y, float duration, iam_ease_desc const& ez) {
	using namespace iam_scroll_detail;
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window) return;

	scroll_anim* sa = find_or_create(window->ID);
	sa->start_y = window->Scroll.y;
	sa->target_y = target_y;
	sa->duration = duration;
	sa->elapsed = 0;
	sa->ease = ez;
	sa->active_y = true;
}

void iam_scroll_to_x(float target_x, float duration, iam_ease_desc const& ez) {
	using namespace iam_scroll_detail;
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window) return;

	scroll_anim* sa = find_or_create(window->ID);
	sa->start_x = window->Scroll.x;
	sa->target_x = target_x;
	sa->duration = duration;
	sa->elapsed = 0;
	sa->ease = ez;
	sa->active_x = true;
}

void iam_scroll_to_top(float duration, iam_ease_desc const& ez) {
	iam_scroll_to_y(0.0f, duration, ez);
}

void iam_scroll_to_bottom(float duration, iam_ease_desc const& ez) {
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window) return;
	float max_y = window->ScrollMax.y;
	iam_scroll_to_y(max_y, duration, ez);
}

// Call this in iam_update_begin_frame to process scroll animations
static void iam_scroll_update_internal(float dt) {
	using namespace iam_scroll_detail;
	dt *= iam_detail::g_time_scale;

	for (int i = g_scroll_anims.Size - 1; i >= 0; i--) {
		scroll_anim& sa = g_scroll_anims[i];
		if (!sa.active_x && !sa.active_y) {
			g_scroll_anims.erase(&g_scroll_anims[i]);
			continue;
		}

		sa.elapsed += dt;
		float t = (sa.duration > 0) ? ImClamp(sa.elapsed / sa.duration, 0.0f, 1.0f) : 1.0f;
		float eased_t = iam_detail::eval(sa.ease, t);

		ImGuiWindow* window = ImGui::FindWindowByID(sa.window_id);
		if (!window) {
			sa.active_x = sa.active_y = false;
			continue;
		}

		if (sa.active_y) {
			float new_y = sa.start_y + (sa.target_y - sa.start_y) * eased_t;
			window->Scroll.y = new_y;
			if (t >= 1.0f) sa.active_y = false;
		}

		if (sa.active_x) {
			float new_x = sa.start_x + (sa.target_x - sa.start_x) * eased_t;
			window->Scroll.x = new_x;
			if (t >= 1.0f) sa.active_x = false;
		}
	}
}

// ----------------------------------------------------
// Motion Paths - animate along curves and splines
// ----------------------------------------------------
namespace iam_path_detail {

// Evaluate quadratic bezier: B(t) = (1-t)²P₀ + 2(1-t)tP₁ + t²P₂
static ImVec2 eval_quadratic(ImVec2 p0, ImVec2 p1, ImVec2 p2, float t) {
	float u = 1.0f - t;
	float tt = t * t;
	float uu = u * u;
	float ut2 = 2.0f * u * t;
	return ImVec2(
		uu * p0.x + ut2 * p1.x + tt * p2.x,
		uu * p0.y + ut2 * p1.y + tt * p2.y
	);
}

// Derivative of quadratic bezier: B'(t) = 2(1-t)(P₁-P₀) + 2t(P₂-P₁)
static ImVec2 eval_quadratic_deriv(ImVec2 p0, ImVec2 p1, ImVec2 p2, float t) {
	float u = 1.0f - t;
	return ImVec2(
		2.0f * u * (p1.x - p0.x) + 2.0f * t * (p2.x - p1.x),
		2.0f * u * (p1.y - p0.y) + 2.0f * t * (p2.y - p1.y)
	);
}

// Evaluate cubic bezier: B(t) = (1-t)³P₀ + 3(1-t)²tP₁ + 3(1-t)t²P₂ + t³P₃
static ImVec2 eval_cubic(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
	float u = 1.0f - t;
	float tt = t * t;
	float ttt = tt * t;
	float uu = u * u;
	float uuu = uu * u;
	return ImVec2(
		uuu * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + ttt * p3.x,
		uuu * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + ttt * p3.y
	);
}

// Derivative of cubic bezier: B'(t) = 3(1-t)²(P₁-P₀) + 6(1-t)t(P₂-P₁) + 3t²(P₃-P₂)
static ImVec2 eval_cubic_deriv(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
	float u = 1.0f - t;
	float uu = u * u;
	float tt = t * t;
	return ImVec2(
		3.0f * uu * (p1.x - p0.x) + 6.0f * u * t * (p2.x - p1.x) + 3.0f * tt * (p3.x - p2.x),
		3.0f * uu * (p1.y - p0.y) + 6.0f * u * t * (p2.y - p1.y) + 3.0f * tt * (p3.y - p2.y)
	);
}

// Evaluate Catmull-Rom spline (goes through p1 and p2)
static ImVec2 eval_catmull_rom(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t, float tension) {
	float t2 = t * t;
	float t3 = t2 * t;
	float s = (1.0f - tension) / 2.0f;

	// Catmull-Rom basis functions
	float h1 = -s * t3 + 2.0f * s * t2 - s * t;
	float h2 = (2.0f - s) * t3 + (s - 3.0f) * t2 + 1.0f;
	float h3 = (s - 2.0f) * t3 + (3.0f - 2.0f * s) * t2 + s * t;
	float h4 = s * t3 - s * t2;

	return ImVec2(
		h1 * p0.x + h2 * p1.x + h3 * p2.x + h4 * p3.x,
		h1 * p0.y + h2 * p1.y + h3 * p2.y + h4 * p3.y
	);
}

// Derivative of Catmull-Rom
static ImVec2 eval_catmull_rom_deriv(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t, float tension) {
	float t2 = t * t;
	float s = (1.0f - tension) / 2.0f;

	// Derivatives of basis functions
	float dh1 = -3.0f * s * t2 + 4.0f * s * t - s;
	float dh2 = 3.0f * (2.0f - s) * t2 + 2.0f * (s - 3.0f) * t;
	float dh3 = 3.0f * (s - 2.0f) * t2 + 2.0f * (3.0f - 2.0f * s) * t + s;
	float dh4 = 3.0f * s * t2 - 2.0f * s * t;

	return ImVec2(
		dh1 * p0.x + dh2 * p1.x + dh3 * p2.x + dh4 * p3.x,
		dh1 * p0.y + dh2 * p1.y + dh3 * p2.y + dh4 * p3.y
	);
}

// Path segment storage
struct path_segment {
	int type;           // iam_path_segment_type
	ImVec2 p0, p1, p2, p3;  // Control points (usage depends on type)
	float tension;      // For catmull-rom
	float length;       // Approximate segment length

	ImVec2 evaluate(float t) const {
		switch (type) {
			case iam_seg_line:
				return ImVec2(p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t);
			case iam_seg_quadratic_bezier:
				return eval_quadratic(p0, p1, p2, t);
			case iam_seg_cubic_bezier:
				return eval_cubic(p0, p1, p2, p3, t);
			case iam_seg_catmull_rom:
				return eval_catmull_rom(p0, p1, p2, p3, t, tension);
			default:
				return p0;
		}
	}

	ImVec2 derivative(float t) const {
		switch (type) {
			case iam_seg_line:
				return ImVec2(p1.x - p0.x, p1.y - p0.y);
			case iam_seg_quadratic_bezier:
				return eval_quadratic_deriv(p0, p1, p2, t);
			case iam_seg_cubic_bezier:
				return eval_cubic_deriv(p0, p1, p2, p3, t);
			case iam_seg_catmull_rom:
				return eval_catmull_rom_deriv(p0, p1, p2, p3, t, tension);
			default:
				return ImVec2(1, 0);
		}
	}
};

// Approximate segment length using subdivision
static float approx_segment_length(path_segment const& seg, int subdivisions = 16) {
	float len = 0.0f;
	ImVec2 prev = seg.evaluate(0.0f);
	for (int i = 1; i <= subdivisions; i++) {
		float t = (float)i / (float)subdivisions;
		ImVec2 cur = seg.evaluate(t);
		float dx = cur.x - prev.x;
		float dy = cur.y - prev.y;
		len += ImSqrt(dx * dx + dy * dy);
		prev = cur;
	}
	return len;
}

// Arc-length LUT entry
struct arc_lut_entry {
	float distance;   // Cumulative arc-length distance
	float t;          // Global t parameter [0,1]
};

// Path data storage
struct path_data {
	ImVector<path_segment> segments;
	ImVec2 start_point;
	float total_length;
	bool closed;

	// Arc-length LUT for constant-speed evaluation
	ImVector<arc_lut_entry> arc_lut;
	bool has_arc_lut;

	path_data() : start_point(0, 0), total_length(0), closed(false), has_arc_lut(false) {}

	void compute_lengths() {
		total_length = 0;
		for (int i = 0; i < segments.Size; i++) {
			segments[i].length = approx_segment_length(segments[i]);
			total_length += segments[i].length;
		}
	}

	// Find segment and local t for global t in [0,1]
	void find_segment(float global_t, int* out_seg_idx, float* out_local_t) const {
		if (segments.Size == 0) {
			*out_seg_idx = -1;
			*out_local_t = 0;
			return;
		}
		if (global_t <= 0.0f) {
			*out_seg_idx = 0;
			*out_local_t = 0;
			return;
		}
		if (global_t >= 1.0f) {
			*out_seg_idx = segments.Size - 1;
			*out_local_t = 1.0f;
			return;
		}

		float target_dist = global_t * total_length;
		float accum = 0;
		for (int i = 0; i < segments.Size; i++) {
			if (accum + segments[i].length >= target_dist) {
				*out_seg_idx = i;
				float local_dist = target_dist - accum;
				*out_local_t = (segments[i].length > 0) ? (local_dist / segments[i].length) : 0;
				return;
			}
			accum += segments[i].length;
		}
		// Fallback
		*out_seg_idx = segments.Size - 1;
		*out_local_t = 1.0f;
	}

	ImVec2 evaluate(float t) const {
		int seg_idx;
		float local_t;
		find_segment(t, &seg_idx, &local_t);
		if (seg_idx < 0) return start_point;
		return segments[seg_idx].evaluate(local_t);
	}

	ImVec2 derivative(float t) const {
		int seg_idx;
		float local_t;
		find_segment(t, &seg_idx, &local_t);
		if (seg_idx < 0) return ImVec2(1, 0);
		return segments[seg_idx].derivative(local_t);
	}

	// Build arc-length lookup table for constant-speed evaluation
	void build_arc_lut(int subdivisions) {
		arc_lut.clear();
		if (segments.Size == 0 || total_length <= 0) {
			has_arc_lut = false;
			return;
		}

		// Sample path at regular t intervals and record cumulative distance
		arc_lut_entry first;
		first.distance = 0;
		first.t = 0;
		arc_lut.push_back(first);

		ImVec2 prev = evaluate(0.0f);
		float cumulative = 0;

		for (int i = 1; i <= subdivisions; i++) {
			float t = (float)i / (float)subdivisions;
			ImVec2 cur = evaluate(t);
			float dx = cur.x - prev.x;
			float dy = cur.y - prev.y;
			cumulative += ImSqrt(dx * dx + dy * dy);

			arc_lut_entry entry;
			entry.distance = cumulative;
			entry.t = t;
			arc_lut.push_back(entry);

			prev = cur;
		}

		// Update total_length from LUT (more accurate)
		if (arc_lut.Size > 0) {
			total_length = arc_lut[arc_lut.Size - 1].distance;
		}
		has_arc_lut = true;
	}

	// Convert arc-length distance to t parameter using LUT (binary search)
	float distance_to_t(float distance) const {
		if (!has_arc_lut || arc_lut.Size < 2) {
			// Fallback: linear approximation
			return (total_length > 0) ? ImClamp(distance / total_length, 0.0f, 1.0f) : 0.0f;
		}

		if (distance <= 0) return 0.0f;
		if (distance >= total_length) return 1.0f;

		// TODO: Add a define to choose between binary and
		// an tabulated inverse CDF read, that's an approximation
		// but faster for real-time.

		// Binary search for the interval containing this distance
		int lo = 0, hi = arc_lut.Size - 1;
		while (lo < hi - 1) {
			int mid = (lo + hi) / 2;
			if (arc_lut[mid].distance < distance) {
				lo = mid;
			} else {
				hi = mid;
			}
		}

		// Linear interpolation within the interval
		float d0 = arc_lut[lo].distance;
		float d1 = arc_lut[hi].distance;
		float t0 = arc_lut[lo].t;
		float t1 = arc_lut[hi].t;

		if (d1 - d0 <= 0) return t0;
		float u = (distance - d0) / (d1 - d0);
		return t0 + (t1 - t0) * u;
	}

	// Evaluate at arc-length distance
	ImVec2 evaluate_at_distance(float distance) const {
		float t = distance_to_t(distance);
		return evaluate(t);
	}

	// Get angle at arc-length distance
	float angle_at_distance(float distance) const {
		float t = distance_to_t(distance);
		ImVec2 d = derivative(t);
		return ImAtan2(d.y, d.x);
	}

	// Get tangent at arc-length distance
	ImVec2 tangent_at_distance(float distance) const {
		float t = distance_to_t(distance);
		ImVec2 d = derivative(t);
		float len = ImSqrt(d.x * d.x + d.y * d.y);
		if (len > 0.0001f) {
			d.x /= len;
			d.y /= len;
		}
		return d;
	}
};

// Global path storage
static ImPool<path_data> g_paths;
static ImGuiStorage g_path_map;

// Currently building path
static path_data* g_building_path = nullptr;
static ImGuiID g_building_path_id = 0;
static ImVec2 g_current_point;
static ImVector<ImVec2> g_catmull_points;  // For collecting catmull-rom points

static path_data* get_path(ImGuiID path_id) {
	int idx = g_path_map.GetInt(path_id, -1);
	if (idx < 0) return nullptr;
	return g_paths.GetByIndex(idx);
}

} // namespace iam_path_detail

// Public curve evaluation functions
ImVec2 iam_bezier_quadratic(ImVec2 p0, ImVec2 p1, ImVec2 p2, float t) {
	return iam_path_detail::eval_quadratic(p0, p1, p2, t);
}

ImVec2 iam_bezier_cubic(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
	return iam_path_detail::eval_cubic(p0, p1, p2, p3, t);
}

ImVec2 iam_catmull_rom(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t, float tension) {
	return iam_path_detail::eval_catmull_rom(p0, p1, p2, p3, t, tension);
}

ImVec2 iam_bezier_quadratic_deriv(ImVec2 p0, ImVec2 p1, ImVec2 p2, float t) {
	return iam_path_detail::eval_quadratic_deriv(p0, p1, p2, t);
}

ImVec2 iam_bezier_cubic_deriv(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
	return iam_path_detail::eval_cubic_deriv(p0, p1, p2, p3, t);
}

ImVec2 iam_catmull_rom_deriv(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t, float tension) {
	return iam_path_detail::eval_catmull_rom_deriv(p0, p1, p2, p3, t, tension);
}

// iam_path fluent builder
iam_path iam_path::begin(ImGuiID path_id, ImVec2 start) {
	using namespace iam_path_detail;

	// Clean up existing path if re-defining
	int existing_idx = g_path_map.GetInt(path_id, -1);
	if (existing_idx >= 0) {
		g_paths.Remove(path_id, g_paths.GetByIndex(existing_idx));
	}

	// Create new path
	g_building_path = g_paths.GetOrAddByKey(path_id);
	g_building_path_id = path_id;
	g_building_path->segments.clear();
	g_building_path->start_point = start;
	g_building_path->total_length = 0;
	g_building_path->closed = false;
	g_current_point = start;
	g_catmull_points.clear();
	g_catmull_points.push_back(start);

	return iam_path(path_id);
}

iam_path& iam_path::line_to(ImVec2 end) {
	using namespace iam_path_detail;
	if (!g_building_path) return *this;

	path_segment seg;
	seg.type = iam_seg_line;
	seg.p0 = g_current_point;
	seg.p1 = end;
	seg.tension = 0;
	g_building_path->segments.push_back(seg);
	g_current_point = end;
	g_catmull_points.push_back(end);

	return *this;
}

iam_path& iam_path::quadratic_to(ImVec2 ctrl, ImVec2 end) {
	using namespace iam_path_detail;
	if (!g_building_path) return *this;

	path_segment seg;
	seg.type = iam_seg_quadratic_bezier;
	seg.p0 = g_current_point;
	seg.p1 = ctrl;
	seg.p2 = end;
	seg.tension = 0;
	g_building_path->segments.push_back(seg);
	g_current_point = end;
	g_catmull_points.push_back(end);

	return *this;
}

iam_path& iam_path::cubic_to(ImVec2 ctrl1, ImVec2 ctrl2, ImVec2 end) {
	using namespace iam_path_detail;
	if (!g_building_path) return *this;

	path_segment seg;
	seg.type = iam_seg_cubic_bezier;
	seg.p0 = g_current_point;
	seg.p1 = ctrl1;
	seg.p2 = ctrl2;
	seg.p3 = end;
	seg.tension = 0;
	g_building_path->segments.push_back(seg);
	g_current_point = end;
	g_catmull_points.push_back(end);

	return *this;
}

iam_path& iam_path::catmull_to(ImVec2 end, float tension) {
	using namespace iam_path_detail;
	if (!g_building_path) return *this;

	// For catmull-rom, we need 4 points. Use previous points as context
	int n = g_catmull_points.Size;
	ImVec2 p0 = (n >= 2) ? g_catmull_points[n - 2] : g_current_point;
	ImVec2 p1 = g_current_point;
	ImVec2 p2 = end;
	ImVec2 p3 = end;  // Will be updated by next point or mirror

	path_segment seg;
	seg.type = iam_seg_catmull_rom;
	seg.p0 = p0;
	seg.p1 = p1;
	seg.p2 = p2;
	seg.p3 = p3;
	seg.tension = tension;
	g_building_path->segments.push_back(seg);
	g_current_point = end;
	g_catmull_points.push_back(end);

	// Update previous catmull-rom segment's p3 if any
	int seg_count = g_building_path->segments.Size;
	if (seg_count >= 2) {
		path_segment& prev = g_building_path->segments[seg_count - 2];
		if (prev.type == iam_seg_catmull_rom) {
			prev.p3 = end;
		}
	}

	return *this;
}

iam_path& iam_path::close() {
	using namespace iam_path_detail;
	if (!g_building_path) return *this;

	// Add line back to start if not already there
	if (g_current_point.x != g_building_path->start_point.x ||
		g_current_point.y != g_building_path->start_point.y) {
		line_to(g_building_path->start_point);
	}
	g_building_path->closed = true;

	return *this;
}

void iam_path::end() {
	using namespace iam_path_detail;
	if (!g_building_path) return;

	// Compute segment lengths
	g_building_path->compute_lengths();

	// Register in map
	g_path_map.SetInt(g_building_path_id, g_paths.GetIndex(g_building_path));

	g_building_path = nullptr;
	g_building_path_id = 0;
	g_catmull_points.clear();
}

// Path query functions
bool iam_path_exists(ImGuiID path_id) {
	return iam_path_detail::get_path(path_id) != nullptr;
}

float iam_path_length(ImGuiID path_id) {
	iam_path_detail::path_data* p = iam_path_detail::get_path(path_id);
	return p ? p->total_length : 0.0f;
}

ImVec2 iam_path_evaluate(ImGuiID path_id, float t) {
	iam_path_detail::path_data* p = iam_path_detail::get_path(path_id);
	return p ? p->evaluate(t) : ImVec2(0, 0);
}

ImVec2 iam_path_tangent(ImGuiID path_id, float t) {
	iam_path_detail::path_data* p = iam_path_detail::get_path(path_id);
	if (!p) return ImVec2(1, 0);

	ImVec2 d = p->derivative(t);
	float len = ImSqrt(d.x * d.x + d.y * d.y);
	if (len > 1e-6f) {
		d.x /= len;
		d.y /= len;
	}
	return d;
}

float iam_path_angle(ImGuiID path_id, float t) {
	ImVec2 tangent = iam_path_tangent(path_id, t);
	return atan2f(tangent.y, tangent.x);
}

// Tween along path
ImVec2 iam_tween_path(ImGuiID id, ImGuiID channel_id, ImGuiID path_id, float dur, iam_ease_desc const& ez, int policy, float dt) {
	using namespace iam_detail;

	// Apply global time scale
	dt *= g_time_scale;

	// Get path
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (!path || path->segments.Size == 0) {
		return ImVec2(0, 0);
	}

	// Use float channel to track progress (0 to 1)
	ImGuiID key = make_key(id, channel_id);
	float_chan* c = g_float.get(key);

	// Check if target changed (always 1.0 for path progress)
	float target = 1.0f;
	bool changed = (c->target != target);
	if (changed) {
		switch (c->policy) {
			case iam_policy_cut:
				c->current = 0.0f;  // Start from beginning
				c->set(target, dur, ez, policy);
				break;
			case iam_policy_queue:
				if (c->progress() < 1.0f && !c->has_pending) {
					c->has_pending = 1;
					c->pending_target = target;
				} else {
					c->set(target, dur, ez, policy);
				}
				break;
			default: // crossfade
				c->set(target, dur, ez, policy);
				break;
		}
	}

	c->tick(dt);

	// Handle pending
	if (c->has_pending && c->progress() >= 1.0f) {
		c->set(c->pending_target, dur, ez, policy);
		c->has_pending = 0;
	}

	// Evaluate path at current progress
	float progress = c->current;

	// Use arc-length parameterization for constant speed if LUT is available
	if (path->has_arc_lut) {
		float distance = progress * path->total_length;
		float t = path->distance_to_t(distance);
		return path->evaluate(t);
	}

	return path->evaluate(progress);
}

float iam_tween_path_angle(ImGuiID id, ImGuiID channel_id, ImGuiID path_id, float dur, iam_ease_desc const& ez, int policy, float dt) {
	using namespace iam_detail;

	// Apply global time scale
	dt *= g_time_scale;

	// Get path
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (!path || path->segments.Size == 0) {
		return 0.0f;
	}

	// Use float channel to track progress (0 to 1)
	// Use different channel for angle to allow independent queries
	ImGuiID angle_channel = ImHashStr("_angle", 0, channel_id);
	ImGuiID key = make_key(id, angle_channel);
	float_chan* c = g_float.get(key);

	float target = 1.0f;
	bool changed = (c->target != target);
	if (changed) {
		switch (c->policy) {
			case iam_policy_cut:
				c->current = 0.0f;
				c->set(target, dur, ez, policy);
				break;
			case iam_policy_queue:
				if (c->progress() < 1.0f && !c->has_pending) {
					c->has_pending = 1;
					c->pending_target = target;
				} else {
					c->set(target, dur, ez, policy);
				}
				break;
			default:
				c->set(target, dur, ez, policy);
				break;
		}
	}

	c->tick(dt);

	if (c->has_pending && c->progress() >= 1.0f) {
		c->set(c->pending_target, dur, ez, policy);
		c->has_pending = 0;
	}

	float progress = c->current;

	// Use arc-length parameterization for constant speed if LUT is available
	if (path->has_arc_lut) {
		float distance = progress * path->total_length;
		return iam_path_angle_at_distance(path_id, distance);
	}

	return iam_path_angle(path_id, progress);
}

// ----------------------------------------------------
// Arc-length parameterization
// ----------------------------------------------------

void iam_path_build_arc_lut(ImGuiID path_id, int subdivisions) {
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (path) {
		path->build_arc_lut(subdivisions);
	}
}

bool iam_path_has_arc_lut(ImGuiID path_id) {
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	return path && path->has_arc_lut;
}

float iam_path_distance_to_t(ImGuiID path_id, float distance) {
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (!path) return 0.0f;
	return path->distance_to_t(distance);
}

ImVec2 iam_path_evaluate_at_distance(ImGuiID path_id, float distance) {
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (!path) return ImVec2(0, 0);
	return path->evaluate_at_distance(distance);
}

float iam_path_angle_at_distance(ImGuiID path_id, float distance) {
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (!path) return 0.0f;
	return path->angle_at_distance(distance);
}

ImVec2 iam_path_tangent_at_distance(ImGuiID path_id, float distance) {
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (!path) return ImVec2(1, 0);
	return path->tangent_at_distance(distance);
}

// ============================================================
// PATH MORPHING - Interpolate between two paths
// ============================================================

namespace iam_morph_detail {

// State for morph tweens
struct morph_state {
	float blend;         // Current blend value (0 = path_a, 1 = path_b)
	float path_t;        // Current position along morphed path
	ImGuiID last_frame;
};

static ImPool<morph_state> g_morph_states;

morph_state* get_morph_state(ImGuiID id, ImGuiID channel_id) {
	ImGuiID key = iam_detail::make_key(id, channel_id);
	morph_state* s = g_morph_states.GetByKey(key);
	if (!s) {
		s = g_morph_states.GetOrAddByKey(key);
		s->blend = 0.0f;
		s->path_t = 0.0f;
		s->last_frame = 0;
	}
	return s;
}

// Sample a path at uniform intervals for morphing
void sample_path(ImGuiID path_id, int samples, ImVector<ImVec2>& out_points, bool use_arc_length) {
	out_points.resize(samples);

	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (!path || path->segments.Size == 0) {
		// Fill with zeros if path doesn't exist
		for (int i = 0; i < samples; i++) {
			out_points[i] = ImVec2(0, 0);
		}
		return;
	}

	// Build arc-length LUT if needed and using arc-length sampling
	if (use_arc_length && !path->has_arc_lut) {
		path->build_arc_lut(64);
	}

	for (int i = 0; i < samples; i++) {
		float t = (samples > 1) ? (float)i / (float)(samples - 1) : 0.0f;

		if (use_arc_length && path->has_arc_lut) {
			float distance = t * path->total_length;
			float path_t = path->distance_to_t(distance);
			out_points[i] = path->evaluate(path_t);
		} else {
			out_points[i] = path->evaluate(t);
		}
	}
}

// Interpolate between two point arrays
ImVec2 lerp_sampled(ImVector<ImVec2> const& a, ImVector<ImVec2> const& b, float t, float blend) {
	int n = a.Size;
	if (n == 0) return ImVec2(0, 0);

	// Handle endpoints
	if (t <= 0.0f) {
		return ImVec2(
			a[0].x + (b[0].x - a[0].x) * blend,
			a[0].y + (b[0].y - a[0].y) * blend
		);
	}
	if (t >= 1.0f) {
		return ImVec2(
			a[n-1].x + (b[n-1].x - a[n-1].x) * blend,
			a[n-1].y + (b[n-1].y - a[n-1].y) * blend
		);
	}

	// Find segment
	float scaled_t = t * (n - 1);
	int idx = (int)scaled_t;
	float frac = scaled_t - idx;

	if (idx >= n - 1) {
		idx = n - 2;
		frac = 1.0f;
	}

	// Interpolate within segment for both paths
	ImVec2 pa = ImVec2(
		a[idx].x + (a[idx+1].x - a[idx].x) * frac,
		a[idx].y + (a[idx+1].y - a[idx].y) * frac
	);
	ImVec2 pb = ImVec2(
		b[idx].x + (b[idx+1].x - b[idx].x) * frac,
		b[idx].y + (b[idx+1].y - b[idx].y) * frac
	);

	// Blend between paths
	return ImVec2(
		pa.x + (pb.x - pa.x) * blend,
		pa.y + (pb.y - pa.y) * blend
	);
}

// Get tangent from sampled points
ImVec2 tangent_sampled(ImVector<ImVec2> const& a, ImVector<ImVec2> const& b, float t, float blend) {
	int n = a.Size;
	if (n < 2) return ImVec2(1, 0);

	// Small delta for numerical derivative
	float dt = 0.001f;
	float t0 = ImMax(0.0f, t - dt);
	float t1 = ImMin(1.0f, t + dt);

	ImVec2 p0 = lerp_sampled(a, b, t0, blend);
	ImVec2 p1 = lerp_sampled(a, b, t1, blend);

	ImVec2 d = ImVec2(p1.x - p0.x, p1.y - p0.y);
	float len = ImSqrt(d.x * d.x + d.y * d.y);
	if (len > 1e-6f) {
		d.x /= len;
		d.y /= len;
	} else {
		d = ImVec2(1, 0);
	}
	return d;
}

} // namespace iam_morph_detail

ImVec2 iam_path_morph(ImGuiID path_a, ImGuiID path_b, float t, float blend, iam_morph_opts const& opts) {
	using namespace iam_morph_detail;

	// Clamp inputs
	t = ImClamp(t, 0.0f, 1.0f);
	blend = ImClamp(blend, 0.0f, 1.0f);

	// Fast path: no blending needed
	if (blend <= 0.0f) {
		return iam_path_evaluate(path_a, t);
	}
	if (blend >= 1.0f) {
		return iam_path_evaluate(path_b, t);
	}

	// Sample both paths
	static ImVector<ImVec2> samples_a;
	static ImVector<ImVec2> samples_b;

	sample_path(path_a, opts.samples, samples_a, opts.use_arc_length);
	sample_path(path_b, opts.samples, samples_b, opts.use_arc_length);

	// Interpolate
	return lerp_sampled(samples_a, samples_b, t, blend);
}

ImVec2 iam_path_morph_tangent(ImGuiID path_a, ImGuiID path_b, float t, float blend, iam_morph_opts const& opts) {
	using namespace iam_morph_detail;

	// Clamp inputs
	t = ImClamp(t, 0.0f, 1.0f);
	blend = ImClamp(blend, 0.0f, 1.0f);

	// Fast path: no blending needed
	if (blend <= 0.0f) {
		return iam_path_tangent(path_a, t);
	}
	if (blend >= 1.0f) {
		return iam_path_tangent(path_b, t);
	}

	// Sample both paths
	static ImVector<ImVec2> samples_a;
	static ImVector<ImVec2> samples_b;

	sample_path(path_a, opts.samples, samples_a, opts.use_arc_length);
	sample_path(path_b, opts.samples, samples_b, opts.use_arc_length);

	return tangent_sampled(samples_a, samples_b, t, blend);
}

float iam_path_morph_angle(ImGuiID path_a, ImGuiID path_b, float t, float blend, iam_morph_opts const& opts) {
	ImVec2 tangent = iam_path_morph_tangent(path_a, path_b, t, blend, opts);
	return atan2f(tangent.y, tangent.x);
}

ImVec2 iam_tween_path_morph(ImGuiID id, ImGuiID channel_id, ImGuiID path_a, ImGuiID path_b,
                            float target_blend, float dur, iam_ease_desc const& path_ease,
                            iam_ease_desc const& morph_ease, int policy, float dt,
                            iam_morph_opts const& opts) {
	using namespace iam_detail;
	using namespace iam_morph_detail;

	// Apply global time scale
	dt *= g_time_scale;

	// Get or create morph state
	morph_state* ms = get_morph_state(id, channel_id);

	// Use separate float tweens for path progress and blend
	ImGuiID path_ch = ImHashStr("_morph_path", 0, channel_id);
	ImGuiID blend_ch = ImHashStr("_morph_blend", 0, channel_id);

	// Animate path progress (0 to 1)
	ImGuiID path_key = make_key(id, path_ch);
	float_chan* path_c = g_float.get(path_key);

	// Check if path tween needs update
	float path_target = 1.0f;
	if (path_c->target != path_target || path_c->progress() >= 1.0f) {
		if (policy == iam_policy_cut) {
			path_c->current = 0.0f;
			path_c->set(path_target, dur, path_ease, policy);
		} else {
			path_c->set(path_target, dur, path_ease, policy);
		}
	}
	path_c->tick(dt);

	// Animate morph blend
	ImGuiID blend_key = make_key(id, blend_ch);
	float_chan* blend_c = g_float.get(blend_key);

	// Check if blend tween needs update
	if (fabsf(blend_c->target - target_blend) > 1e-6f || blend_c->progress() >= 1.0f) {
		if (policy == iam_policy_cut) {
			blend_c->current = blend_c->start = blend_c->target = target_blend;
			blend_c->sleeping = 1;
		} else {
			blend_c->set(target_blend, dur, morph_ease, policy);
		}
	}
	blend_c->tick(dt);

	// Store state for queries
	ms->path_t = path_c->current;
	ms->blend = blend_c->current;

	// Evaluate morphed path
	return iam_path_morph(path_a, path_b, ms->path_t, ms->blend, opts);
}

float iam_get_morph_blend(ImGuiID id, ImGuiID channel_id) {
	iam_morph_detail::morph_state* ms = iam_morph_detail::g_morph_states.GetByKey(
		iam_detail::make_key(id, channel_id)
	);
	return ms ? ms->blend : 0.0f;
}

// ----------------------------------------------------
// Quad transform helpers
// ----------------------------------------------------

void iam_transform_quad(ImVec2* quad, ImVec2 center, float angle_rad, ImVec2 translation) {
	float cos_a = ImCos(angle_rad);
	float sin_a = ImSin(angle_rad);

	for (int i = 0; i < 4; i++) {
		// Translate to origin (relative to center)
		float x = quad[i].x - center.x;
		float y = quad[i].y - center.y;

		// Rotate
		float rx = x * cos_a - y * sin_a;
		float ry = x * sin_a + y * cos_a;

		// Translate to final position
		quad[i].x = rx + center.x + translation.x;
		quad[i].y = ry + center.y + translation.y;
	}
}

void iam_make_glyph_quad(ImVec2* quad, ImVec2 pos, float angle_rad, float glyph_width, float glyph_height, float baseline_offset) {
	float cos_a = ImCos(angle_rad);
	float sin_a = ImSin(angle_rad);

	// Perpendicular vector (90 degrees rotated from tangent, screen coords Y-down)
	float perp_x = sin_a;
	float perp_y = -cos_a;

	// Offset along perpendicular for baseline
	float base_offset_x = perp_x * baseline_offset;
	float base_offset_y = perp_y * baseline_offset;

	// Create quad corners (centered horizontally, baseline-aligned vertically)
	// Forward vector (along path tangent)
	float fwd_x = cos_a;
	float fwd_y = sin_a;

	float half_w = glyph_width * 0.5f;

	// Bottom-left
	quad[0].x = pos.x - fwd_x * half_w + base_offset_x;
	quad[0].y = pos.y - fwd_y * half_w + base_offset_y;

	// Bottom-right
	quad[1].x = pos.x + fwd_x * half_w + base_offset_x;
	quad[1].y = pos.y + fwd_y * half_w + base_offset_y;

	// Top-right
	quad[2].x = pos.x + fwd_x * half_w + base_offset_x - perp_x * glyph_height;
	quad[2].y = pos.y + fwd_y * half_w + base_offset_y - perp_y * glyph_height;

	// Top-left
	quad[3].x = pos.x - fwd_x * half_w + base_offset_x - perp_x * glyph_height;
	quad[3].y = pos.y - fwd_y * half_w + base_offset_y - perp_y * glyph_height;
}

// ----------------------------------------------------
// Text along motion paths
// ----------------------------------------------------

float iam_text_path_width(char const* text, iam_text_path_opts const& opts) {
	ImFont* font = opts.font ? opts.font : ImGui::GetFont();
	float font_size = ImGui::GetFontSize() * opts.font_scale;
	FontType* baked = GetBakedFont(font, font_size);
	if (!baked) return 0.0f;

	float total_width = 0;
	char const* p = text;
	while (*p) {
		unsigned int c = 0;
		int char_len = ImTextCharFromUtf8(&c, p, nullptr);
		if (char_len == 0) break;

		ImFontGlyph const* glyph = baked->FindGlyph((ImWchar)c);
		if (glyph) {
			total_width += glyph->AdvanceX;
			total_width += opts.letter_spacing;
		}
		p += char_len;
	}
	// Remove last letter_spacing
	if (total_width > 0 && opts.letter_spacing != 0) {
		total_width -= opts.letter_spacing;
	}
	return total_width;
}

void iam_text_path(ImGuiID path_id, char const* text, iam_text_path_opts const& opts) {
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (!path || path->segments.Size == 0 || !text || !*text) return;

	// Ensure path has arc-length LUT for constant-speed text placement
	if (!path->has_arc_lut) {
		path->build_arc_lut(64);
	}

	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	ImFont* font = opts.font ? opts.font : ImGui::GetFont();
	float font_size = ImGui::GetFontSize() * opts.font_scale;
	FontType* baked = GetBakedFont(font, font_size);
	if (!baked) return;

	// Calculate text width and starting offset
	float text_width = iam_text_path_width(text, opts);
	float path_len = path->total_length;

	float start_offset = opts.offset;
	switch (opts.align) {
		case iam_text_align_center:
			start_offset = (path_len - text_width) * 0.5f + opts.offset;
			break;
		case iam_text_align_end:
			start_offset = path_len - text_width + opts.offset;
			break;
		default: // iam_text_align_start
			break;
	}

	// Render each character
	float current_dist = start_offset;
	char const* p = text;

	while (*p) {
		unsigned int c = 0;
		int char_len = ImTextCharFromUtf8(&c, p, nullptr);
		if (char_len == 0) break;

		ImFontGlyph const* glyph = baked->FindGlyph((ImWchar)c);
		if (!glyph) {
			p += char_len;
			continue;
		}

		float glyph_advance = glyph->AdvanceX;
		float glyph_width = glyph->X1 - glyph->X0;
		float glyph_height = glyph->Y1 - glyph->Y0;
		float glyph_offset_x = glyph->X0;
		float glyph_offset_y = glyph->Y0;

		// Position at center of glyph advance
		float char_center_dist = current_dist + glyph_advance * 0.5f;

		// Skip if outside path bounds
		if (char_center_dist >= 0 && char_center_dist <= path_len) {
			ImVec2 pos = path->evaluate_at_distance(char_center_dist);
			float angle = path->angle_at_distance(char_center_dist);

			if (opts.flip_y) {
				angle += IM_PI;
			}

			float cos_a = ImCos(angle);
			float sin_a = ImSin(angle);

			// Perpendicular (up) direction in screen coords (Y-down)
			float perp_x = sin_a;
			float perp_y = -cos_a;

			// Calculate quad corners for the glyph
			// Shift glyph up so it sits ON TOP of the path (path = baseline)
			float local_x0 = -glyph_advance * 0.5f + glyph_offset_x;
			float local_x1 = local_x0 + glyph_width;
			float local_y0 = glyph_offset_y - glyph_height;  // Top of glyph (shifted up)
			float local_y1 = glyph_offset_y;                  // Bottom of glyph (at path)

			// Transform to path coordinates + origin offset
			float ox = opts.origin.x;
			float oy = opts.origin.y;
			ImVec2 corners[4];
			// Bottom-left (UV: u0, v0)
			corners[0].x = ox + pos.x + cos_a * local_x0 - perp_x * local_y0;
			corners[0].y = oy + pos.y + sin_a * local_x0 - perp_y * local_y0;
			// Bottom-right (UV: u1, v0)
			corners[1].x = ox + pos.x + cos_a * local_x1 - perp_x * local_y0;
			corners[1].y = oy + pos.y + sin_a * local_x1 - perp_y * local_y0;
			// Top-right (UV: u1, v1)
			corners[2].x = ox + pos.x + cos_a * local_x1 - perp_x * local_y1;
			corners[2].y = oy + pos.y + sin_a * local_x1 - perp_y * local_y1;
			// Top-left (UV: u0, v1)
			corners[3].x = ox + pos.x + cos_a * local_x0 - perp_x * local_y1;
			corners[3].y = oy + pos.y + sin_a * local_x0 - perp_y * local_y1;

			// Draw textured quad
			draw_list->PrimReserve(6, 4);
			draw_list->PrimQuadUV(
				corners[0], corners[1], corners[2], corners[3],
				ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V0),
				ImVec2(glyph->U1, glyph->V1), ImVec2(glyph->U0, glyph->V1),
				opts.color
			);
		}

		current_dist += glyph_advance + opts.letter_spacing;
		p += char_len;
	}
}

void iam_text_path_animated(ImGuiID path_id, char const* text, float progress, iam_text_path_opts const& opts) {
	iam_path_detail::path_data* path = iam_path_detail::get_path(path_id);
	if (!path || path->segments.Size == 0 || !text || !*text) return;

	// Ensure path has arc-length LUT
	if (!path->has_arc_lut) {
		path->build_arc_lut(64);
	}

	// Clamp progress
	progress = ImClamp(progress, 0.0f, 1.0f);
	if (progress <= 0.0f) return;

	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	ImFont* font = opts.font ? opts.font : ImGui::GetFont();
	float font_size = ImGui::GetFontSize() * opts.font_scale;
	FontType* baked = GetBakedFont(font, font_size);
	if (!baked) return;

	// Calculate text width and starting offset
	float text_width = iam_text_path_width(text, opts);
	float path_len = path->total_length;

	float start_offset = opts.offset;
	switch (opts.align) {
		case iam_text_align_center:
			start_offset = (path_len - text_width) * 0.5f + opts.offset;
			break;
		case iam_text_align_end:
			start_offset = path_len - text_width + opts.offset;
			break;
		default:
			break;
	}

	// Count characters for progress calculation
	int char_count = 0;
	char const* p = text;
	while (*p) {
		unsigned int c = 0;
		int char_len = ImTextCharFromUtf8(&c, p, nullptr);
		if (char_len == 0) break;
		char_count++;
		p += char_len;
	}

	// How many characters to show
	int visible_chars = (int)(progress * char_count + 0.999f);  // Round up
	float partial_char_alpha = ImFmod(progress * char_count, 1.0f);
	if (progress >= 1.0f) partial_char_alpha = 1.0f;

	// Render characters
	float current_dist = start_offset;
	p = text;
	int char_idx = 0;

	while (*p && char_idx < visible_chars) {
		unsigned int c = 0;
		int char_len = ImTextCharFromUtf8(&c, p, nullptr);
		if (char_len == 0) break;

		ImFontGlyph const* glyph = baked->FindGlyph((ImWchar)c);
		if (!glyph) {
			p += char_len;
			char_idx++;
			continue;
		}

		float glyph_advance = glyph->AdvanceX;
		float glyph_width = glyph->X1 - glyph->X0;
		float glyph_height = glyph->Y1 - glyph->Y0;
		float glyph_offset_x = glyph->X0;
		float glyph_offset_y = glyph->Y0;

		float char_center_dist = current_dist + glyph_advance * 0.5f;

		if (char_center_dist >= 0 && char_center_dist <= path_len) {
			ImVec2 pos = path->evaluate_at_distance(char_center_dist);
			float angle = path->angle_at_distance(char_center_dist);

			if (opts.flip_y) {
				angle += IM_PI;
			}

			float cos_a = ImCos(angle);
			float sin_a = ImSin(angle);
			float perp_x = sin_a;
			float perp_y = -cos_a;

			// Shift glyph up so it sits ON TOP of the path (path = baseline)
			float local_x0 = -glyph_advance * 0.5f + glyph_offset_x;
			float local_x1 = local_x0 + glyph_width;
			float local_y0 = glyph_offset_y - glyph_height;  // Top of glyph (shifted up)
			float local_y1 = glyph_offset_y;                  // Bottom of glyph (at path)

			// Transform to path coordinates + origin offset
			float ox = opts.origin.x;
			float oy = opts.origin.y;
			ImVec2 corners[4];
			corners[0].x = ox + pos.x + cos_a * local_x0 - perp_x * local_y0;
			corners[0].y = oy + pos.y + sin_a * local_x0 - perp_y * local_y0;
			corners[1].x = ox + pos.x + cos_a * local_x1 - perp_x * local_y0;
			corners[1].y = oy + pos.y + sin_a * local_x1 - perp_y * local_y0;
			corners[2].x = ox + pos.x + cos_a * local_x1 - perp_x * local_y1;
			corners[2].y = oy + pos.y + sin_a * local_x1 - perp_y * local_y1;
			corners[3].x = ox + pos.x + cos_a * local_x0 - perp_x * local_y1;
			corners[3].y = oy + pos.y + sin_a * local_x0 - perp_y * local_y1;

			// Calculate alpha for this character (fade in last character)
			ImU32 color = opts.color;
			if (char_idx == visible_chars - 1 && partial_char_alpha < 1.0f) {
				ImU32 alpha = (ImU32)((color >> IM_COL32_A_SHIFT) & 0xFF);
				alpha = (ImU32)(alpha * partial_char_alpha);
				color = (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
			}

			draw_list->PrimReserve(6, 4);
			draw_list->PrimQuadUV(
				corners[0], corners[1], corners[2], corners[3],
				ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V0),
				ImVec2(glyph->U1, glyph->V1), ImVec2(glyph->U0, glyph->V1),
				color
			);
		}

		current_dist += glyph_advance + opts.letter_spacing;
		p += char_len;
		char_idx++;
	}
}

// ============================================================
// TEXT STAGGER - Per-character animation effects
// ============================================================

float iam_text_stagger_width(char const* text, iam_text_stagger_opts const& opts) {
	ImFont* font = opts.font ? opts.font : ImGui::GetFont();
	float font_size = ImGui::GetFontSize() * opts.font_scale;
	FontType* baked = GetBakedFont(font, font_size);

	float width = 0.0f;
	char const* p = text;
	int char_count = 0;

	while (*p) {
		unsigned int c;
		int char_len = ImTextCharFromUtf8(&c, p, nullptr);
		if (char_len == 0) break;

		ImFontGlyph const* glyph = baked->FindGlyph((ImWchar)c);
		if (glyph) {
			width += glyph->AdvanceX;
			if (*(p + char_len)) {
				width += opts.letter_spacing;
			}
		}
		p += char_len;
		char_count++;
	}

	return width;
}

float iam_text_stagger_duration(char const* text, iam_text_stagger_opts const& opts) {
	int char_count = 0;
	char const* p = text;
	while (*p) {
		unsigned int c;
		int char_len = ImTextCharFromUtf8(&c, p, nullptr);
		if (char_len == 0) break;
		char_count++;
		p += char_len;
	}

	if (char_count == 0) return 0.0f;
	return (char_count - 1) * opts.char_delay + opts.char_duration;
}

void iam_text_stagger(ImGuiID id, char const* text, float progress, iam_text_stagger_opts const& opts) {
	if (!text || !*text) return;

	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	ImFont* font = opts.font ? opts.font : ImGui::GetFont();
	float font_size = ImGui::GetFontSize() * opts.font_scale;
	FontType* baked = GetBakedFont(font, font_size);

	// Count characters
	int char_count = 0;
	char const* p = text;
	while (*p) {
		unsigned int c;
		int char_len = ImTextCharFromUtf8(&c, p, nullptr);
		if (char_len == 0) break;
		char_count++;
		p += char_len;
	}

	if (char_count == 0) return;

	float total_duration = iam_text_stagger_duration(text, opts);
	float current_time = progress * total_duration;

	float cursor_x = opts.pos.x;
	float cursor_y = opts.pos.y;
	p = text;
	int char_idx = 0;

	while (*p) {
		unsigned int c;
		int char_len = ImTextCharFromUtf8(&c, p, nullptr);
		if (char_len == 0) break;

		ImFontGlyph const* glyph = baked->FindGlyph((ImWchar)c);
		if (!glyph) {
			p += char_len;
			char_idx++;
			continue;
		}

		// Calculate character animation progress
		float char_start_time = char_idx * opts.char_delay;
		float char_progress = 0.0f;

		if (current_time >= char_start_time + opts.char_duration) {
			char_progress = 1.0f;
		} else if (current_time > char_start_time) {
			float local_t = (current_time - char_start_time) / opts.char_duration;
			char_progress = iam_detail::eval(opts.ease, local_t);
		}

		// Skip invisible characters
		if (char_progress <= 0.0f && opts.effect != iam_text_fx_wave) {
			cursor_x += glyph->AdvanceX + opts.letter_spacing;
			p += char_len;
			char_idx++;
			continue;
		}

		// Calculate effect transforms
		float alpha = 1.0f;
		float scale = 1.0f;
		float offset_x = 0.0f;
		float offset_y = 0.0f;
		float rotation = 0.0f;

		switch (opts.effect) {
			case iam_text_fx_none:
			case iam_text_fx_typewriter:
				// Instant appear
				alpha = char_progress > 0.0f ? 1.0f : 0.0f;
				break;

			case iam_text_fx_fade:
				alpha = char_progress;
				break;

			case iam_text_fx_scale:
				alpha = char_progress;
				scale = char_progress;
				break;

			case iam_text_fx_slide_up:
				alpha = char_progress;
				offset_y = (1.0f - char_progress) * opts.effect_intensity;
				break;

			case iam_text_fx_slide_down:
				alpha = char_progress;
				offset_y = -(1.0f - char_progress) * opts.effect_intensity;
				break;

			case iam_text_fx_slide_left:
				alpha = char_progress;
				offset_x = (1.0f - char_progress) * opts.effect_intensity;
				break;

			case iam_text_fx_slide_right:
				alpha = char_progress;
				offset_x = -(1.0f - char_progress) * opts.effect_intensity;
				break;

			case iam_text_fx_rotate:
				alpha = char_progress;
				rotation = (1.0f - char_progress) * opts.effect_intensity * (3.14159f / 180.0f);
				break;

			case iam_text_fx_bounce: {
				alpha = char_progress;
				// Overshoot effect using back easing
				float bounce_t = char_progress;
				if (bounce_t < 1.0f) {
					bounce_t = bounce_t * bounce_t * ((iam_detail::BACK_OVERSHOOT + 1) * bounce_t - iam_detail::BACK_OVERSHOOT);
				}
				scale = bounce_t;
				break;
			}

			case iam_text_fx_wave: {
				// Continuous wave effect based on time and character index
				float wave_offset = char_idx * 0.3f;
				float wave_time = progress * 6.28318f + wave_offset;
				offset_y = ImSin(wave_time) * opts.effect_intensity * 0.5f;
				alpha = 1.0f;
				break;
			}
		}

		if (alpha <= 0.0f) {
			cursor_x += glyph->AdvanceX + opts.letter_spacing;
			p += char_len;
			char_idx++;
			continue;
		}

		// Calculate glyph dimensions
		float glyph_width = (glyph->X1 - glyph->X0);
		float glyph_height = (glyph->Y1 - glyph->Y0);
		float glyph_x = cursor_x + glyph->X0 + offset_x;
		float glyph_y = cursor_y + glyph->Y0 + offset_y;

		// Apply scale around center
		if (scale != 1.0f) {
			float center_x = glyph_x + glyph_width * 0.5f;
			float center_y = glyph_y + glyph_height * 0.5f;
			glyph_x = center_x + (glyph_x - center_x) * scale;
			glyph_y = center_y + (glyph_y - center_y) * scale;
			glyph_width *= scale;
			glyph_height *= scale;
		}

		// Apply color with alpha
		ImU32 color = opts.color;
		ImU32 base_alpha = (color >> IM_COL32_A_SHIFT) & 0xFF;
		ImU32 final_alpha = (ImU32)(base_alpha * alpha);
		color = (color & ~IM_COL32_A_MASK) | (final_alpha << IM_COL32_A_SHIFT);

		// Render the glyph
		if (rotation == 0.0f) {
			// Simple axis-aligned quad
			ImVec2 p0(glyph_x, glyph_y);
			ImVec2 p1(glyph_x + glyph_width, glyph_y + glyph_height);
			draw_list->PrimReserve(6, 4);
			draw_list->PrimRectUV(p0, p1, ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V1), color);
		} else {
			// Rotated quad
			float center_x = glyph_x + glyph_width * 0.5f;
			float center_y = glyph_y + glyph_height * 0.5f;
			float cos_r = ImCos(rotation);
			float sin_r = ImSin(rotation);

			ImVec2 corners[4];
			float hw = glyph_width * 0.5f;
			float hh = glyph_height * 0.5f;
			float local_corners[4][2] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};

			for (int i = 0; i < 4; i++) {
				float lx = local_corners[i][0];
				float ly = local_corners[i][1];
				corners[i].x = center_x + lx * cos_r - ly * sin_r;
				corners[i].y = center_y + lx * sin_r + ly * cos_r;
			}

			draw_list->PrimReserve(6, 4);
			draw_list->PrimQuadUV(
				corners[0], corners[1], corners[2], corners[3],
				ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V0),
				ImVec2(glyph->U1, glyph->V1), ImVec2(glyph->U0, glyph->V1),
				color
			);
		}

		cursor_x += glyph->AdvanceX + opts.letter_spacing;
		p += char_len;
		char_idx++;
	}
}

// ============================================================
// NOISE CHANNELS - Perlin/Simplex noise for organic movement
// ============================================================

namespace iam_noise_detail {

// Permutation table for noise
static int const perm[512] = {
	151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
	190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,
	125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
	105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196,
	135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,
	82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,
	153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,
	251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,
	106,157,184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,
	78,66,215,61,156,180,
	// Repeat
	151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
	190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,
	125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
	105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196,
	135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,
	82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,
	153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,
	251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,
	106,157,184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,
	78,66,215,61,156,180
};

inline float fade(float t) {
	return t * t * t * (t * (t * 6 - 15) + 10);
}

inline float lerp(float a, float b, float t) {
	return a + t * (b - a);
}

inline float grad2d(int hash, float x, float y) {
	int h = hash & 7;
	float u = h < 4 ? x : y;
	float v = h < 4 ? y : x;
	return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

// Simplex-specific gradient (returns values in [-1, 1] range)
inline float grad2d_simplex(int hash, float x, float y) {
	static float const grad2[][2] = {
		{1, 0}, {-1, 0}, {0, 1}, {0, -1},
		{0.7071067811865476f, 0.7071067811865476f},
		{-0.7071067811865476f, 0.7071067811865476f},
		{0.7071067811865476f, -0.7071067811865476f},
		{-0.7071067811865476f, -0.7071067811865476f}
	};
	int h = hash & 7;
	return grad2[h][0] * x + grad2[h][1] * y;
}

inline float grad3d(int hash, float x, float y, float z) {
	int h = hash & 15;
	float u = h < 8 ? x : y;
	float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
	return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float perlin_2d(float x, float y, int seed) {
	x += seed * 12.9898f;
	y += seed * 78.233f;

	int X = (int)floorf(x) & 255;
	int Y = (int)floorf(y) & 255;

	x -= floorf(x);
	y -= floorf(y);

	float u = fade(x);
	float v = fade(y);

	int A = perm[X] + Y;
	int B = perm[X + 1] + Y;

	return lerp(
		lerp(grad2d(perm[A], x, y), grad2d(perm[B], x - 1, y), u),
		lerp(grad2d(perm[A + 1], x, y - 1), grad2d(perm[B + 1], x - 1, y - 1), u),
		v
	) * 0.5f;
}

float perlin_3d(float x, float y, float z, int seed) {
	x += seed * 12.9898f;
	y += seed * 78.233f;
	z += seed * 37.719f;

	int X = (int)floorf(x) & 255;
	int Y = (int)floorf(y) & 255;
	int Z = (int)floorf(z) & 255;

	x -= floorf(x);
	y -= floorf(y);
	z -= floorf(z);

	float u = fade(x);
	float v = fade(y);
	float w = fade(z);

	int A = perm[X] + Y;
	int AA = perm[A] + Z;
	int AB = perm[A + 1] + Z;
	int B = perm[X + 1] + Y;
	int BA = perm[B] + Z;
	int BB = perm[B + 1] + Z;

	return lerp(
		lerp(
			lerp(grad3d(perm[AA], x, y, z), grad3d(perm[BA], x - 1, y, z), u),
			lerp(grad3d(perm[AB], x, y - 1, z), grad3d(perm[BB], x - 1, y - 1, z), u),
			v
		),
		lerp(
			lerp(grad3d(perm[AA + 1], x, y, z - 1), grad3d(perm[BA + 1], x - 1, y, z - 1), u),
			lerp(grad3d(perm[AB + 1], x, y - 1, z - 1), grad3d(perm[BB + 1], x - 1, y - 1, z - 1), u),
			v
		),
		w
	) * 0.5f;
}

// Simplex noise 2D
float simplex_2d(float x, float y, int seed) {
	float const F2 = 0.366025403f; // (sqrt(3) - 1) / 2
	float const G2 = 0.211324865f; // (3 - sqrt(3)) / 6

	x += seed * 12.9898f;
	y += seed * 78.233f;

	float s = (x + y) * F2;
	int i = (int)floorf(x + s);
	int j = (int)floorf(y + s);

	float t = (i + j) * G2;
	float X0 = i - t;
	float Y0 = j - t;
	float x0 = x - X0;
	float y0 = y - Y0;

	int i1, j1;
	if (x0 > y0) { i1 = 1; j1 = 0; }
	else { i1 = 0; j1 = 1; }

	float x1 = x0 - i1 + G2;
	float y1 = y0 - j1 + G2;
	float x2 = x0 - 1.0f + 2.0f * G2;
	float y2 = y0 - 1.0f + 2.0f * G2;

	int ii = i & 255;
	int jj = j & 255;

	float n0, n1, n2;

	float t0 = 0.5f - x0 * x0 - y0 * y0;
	if (t0 < 0) n0 = 0.0f;
	else {
		t0 *= t0;
		n0 = t0 * t0 * grad2d_simplex(perm[ii + perm[jj]], x0, y0);
	}

	float t1 = 0.5f - x1 * x1 - y1 * y1;
	if (t1 < 0) n1 = 0.0f;
	else {
		t1 *= t1;
		n1 = t1 * t1 * grad2d_simplex(perm[ii + i1 + perm[jj + j1]], x1, y1);
	}

	float t2 = 0.5f - x2 * x2 - y2 * y2;
	if (t2 < 0) n2 = 0.0f;
	else {
		t2 *= t2;
		n2 = t2 * t2 * grad2d_simplex(perm[ii + 1 + perm[jj + 1]], x2, y2);
	}

	// Scale to approximately [-1, 1] range
	return 45.23065f * (n0 + n1 + n2);
}

// Worley/Cellular noise 2D
float worley_2d(float x, float y, int seed) {
	x += seed * 12.9898f;
	y += seed * 78.233f;

	int xi = (int)floorf(x);
	int yi = (int)floorf(y);

	float min_dist = 1e10f;

	// Check 3x3 grid of cells
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			int cx = xi + dx;
			int cy = yi + dy;

			// Generate pseudo-random point in this cell
			int h = perm[(cx & 255) + perm[(cy & 255)]];
			float px = cx + (perm[h] / 255.0f);
			float py = cy + (perm[(h + 1) & 255] / 255.0f);

			// Distance to point
			float ddx = x - px;
			float ddy = y - py;
			float dist = sqrtf(ddx * ddx + ddy * ddy);

			if (dist < min_dist) {
				min_dist = dist;
			}
		}
	}

	// Normalize to approximately [-1, 1] range
	// Typical distances range from 0 to ~1.4
	return min_dist * 1.4f - 1.0f;
}

float value_noise_2d(float x, float y, int seed) {
	int xi = (int)floorf(x);
	int yi = (int)floorf(y);

	float xf = x - xi;
	float yf = y - yi;

	xi = xi & 255;
	yi = yi & 255;

	auto hash = [seed](int x, int y) {
		int h = perm[(x + seed) & 255];
		h = perm[(h + y) & 255];
		return (h / 255.0f) * 2.0f - 1.0f;
	};

	float u = fade(xf);
	float v = fade(yf);

	float n00 = hash(xi, yi);
	float n10 = hash(xi + 1, yi);
	float n01 = hash(xi, yi + 1);
	float n11 = hash(xi + 1, yi + 1);

	return lerp(
		lerp(n00, n10, u),
		lerp(n01, n11, u),
		v
	);
}

// Noise channel state
struct noise_state {
	float time;
	ImGuiID last_frame;
};

static ImPool<noise_state> g_noise_states;

noise_state* get_noise_state(ImGuiID id) {
	noise_state* s = g_noise_states.GetByKey(id);
	if (!s) {
		s = g_noise_states.GetOrAddByKey(id);
		s->time = 0.0f;
		s->last_frame = 0;
	}
	return s;
}

} // namespace iam_noise_detail

float iam_noise_2d(float x, float y, iam_noise_opts const& opts) {
	float total = 0.0f;
	float amplitude = 1.0f;
	float frequency = 1.0f;
	float max_value = 0.0f;

	for (int i = 0; i < opts.octaves; i++) {
		float nx = x * frequency;
		float ny = y * frequency;

		float value;
		switch (opts.type) {
			case iam_noise_simplex:
				value = iam_noise_detail::simplex_2d(nx, ny, opts.seed);
				break;
			case iam_noise_value:
				value = iam_noise_detail::value_noise_2d(nx, ny, opts.seed);
				break;
			case iam_noise_worley:
				value = iam_noise_detail::worley_2d(nx, ny, opts.seed);
				break;
			default: // perlin
				value = iam_noise_detail::perlin_2d(nx, ny, opts.seed);
				break;
		}

		total += value * amplitude;
		max_value += amplitude;

		amplitude *= opts.persistence;
		frequency *= opts.lacunarity;
	}

	return total / max_value;
}

float iam_noise_3d(float x, float y, float z, iam_noise_opts const& opts) {
	float total = 0.0f;
	float amplitude = 1.0f;
	float frequency = 1.0f;
	float max_value = 0.0f;

	for (int i = 0; i < opts.octaves; i++) {
		float nx = x * frequency;
		float ny = y * frequency;
		float nz = z * frequency;

		float value = iam_noise_detail::perlin_3d(nx, ny, nz, opts.seed);
		total += value * amplitude;
		max_value += amplitude;

		amplitude *= opts.persistence;
		frequency *= opts.lacunarity;
	}

	return total / max_value;
}

float iam_noise_channel_float(ImGuiID id, float frequency, float amplitude, iam_noise_opts const& opts, float dt) {
	using namespace iam_noise_detail;
	noise_state* s = get_noise_state(id);

	s->time += dt;
	float noise_val = iam_noise_2d(s->time * frequency, 0.0f, opts);
	return noise_val * amplitude;
}

ImVec2 iam_noise_channel_vec2(ImGuiID id, ImVec2 frequency, ImVec2 amplitude, iam_noise_opts const& opts, float dt) {
	using namespace iam_noise_detail;
	noise_state* s = get_noise_state(id);

	s->time += dt;
	float nx = iam_noise_2d(s->time * frequency.x, 0.0f, opts);
	float ny = iam_noise_2d(s->time * frequency.y, 100.0f, opts); // Offset Y to get different values
	return ImVec2(nx * amplitude.x, ny * amplitude.y);
}

ImVec4 iam_noise_channel_vec4(ImGuiID id, ImVec4 frequency, ImVec4 amplitude, iam_noise_opts const& opts, float dt) {
	using namespace iam_noise_detail;
	noise_state* s = get_noise_state(id);

	s->time += dt;
	float nx = iam_noise_2d(s->time * frequency.x, 0.0f, opts);
	float ny = iam_noise_2d(s->time * frequency.y, 100.0f, opts);
	float nz = iam_noise_2d(s->time * frequency.z, 200.0f, opts);
	float nw = iam_noise_2d(s->time * frequency.w, 300.0f, opts);
	return ImVec4(nx * amplitude.x, ny * amplitude.y, nz * amplitude.z, nw * amplitude.w);
}

float iam_smooth_noise_float(ImGuiID id, float amplitude, float speed, float dt) {
	iam_noise_opts opts;
	opts.octaves = 2;
	opts.persistence = 0.5f;
	return iam_noise_channel_float(id, speed, amplitude, opts, dt);
}

ImVec2 iam_smooth_noise_vec2(ImGuiID id, ImVec2 amplitude, float speed, float dt) {
	iam_noise_opts opts;
	opts.octaves = 2;
	opts.persistence = 0.5f;
	return iam_noise_channel_vec2(id, ImVec2(speed, speed), amplitude, opts, dt);
}

// ============================================================
ImVec4 iam_smooth_noise_vec4(ImGuiID id, ImVec4 amplitude, float speed, float dt) {	iam_noise_opts opts;	opts.octaves = 2;	opts.persistence = 0.5f;	return iam_noise_channel_vec4(id, ImVec4(speed, speed, speed, speed), amplitude, opts, dt);}ImVec4 iam_noise_channel_color(ImGuiID id, ImVec4 base_color, ImVec4 amplitude, float frequency, iam_noise_opts const& opts, int color_space, float dt) {	ImVec4 noise = iam_noise_channel_vec4(id, ImVec4(frequency, frequency, frequency, frequency), amplitude, opts, dt);	ImVec4 working;	switch (color_space) {		case iam_col_srgb_linear:			working = iam_detail::color::srgb_to_linear(base_color);			working.x += noise.x; working.y += noise.y; working.z += noise.z; working.w += noise.w;			return iam_detail::color::linear_to_srgb(working);		case iam_col_hsv:			working = iam_detail::color::srgb_to_hsv(base_color);			working.x = ImFmod(working.x + noise.x + 1.0f, 1.0f);			working.y = ImClamp(working.y + noise.y, 0.0f, 1.0f);			working.z = ImClamp(working.z + noise.z, 0.0f, 1.0f);			working.w = ImClamp(working.w + noise.w, 0.0f, 1.0f);			return iam_detail::color::hsv_to_srgb(working);		case iam_col_oklab:			working = iam_detail::color::srgb_to_oklab(base_color);			working.x += noise.x; working.y += noise.y; working.z += noise.z; working.w += noise.w;			return iam_detail::color::oklab_to_srgb(working);		case iam_col_oklch:			working = iam_detail::color::srgb_to_oklch(base_color);			working.x += noise.x; working.y += noise.y;			working.z = ImFmod(working.z + noise.z + 360.0f, 360.0f);			working.w += noise.w;			return iam_detail::color::oklch_to_srgb(working);		default:			return ImVec4(ImClamp(base_color.x + noise.x, 0.0f, 1.0f), ImClamp(base_color.y + noise.y, 0.0f, 1.0f), ImClamp(base_color.z + noise.z, 0.0f, 1.0f), ImClamp(base_color.w + noise.w, 0.0f, 1.0f));	}}ImVec4 iam_smooth_noise_color(ImGuiID id, ImVec4 base_color, ImVec4 amplitude, float speed, int color_space, float dt) {	iam_noise_opts opts;	opts.octaves = 2;	opts.persistence = 0.5f;	return iam_noise_channel_color(id, base_color, amplitude, speed, opts, color_space, dt);}
// STYLE INTERPOLATION - Animate between ImGuiStyle themes
// ============================================================

namespace iam_style_detail {

struct registered_style {
	ImGuiStyle style;
	bool valid;
};

static ImPool<registered_style> g_styles;

// Style tween state
struct style_tween_state {
	ImGuiID target_style;
	ImGuiID source_style;
	float t;
	float duration;
	iam_ease_desc ease;
	int color_space;
	bool active;
};

static ImPool<style_tween_state> g_style_tweens;

inline float lerp_float(float a, float b, float t) {
	return a + (b - a) * t;
}

inline ImVec2 lerp_vec2(ImVec2 a, ImVec2 b, float t) {
	return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

void blend_styles(ImGuiStyle const& a, ImGuiStyle const& b, float t, ImGuiStyle* out, int color_space) {
	// Blend all float properties
	out->Alpha = lerp_float(a.Alpha, b.Alpha, t);
	out->DisabledAlpha = lerp_float(a.DisabledAlpha, b.DisabledAlpha, t);
	out->WindowRounding = lerp_float(a.WindowRounding, b.WindowRounding, t);
	out->WindowBorderSize = lerp_float(a.WindowBorderSize, b.WindowBorderSize, t);
	out->ChildRounding = lerp_float(a.ChildRounding, b.ChildRounding, t);
	out->ChildBorderSize = lerp_float(a.ChildBorderSize, b.ChildBorderSize, t);
	out->PopupRounding = lerp_float(a.PopupRounding, b.PopupRounding, t);
	out->PopupBorderSize = lerp_float(a.PopupBorderSize, b.PopupBorderSize, t);
	out->FrameRounding = lerp_float(a.FrameRounding, b.FrameRounding, t);
	out->FrameBorderSize = lerp_float(a.FrameBorderSize, b.FrameBorderSize, t);
	out->IndentSpacing = lerp_float(a.IndentSpacing, b.IndentSpacing, t);
	out->ColumnsMinSpacing = lerp_float(a.ColumnsMinSpacing, b.ColumnsMinSpacing, t);
	out->ScrollbarSize = lerp_float(a.ScrollbarSize, b.ScrollbarSize, t);
	out->ScrollbarRounding = lerp_float(a.ScrollbarRounding, b.ScrollbarRounding, t);
	out->GrabMinSize = lerp_float(a.GrabMinSize, b.GrabMinSize, t);
	out->GrabRounding = lerp_float(a.GrabRounding, b.GrabRounding, t);
	out->TabRounding = lerp_float(a.TabRounding, b.TabRounding, t);
	out->TabBorderSize = lerp_float(a.TabBorderSize, b.TabBorderSize, t);
	out->TabBarBorderSize = lerp_float(a.TabBarBorderSize, b.TabBarBorderSize, t);
	out->SeparatorTextBorderSize = lerp_float(a.SeparatorTextBorderSize, b.SeparatorTextBorderSize, t);

	// Blend vec2 properties
	out->WindowPadding = lerp_vec2(a.WindowPadding, b.WindowPadding, t);
	out->WindowMinSize = lerp_vec2(a.WindowMinSize, b.WindowMinSize, t);
	out->WindowTitleAlign = lerp_vec2(a.WindowTitleAlign, b.WindowTitleAlign, t);
	out->FramePadding = lerp_vec2(a.FramePadding, b.FramePadding, t);
	out->ItemSpacing = lerp_vec2(a.ItemSpacing, b.ItemSpacing, t);
	out->ItemInnerSpacing = lerp_vec2(a.ItemInnerSpacing, b.ItemInnerSpacing, t);
	out->CellPadding = lerp_vec2(a.CellPadding, b.CellPadding, t);
	out->ButtonTextAlign = lerp_vec2(a.ButtonTextAlign, b.ButtonTextAlign, t);
	out->SelectableTextAlign = lerp_vec2(a.SelectableTextAlign, b.SelectableTextAlign, t);
	out->SeparatorTextAlign = lerp_vec2(a.SeparatorTextAlign, b.SeparatorTextAlign, t);
	out->SeparatorTextPadding = lerp_vec2(a.SeparatorTextPadding, b.SeparatorTextPadding, t);

	// Blend all colors using the existing color space infrastructure
	for (int i = 0; i < ImGuiCol_COUNT; i++) {
		out->Colors[i] = iam_detail::color::lerp_color(a.Colors[i], b.Colors[i], t, color_space);
	}
}

} // namespace iam_style_detail

void iam_style_register(ImGuiID style_id, ImGuiStyle const& style) {
	using namespace iam_style_detail;
	registered_style* s = g_styles.GetOrAddByKey(style_id);
	s->style = style;
	s->valid = true;
}

void iam_style_register_current(ImGuiID style_id) {
	iam_style_register(style_id, ImGui::GetStyle());
}

bool iam_style_exists(ImGuiID style_id) {
	using namespace iam_style_detail;
	registered_style* s = g_styles.GetByKey(style_id);
	return s && s->valid;
}

void iam_style_unregister(ImGuiID style_id) {
	using namespace iam_style_detail;
	registered_style* s = g_styles.GetByKey(style_id);
	if (s) {
		s->valid = false;
	}
}

void iam_style_blend_to(ImGuiID style_a, ImGuiID style_b, float t, ImGuiStyle* out_style, int color_space) {
	using namespace iam_style_detail;

	registered_style* sa = g_styles.GetByKey(style_a);
	registered_style* sb = g_styles.GetByKey(style_b);

	if (!sa || !sa->valid || !sb || !sb->valid) {
		return;
	}

	blend_styles(sa->style, sb->style, t, out_style, color_space);
}

void iam_style_blend(ImGuiID style_a, ImGuiID style_b, float t, int color_space) {
	ImGuiStyle& current = ImGui::GetStyle();
	iam_style_blend_to(style_a, style_b, t, &current, color_space);
}

void iam_style_tween(ImGuiID id, ImGuiID target_style, float duration, iam_ease_desc const& ease, int color_space, float dt) {
	using namespace iam_style_detail;

	style_tween_state* state = g_style_tweens.GetOrAddByKey(id);

	// Check if target changed
	if (state->target_style != target_style || !state->active) {
		// Save current style as source
		ImGuiID source_id = ImHashStr("__current_style_source");
		iam_style_register_current(source_id);

		state->source_style = source_id;
		state->target_style = target_style;
		state->t = 0.0f;
		state->duration = duration;
		state->ease = ease;
		state->color_space = color_space;
		state->active = true;
	}

	if (!state->active) return;

	state->t += dt / state->duration;
	if (state->t >= 1.0f) {
		state->t = 1.0f;
		state->active = false;
	}

	float eased_t = iam_detail::eval(state->ease, state->t);
	iam_style_blend(state->source_style, state->target_style, eased_t, state->color_space);
}

// ============================================================
// GRADIENT INTERPOLATION
// ============================================================
iam_gradient& iam_gradient::add(float position, ImVec4 color) {
	// Find insertion point to maintain sorted order
	int insert_idx = positions.Size;
	for (int i = 0; i < positions.Size; ++i) {
		if (position < positions[i]) {
			insert_idx = i;
			break;
		}
	}
	// Insert at the correct position
	positions.insert(positions.Data + insert_idx, position);
	colors.insert(colors.Data + insert_idx, color);
	return *this;
}

ImVec4 iam_gradient::sample(float t, int color_space) const {
	int count = positions.Size;
	if (count == 0) return ImVec4(1, 1, 1, 1);
	if (count == 1) return colors[0];
	// Clamp t
	if (t <= positions[0]) return colors[0];
	if (t >= positions[count - 1]) return colors[count - 1];
	// Find the two stops we are between (positions are already sorted)
	for (int i = 0; i < count - 1; ++i) {
		if (t >= positions[i] && t <= positions[i + 1]) {
			float range = positions[i + 1] - positions[i];
			float local_t = (range > 1e-6f) ? (t - positions[i]) / range : 0.0f;
			return iam_detail::color::lerp_color(colors[i], colors[i + 1], local_t, color_space);
		}
	}
	return colors[count - 1];
}

iam_gradient iam_gradient::solid(ImVec4 color) {
	iam_gradient g;
	g.add(0.0f, color);
	g.add(1.0f, color);
	return g;
}

iam_gradient iam_gradient::two_color(ImVec4 start, ImVec4 end) {
	iam_gradient g;
	g.add(0.0f, start);
	g.add(1.0f, end);
	return g;
}

iam_gradient iam_gradient::three_color(ImVec4 start, ImVec4 mid, ImVec4 end) {
	iam_gradient g;
	g.add(0.0f, start);
	g.add(0.5f, mid);
	g.add(1.0f, end);
	return g;
}

iam_gradient iam_gradient_lerp(iam_gradient const& a, iam_gradient const& b, float t, int color_space) {
	// Strategy: sample both gradients at unified positions and blend
	ImVector<float> all_positions;

	for (int i = 0; i < a.positions.Size; ++i) {
		all_positions.push_back(a.positions[i]);
	}
	for (int i = 0; i < b.positions.Size; ++i) {
		bool found = false;
		for (int j = 0; j < all_positions.Size; ++j) {
			if (fabsf(all_positions[j] - b.positions[i]) < 1e-6f) {
				found = true;
				break;
			}
		}
		if (!found) {
			all_positions.push_back(b.positions[i]);
		}
	}

	// Sort positions
	for (int i = 0; i < all_positions.Size - 1; ++i) {
		for (int j = i + 1; j < all_positions.Size; ++j) {
			if (all_positions[j] < all_positions[i]) {
				float tmp = all_positions[i];
				all_positions[i] = all_positions[j];
				all_positions[j] = tmp;
			}
		}
	}

	// Build result gradient
	iam_gradient result;
	for (int i = 0; i < all_positions.Size; ++i) {
		ImVec4 color_a = a.sample(all_positions[i], color_space);
		ImVec4 color_b = b.sample(all_positions[i], color_space);
		ImVec4 blended = iam_detail::color::lerp_color(color_a, color_b, t, color_space);
		result.add(all_positions[i], blended);
	}

	return result;
}

// Gradient channel for tweening
namespace iam_gradient_detail {

struct gradient_chan {
	iam_gradient current, start, target;
	float dur, t;  // t is cached progress for backward compatibility
	double start_time;
	iam_ease_desc ez;
	int policy;
	int color_space;
	unsigned last_seen_frame;
	unsigned sleeping;

	gradient_chan() {
		dur = 1e-6f; t = 1.0f; start_time = 0;
		ez = { iam_ease_out_cubic, 0, 0, 0, 0 };
		policy = iam_policy_crossfade;
		color_space = iam_col_oklab;
		last_seen_frame = 0;
		sleeping = 1;
	}

	void set(iam_gradient const& trg, float d, iam_ease_desc const& e, int pol, int cs) {
		start = current;
		target = trg;
		dur = (d <= 1e-6f ? 1e-6f : d);
		start_time = iam_detail::g_global_time;
		t = 0;
		ez = e;
		policy = pol;
		color_space = cs;
		sleeping = 0;
	}

	float progress() {
		if (sleeping) { t = 1.0f; return 1.0f; }
		t = (float)((iam_detail::g_global_time - start_time) / dur);
		if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
		return t;
	}

	iam_gradient evaluate() {
		if (sleeping) return current;
		progress();
		if (t >= 1.0f) { current = target; sleeping = 1; return current; }
		float k = iam_detail::eval(ez, t);
		current = iam_gradient_lerp(start, target, k, color_space);
		return current;
	}

	void tick(float) { evaluate(); }
};

static ImPool<gradient_chan> g_gradient_pool;
static unsigned g_gradient_frame = 0;

} // namespace iam_gradient_detail

iam_gradient iam_tween_gradient(ImGuiID id, ImGuiID channel_id, iam_gradient const& target, float dur, iam_ease_desc const& ez, int policy, int color_space, float dt) {
	using namespace iam_gradient_detail;

	dt *= iam_detail::g_time_scale;
	ImGuiID key = iam_detail::make_key(id, channel_id);
	gradient_chan* c = g_gradient_pool.GetOrAddByKey(key);
	c->last_seen_frame = g_gradient_frame;

	// Fast path: sleeping and target unchanged
	if (c->sleeping && c->target.stop_count() == target.stop_count()) {
		bool same = true;
		for (int i = 0; i < target.stop_count() && same; ++i) {
			if (fabsf(c->target.positions[i] - target.positions[i]) > 1e-6f) same = false;
			if (fabsf(c->target.colors[i].x - target.colors[i].x) > 1e-6f) same = false;
			if (fabsf(c->target.colors[i].y - target.colors[i].y) > 1e-6f) same = false;
			if (fabsf(c->target.colors[i].z - target.colors[i].z) > 1e-6f) same = false;
			if (fabsf(c->target.colors[i].w - target.colors[i].w) > 1e-6f) same = false;
		}
		if (same) return c->current;
	}

	// Check if target changed
	bool change = (c->policy != policy) || (c->ez.type != ez.type) ||
	              (c->ez.p0 != ez.p0) || (c->ez.p1 != ez.p1) || (c->ez.p2 != ez.p2) || (c->ez.p3 != ez.p3) ||
	              (c->progress() >= 1.0f) || (c->target.stop_count() != target.stop_count());

	if (!change) {
		for (int i = 0; i < target.stop_count() && !change; ++i) {
			if (fabsf(c->target.positions[i] - target.positions[i]) > 1e-6f) change = true;
			if (fabsf(c->target.colors[i].x - target.colors[i].x) > 1e-6f) change = true;
			if (fabsf(c->target.colors[i].y - target.colors[i].y) > 1e-6f) change = true;
			if (fabsf(c->target.colors[i].z - target.colors[i].z) > 1e-6f) change = true;
			if (fabsf(c->target.colors[i].w - target.colors[i].w) > 1e-6f) change = true;
		}
	}

	if (change) {
		if (policy == iam_policy_cut) {
			c->current = c->start = c->target = target;
			c->sleeping = 1; c->dur = 1e-6f; c->ez = ez; c->policy = policy; c->color_space = color_space;
			c->sleeping = 1;
		} else {
			if (c->progress() < 1.0f && dt > 0) c->tick(dt);
			c->set(target, dur, ez, policy, color_space);
			c->tick(dt);
		}
	} else {
		c->tick(dt);
	}

	return c->current;
}

// ============================================================
// TRANSFORM INTERPOLATION
// ============================================================

iam_transform iam_transform::operator*(iam_transform const& other) const {
	// Combine: first apply other, then this
	// Result position = this.apply(other.position)
	// Result rotation = this.rotation + other.rotation
	// Result scale = this.scale * other.scale
	iam_transform result;
	result.scale = ImVec2(scale.x * other.scale.x, scale.y * other.scale.y);
	result.rotation = rotation + other.rotation;

	// Apply this transform to other's position
	float c = ImCos(rotation);
	float s = ImSin(rotation);
	result.position.x = position.x + (other.position.x * c - other.position.y * s) * scale.x;
	result.position.y = position.y + (other.position.x * s + other.position.y * c) * scale.y;

	return result;
}

ImVec2 iam_transform::apply(ImVec2 point) const {
	// Apply scale, then rotation, then translation
	float c = ImCos(rotation);
	float s = ImSin(rotation);
	float sx = point.x * scale.x;
	float sy = point.y * scale.y;
	return ImVec2(
		position.x + sx * c - sy * s,
		position.y + sx * s + sy * c
	);
}

iam_transform iam_transform::inverse() const {
	iam_transform result;

	// Inverse scale
	result.scale = ImVec2(
		fabsf(scale.x) > 1e-6f ? 1.0f / scale.x : 1.0f,
		fabsf(scale.y) > 1e-6f ? 1.0f / scale.y : 1.0f
	);

	// Inverse rotation
	result.rotation = -rotation;

	// Inverse translation (apply inverse rotation and scale to negated position)
	float c = ImCos(-rotation);
	float s = ImSin(-rotation);
	result.position.x = (-position.x * c + position.y * s) * result.scale.x;
	result.position.y = (-position.x * s - position.y * c) * result.scale.y;

	return result;
}

// Calculate rotation difference based on mode
static float angle_diff_mode(float from, float to, int mode) {
	// Normalize angles to [0, 2pi)
	float from_n = ImFmod(from, IAM_2PI);
	if (from_n < 0) from_n += IAM_2PI;
	float to_n = ImFmod(to, IAM_2PI);
	if (to_n < 0) to_n += IAM_2PI;

	float diff = to_n - from_n;

	switch (mode) {
	case iam_rotation_shortest:
		// Shortest path: wrap to [-IAM_PI, IAM_PI]
		if (diff > IAM_PI) diff -= IAM_2PI;
		else if (diff < -IAM_PI) diff += IAM_2PI;
		break;

	case iam_rotation_longest:
		// Longest path: take the long way around
		if (diff > 0 && diff < IAM_PI) diff -= IAM_2PI;
		else if (diff < 0 && diff > -IAM_PI) diff += IAM_2PI;
		break;

	case iam_rotation_cw:
		// Clockwise (positive): ensure diff is positive
		if (diff < 0) diff += IAM_2PI;
		break;

	case iam_rotation_ccw:
		// Counter-clockwise (negative): ensure diff is negative
		if (diff > 0) diff -= IAM_2PI;
		break;

	case iam_rotation_direct:
	default:
		// Direct: use raw difference (to - from), no wrapping
		diff = to - from;
		break;
	}

	return diff;
}

// Legacy helper for internal use (shortest path)
static float angle_diff(float from, float to) {
	return angle_diff_mode(from, to, iam_rotation_shortest);
}

iam_transform iam_transform_lerp(iam_transform const& a, iam_transform const& b, float t, int rotation_mode) {
	iam_transform result;

	// Lerp position linearly
	float pos_x = a.position.x + (b.position.x - a.position.x) * t;
	float pos_y = a.position.y + (b.position.y - a.position.y) * t;
	result.position = ImVec2(pos_x, pos_y);

	// Lerp scale linearly
	float scale_x = a.scale.x + (b.scale.x - a.scale.x) * t;
	float scale_y = a.scale.y + (b.scale.y - a.scale.y) * t;
	result.scale = ImVec2(scale_x, scale_y);

	// Lerp rotation using specified mode
	float diff = angle_diff_mode(a.rotation, b.rotation, rotation_mode);
	result.rotation = a.rotation + diff * t;

	return result;
}

iam_transform iam_transform_from_matrix(float m00, float m01, float m10, float m11, float tx, float ty) {
	iam_transform t;

	// Extract translation
	t.position = ImVec2(tx, ty);

	// Extract scale
	t.scale.x = sqrtf(m00 * m00 + m10 * m10);
	t.scale.y = sqrtf(m01 * m01 + m11 * m11);

	// Check for reflection (negative determinant)
	float det = m00 * m11 - m01 * m10;
	if (det < 0) t.scale.x = -t.scale.x;

	// Extract rotation
	t.rotation = atan2f(m10, m00);

	return t;
}

void iam_transform_to_matrix(iam_transform const& t, float* out_matrix) {
	float c = ImCos(t.rotation);
	float s = ImSin(t.rotation);

	// Row-major: [m00 m01 tx; m10 m11 ty]
	out_matrix[0] = c * t.scale.x;  // m00
	out_matrix[1] = -s * t.scale.y; // m01
	out_matrix[2] = t.position.x;   // tx
	out_matrix[3] = s * t.scale.x;  // m10
	out_matrix[4] = c * t.scale.y;  // m11
	out_matrix[5] = t.position.y;   // ty
}

// Transform channel for tweening
namespace iam_transform_detail {

struct transform_chan {
	iam_transform current, start, target;
	float dur, t;  // t is cached progress for backward compatibility
	double start_time;
	iam_ease_desc ez;
	int policy;
	int rotation_mode;
	unsigned last_seen_frame;
	unsigned sleeping;

	transform_chan() : current(), start(), target() {
		dur = 1e-6f; t = 1.0f; start_time = 0;
		ez = { iam_ease_out_cubic, 0, 0, 0, 0 };
		policy = iam_policy_crossfade;
		rotation_mode = iam_rotation_shortest;
		last_seen_frame = 0;
		sleeping = 1;
	}

	void set(iam_transform const& trg, float d, iam_ease_desc const& e, int pol, int rot_mode) {
		start = current;
		target = trg;
		dur = (d <= 1e-6f ? 1e-6f : d);
		start_time = iam_detail::g_global_time;
		t = 0;
		ez = e;
		policy = pol;
		rotation_mode = rot_mode;
		sleeping = 0;
	}

	float progress() {
		if (sleeping) { t = 1.0f; return 1.0f; }
		t = (float)((iam_detail::g_global_time - start_time) / dur);
		if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
		return t;
	}

	iam_transform evaluate() {
		if (sleeping) return current;
		progress();
		if (t >= 1.0f) { current = target; sleeping = 1; return current; }
		float k = iam_detail::eval(ez, t);
		current = iam_transform_lerp(start, target, k, rotation_mode);
		return current;
	}

	void tick(float) { evaluate(); }
};

static ImPool<transform_chan> g_transform_pool;
static unsigned g_transform_frame = 0;

} // namespace iam_transform_detail

iam_transform iam_tween_transform(ImGuiID id, ImGuiID channel_id, iam_transform const& target, float dur, iam_ease_desc const& ez, int policy, int rotation_mode, float dt) {
	using namespace iam_transform_detail;

	dt *= iam_detail::g_time_scale;
	ImGuiID key = iam_detail::make_key(id, channel_id);

	// Check if channel exists first
	transform_chan* c = g_transform_pool.GetByKey(key);
	bool is_new = (c == nullptr);
	if (is_new) {
		c = g_transform_pool.GetOrAddByKey(key);
		// Explicitly initialize new channel
		c->current = target;  // Start at target position (no animation on first frame)
		c->start = target;
		c->target = target;
		c->dur = 1e-6f;
		c->t = 1.0f;
		c->start_time = iam_detail::g_global_time;
		c->ez = ez;
		c->policy = policy;
		c->rotation_mode = rotation_mode;
		c->sleeping = 1;
	}
	c->last_seen_frame = g_transform_frame;

	// Fast path: sleeping and target unchanged
	if (c->sleeping) {
		float pos_diff = fabsf(c->target.position.x - target.position.x) + fabsf(c->target.position.y - target.position.y);
		float rot_diff = fabsf(angle_diff(c->target.rotation, target.rotation));
		float scale_diff = fabsf(c->target.scale.x - target.scale.x) + fabsf(c->target.scale.y - target.scale.y);
		if (pos_diff <= 1e-6f && rot_diff <= 1e-6f && scale_diff <= 1e-6f) {
			return c->current;
		}
	}

	// Check if target changed
	float pos_diff = fabsf(c->target.position.x - target.position.x) + fabsf(c->target.position.y - target.position.y);
	float rot_diff = fabsf(angle_diff(c->target.rotation, target.rotation));
	float scale_diff = fabsf(c->target.scale.x - target.scale.x) + fabsf(c->target.scale.y - target.scale.y);

	bool change = (c->policy != policy) || (c->rotation_mode != rotation_mode) || (c->ez.type != ez.type) ||
	              (c->ez.p0 != ez.p0) || (c->ez.p1 != ez.p1) || (c->ez.p2 != ez.p2) || (c->ez.p3 != ez.p3) ||
	              (pos_diff > 1e-6f) || (rot_diff > 1e-6f) || (scale_diff > 1e-6f) || (c->progress() >= 1.0f);

	if (change) {
		if (policy == iam_policy_cut) {
			c->current = c->start = c->target = target;
			c->sleeping = 1; c->dur = 1e-6f; c->ez = ez; c->policy = policy;
			c->rotation_mode = rotation_mode;
			c->sleeping = 1;
		} else {
			if (c->progress() < 1.0f && dt > 0) c->tick(dt);
			c->set(target, dur, ez, policy, rotation_mode);
			c->tick(dt);
		}
	} else {
		c->tick(dt);
	}

	return c->current;
}

// ----------------------------------------------------
// Unified Inspector (combines Debug Window + Animation Inspector)
// ----------------------------------------------------

void iam_show_unified_inspector(bool* p_open) {
	if (!ImGui::Begin("ImAnim Inspector", p_open, ImGuiWindowFlags_None)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTabBar("UnifiedInspectorTabs")) {
		// Debug Tab
		if (ImGui::BeginTabItem("Debug")) {
			// Time scale control
			if (ImGui::CollapsingHeader("Time Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
				float scale = iam_detail::g_time_scale;
				ImGui::SliderFloat("Global Time Scale", &scale, 0.0f, 2.0f, "%.2fx");
				if (scale != iam_detail::g_time_scale) {
					iam_detail::g_time_scale = scale;
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset##timescale")) {
					iam_detail::g_time_scale = 1.0f;
				}

				ImGui::Text("Presets:");
				ImGui::SameLine();
				if (ImGui::SmallButton("0.1x")) iam_detail::g_time_scale = 0.1f;
				ImGui::SameLine();
				if (ImGui::SmallButton("0.25x")) iam_detail::g_time_scale = 0.25f;
				ImGui::SameLine();
				if (ImGui::SmallButton("0.5x")) iam_detail::g_time_scale = 0.5f;
				ImGui::SameLine();
				if (ImGui::SmallButton("1x")) iam_detail::g_time_scale = 1.0f;
				ImGui::SameLine();
				if (ImGui::SmallButton("2x")) iam_detail::g_time_scale = 2.0f;
			}

			// Tween stats
			if (ImGui::CollapsingHeader("Tween Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Active Tweens:");
				ImGui::Indent();
				ImGui::Text("Float:  %d", iam_detail::g_float.pool.GetAliveCount());
				ImGui::Text("Vec2:   %d", iam_detail::g_vec2.pool.GetAliveCount());
				ImGui::Text("Vec4:   %d", iam_detail::g_vec4.pool.GetAliveCount());
				ImGui::Text("Int:    %d", iam_detail::g_int.pool.GetAliveCount());
				ImGui::Text("Color:  %d", iam_detail::g_color.pool.GetAliveCount());
				int total = iam_detail::g_float.pool.GetAliveCount() +
				            iam_detail::g_vec2.pool.GetAliveCount() +
				            iam_detail::g_vec4.pool.GetAliveCount() +
				            iam_detail::g_int.pool.GetAliveCount() +
				            iam_detail::g_color.pool.GetAliveCount();
				ImGui::Unindent();
				ImGui::Text("Total:  %d", total);
			}

			// Clip stats
			if (ImGui::CollapsingHeader("Clip Stats")) {
				ImGui::Text("Registered Clips: %d", iam_clip_detail::g_clip_sys.clips.Size);
				ImGui::Text("Active Instances: %d", iam_clip_detail::g_clip_sys.instances.Size);
			}

			ImGui::EndTabItem();
		}

		// Animation Inspector Tab
		if (ImGui::BeginTabItem("Animations")) {
			// List active clips/instances
			auto& instances = iam_clip_detail::g_clip_sys.instances;
			if (instances.Size == 0) {
				ImGui::TextDisabled("No active animation instances");
			} else {
				for (int i = 0; i < instances.Size; i++) {
					auto& inst = instances[i];
					ImGui::PushID(i);
					if (ImGui::TreeNode("Instance", "Instance %d (clip 0x%08X)", i, inst.clip_id)) {
						ImGui::Text("Clip ID: 0x%08X", inst.clip_id);
						ImGui::Text("Time: %.2f", inst.time);
						ImGui::Text("Playing: %s", inst.playing ? "Yes" : "No");
						ImGui::Text("Loops Left: %d", inst.loops_left);
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
			}
			ImGui::EndTabItem();
		}

		// Performance Tab
		if (ImGui::BeginTabItem("Performance")) {
			auto& prof = iam_detail::g_profiler;

			// Enable/disable toggle
			bool enabled = prof.enabled;
			if (ImGui::Checkbox("Enable Profiler", &enabled)) {
				iam_profiler_enable(enabled);
			}

			if (!prof.enabled) {
				ImGui::TextDisabled("Profiler is disabled. Enable to collect timing data.");
				ImGui::TextDisabled("Call iam_profiler_begin_frame() at frame start,");
				ImGui::TextDisabled("iam_profiler_end_frame() at frame end.");
			} else {
				ImGui::Separator();

				// Frame time graph
				if (ImGui::CollapsingHeader("Frame Time", ImGuiTreeNodeFlags_DefaultOpen)) {
					// Calculate average frame time
					float avg_frame_time = 0.0f;
					float max_frame_time = 0.0f;
					for (int i = 0; i < iam_detail::PROFILER_HISTORY_SIZE; i++) {
						avg_frame_time += prof.frame_history[i];
						if (prof.frame_history[i] > max_frame_time) max_frame_time = prof.frame_history[i];
					}
					avg_frame_time /= iam_detail::PROFILER_HISTORY_SIZE;

					ImGui::Text("Current: %.3f ms | Avg: %.3f ms | Max: %.3f ms",
						prof.frame_total_time, avg_frame_time, max_frame_time);

					// Plot frame time history
					char overlay[64];
					snprintf(overlay, sizeof(overlay), "%.2f ms", (float)prof.frame_total_time);
					ImGui::PlotLines("##FrameTime", prof.frame_history, iam_detail::PROFILER_HISTORY_SIZE,
						prof.frame_history_idx, overlay, 0.0f, max_frame_time * 1.2f, ImVec2(-1, 80));
				}

				// Per-section breakdown
				if (ImGui::CollapsingHeader("Section Breakdown", ImGuiTreeNodeFlags_DefaultOpen)) {
					if (prof.section_count == 0) {
						ImGui::TextDisabled("No profiler sections recorded.");
						ImGui::TextDisabled("Use iam_profiler_begin(\"name\") and iam_profiler_end() in code.");
					} else {
						float avail_width = ImGui::GetContentRegionAvail().x;
						float col0_width = avail_width * 0.25f;  // Section name
						float col1_width = avail_width * 0.125f; // Time
						float col2_width = avail_width * 0.125f; // Calls
						float col3_width = avail_width * 0.5f;   // Graph (remaining)

						ImGui::Columns(4, "ProfilerSections");
						ImGui::SetColumnWidth(0, col0_width);
						ImGui::SetColumnWidth(1, col1_width);
						ImGui::SetColumnWidth(2, col2_width);
						ImGui::SetColumnWidth(3, col3_width);
						ImGui::Text("Section"); ImGui::NextColumn();
						ImGui::Text("Time (ms)"); ImGui::NextColumn();
						ImGui::Text("Calls"); ImGui::NextColumn();
						ImGui::Text("Graph"); ImGui::NextColumn();
						ImGui::Separator();

						float row_height = ImGui::GetTextLineHeight() * 1.5f;
						for (int i = 0; i < prof.section_count; i++) {
							auto& sec = prof.sections[i];
							// Calculate max for this section's history
							float sec_max = 0.0f;
							for (int j = 0; j < iam_detail::PROFILER_HISTORY_SIZE; j++) {
								if (sec.history[j] > sec_max) sec_max = sec.history[j];
							}
							if (sec_max < 0.01f) sec_max = 0.01f;

							// Vertically center text with plot height
							float text_offset = (row_height - ImGui::GetTextLineHeight()) * 0.5f;
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + text_offset);
							ImGui::Text("%s", sec.name); ImGui::NextColumn();
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + text_offset);
							ImGui::Text("%.3f", sec.accumulated_time); ImGui::NextColumn();
							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + text_offset);
							ImGui::Text("%d", sec.call_count); ImGui::NextColumn();
							ImGui::PushID(i);
							ImGui::PlotLines("##SectionGraph", sec.history, iam_detail::PROFILER_HISTORY_SIZE,
								sec.history_idx, nullptr, 0.0f, sec_max * 1.2f, ImVec2(-1, row_height));
							ImGui::PopID();
							ImGui::NextColumn();
						}
						ImGui::Columns(1);
					}
				}
			}
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
}

// ----------------------------------------------------
// Drag Feedback - animated feedback for drag operations
// ----------------------------------------------------

namespace iam_detail {
	struct drag_state {
		ImVec2 start_pos;
		ImVec2 current_pos;
		ImVec2 prev_pos;
		ImVec2 velocity;
		bool is_dragging;
		bool is_snapping;
		float snap_progress;
		ImVec2 snap_start;
		ImVec2 snap_target;
		float snap_duration;
		int snap_ease;
	};
	static ImGuiStorage g_drag_states;
}

iam_drag_feedback iam_drag_begin(ImGuiID id, ImVec2 pos) {
	iam_detail::drag_state* state = (iam_detail::drag_state*)iam_detail::g_drag_states.GetVoidPtr(id);
	if (!state) {
		state = IM_NEW(iam_detail::drag_state)();
		iam_detail::g_drag_states.SetVoidPtr(id, state);
	}

	state->start_pos = pos;
	state->current_pos = pos;
	state->prev_pos = pos;
	state->velocity = ImVec2(0, 0);
	state->is_dragging = true;
	state->is_snapping = false;
	state->snap_progress = 0.0f;

	iam_drag_feedback fb;
	fb.position = pos;
	fb.offset = ImVec2(0, 0);
	fb.velocity = ImVec2(0, 0);
	fb.is_dragging = true;
	fb.is_snapping = false;
	fb.snap_progress = 0.0f;
	return fb;
}

iam_drag_feedback iam_drag_update(ImGuiID id, ImVec2 pos, float dt) {
	iam_detail::drag_state* state = (iam_detail::drag_state*)iam_detail::g_drag_states.GetVoidPtr(id);

	iam_drag_feedback fb;
	fb.position = pos;
	fb.offset = ImVec2(0, 0);
	fb.velocity = ImVec2(0, 0);
	fb.is_dragging = false;
	fb.is_snapping = false;
	fb.snap_progress = 0.0f;

	if (!state) return fb;

	// Calculate velocity
	if (dt > 0.0f) {
		state->velocity.x = (pos.x - state->prev_pos.x) / dt;
		state->velocity.y = (pos.y - state->prev_pos.y) / dt;
	}
	state->prev_pos = state->current_pos;
	state->current_pos = pos;

	fb.position = pos;
	fb.offset = ImVec2(pos.x - state->start_pos.x, pos.y - state->start_pos.y);
	fb.velocity = state->velocity;
	fb.is_dragging = state->is_dragging;
	fb.is_snapping = state->is_snapping;
	fb.snap_progress = state->snap_progress;
	return fb;
}

iam_drag_feedback iam_drag_release(ImGuiID id, ImVec2 pos, iam_drag_opts const& opts, float dt) {
	iam_detail::drag_state* state = (iam_detail::drag_state*)iam_detail::g_drag_states.GetVoidPtr(id);

	iam_drag_feedback fb;
	fb.position = pos;
	fb.offset = ImVec2(0, 0);
	fb.velocity = ImVec2(0, 0);
	fb.is_dragging = false;
	fb.is_snapping = false;
	fb.snap_progress = 1.0f;

	if (!state) return fb;

	state->is_dragging = false;

	// Check for snap targets
	ImVec2 snap_target = pos;
	bool should_snap = false;

	// Grid snapping
	if (opts.snap_grid.x > 0 || opts.snap_grid.y > 0) {
		if (opts.snap_grid.x > 0) snap_target.x = ImFloor(pos.x / opts.snap_grid.x + 0.5f) * opts.snap_grid.x;
		if (opts.snap_grid.y > 0) snap_target.y = ImFloor(pos.y / opts.snap_grid.y + 0.5f) * opts.snap_grid.y;
		should_snap = true;
	}

	// Point snapping (find closest)
	if (opts.snap_points && opts.snap_points_count > 0) {
		float min_dist = FLT_MAX;
		for (int i = 0; i < opts.snap_points_count; i++) {
			float dx = opts.snap_points[i].x - pos.x;
			float dy = opts.snap_points[i].y - pos.y;
			float dist = dx * dx + dy * dy;
			if (dist < min_dist) {
				min_dist = dist;
				snap_target = opts.snap_points[i];
			}
		}
		should_snap = true;
	}

	if (should_snap && opts.snap_duration > 0.0f) {
		state->is_snapping = true;
		state->snap_progress = 0.0f;
		state->snap_start = pos;
		state->snap_target = snap_target;
		state->snap_duration = opts.snap_duration;
		state->snap_ease = opts.ease_type;
	}

	// Update snapping animation
	if (state->is_snapping) {
		state->snap_progress += dt / state->snap_duration;
		if (state->snap_progress >= 1.0f) {
			state->snap_progress = 1.0f;
			state->is_snapping = false;
		}
		float t = iam_detail::eval_preset_internal(state->snap_ease, state->snap_progress);
		fb.position.x = state->snap_start.x + (state->snap_target.x - state->snap_start.x) * t;
		fb.position.y = state->snap_start.y + (state->snap_target.y - state->snap_start.y) * t;
	} else {
		fb.position = should_snap ? snap_target : pos;
	}

	fb.offset = ImVec2(fb.position.x - state->start_pos.x, fb.position.y - state->start_pos.y);
	fb.velocity = state->velocity;
	fb.is_dragging = false;
	fb.is_snapping = state->is_snapping;
	fb.snap_progress = state->snap_progress;
	return fb;
}

void iam_drag_cancel(ImGuiID id) {
	iam_detail::drag_state* state = (iam_detail::drag_state*)iam_detail::g_drag_states.GetVoidPtr(id);
	if (state) {
		state->is_dragging = false;
		state->is_snapping = false;
	}
}

// ============================================================
// DEBUG TIMELINE VISUALIZATION
// ============================================================

void iam_show_debug_timeline(ImGuiID instance_id) {
	using namespace iam_clip_detail;

	// Brand colors
	ImU32 const C1 = ZimaBlue;
	ImU32 const C2 = AgedCopper;
	ImU32 const C1_dim = IM_COL32(91, 194, 231, 80);
	ImU32 const C2_dim = IM_COL32(204, 120, 88, 80);
	ImU32 const C1_highlight = IM_COL32(120, 210, 240, 255);
	ImU32 const C2_highlight = IM_COL32(230, 140, 110, 255);
	ImU32 const BG_color = IM_COL32(30, 32, 40, 255);
	ImU32 const BG_track = IM_COL32(40, 44, 55, 255);
	ImU32 const grid_color = IM_COL32(60, 65, 80, 255);
	ImU32 const text_color = IM_COL32(180, 185, 195, 255);
	ImU32 const playhead_color = IM_COL32(255, 255, 255, 220);

	// Find instance
	iam_instance_data* inst = find_instance(instance_id);
	if (!inst) {
		ImGui::TextDisabled("Instance 0x%08X not found", instance_id);
		return;
	}

	// Find clip
	iam_clip_data* clip = find_clip(inst->clip_id);
	if (!clip) {
		ImGui::TextDisabled("Clip 0x%08X not found", inst->clip_id);
		return;
	}

	// Layout constants
	float const track_height = 20.0f;
	float const track_spacing = 2.0f;
	float const header_height = 22.0f;
	float const time_ruler_height = 36.0f;
	float const margin = 4.0f;
	float const label_width = ImGui::CalcTextSize("float").x + 8.0f;

	// Calculate widget dimensions
	ImVec2 avail = ImGui::GetContentRegionAvail();
	int num_tracks = clip->iam_tracks.Size;
	if (num_tracks == 0) num_tracks = 1;  // At least show something
	float const progress_bar_height = 24.0f;  // Space for progress bar at bottom
	float total_height = header_height + time_ruler_height + (track_height + track_spacing) * num_tracks + progress_bar_height + margin * 2;
	float timeline_width = avail.x - label_width - margin * 2;
	if (timeline_width < 100.0f) timeline_width = 100.0f;

	// Duration, delay, and current time
	float clip_delay = clip->delay;
	float duration = clip->duration;
	if (duration <= 0.0f) duration = 1.0f;
	float total_duration = clip_delay + duration;  // Total timeline length including delay
	float current_time = inst->time;
	float delay_left = inst->delay_left;

	// Get draw list and cursor position
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 cp = ImGui::GetCursorScreenPos();

	// Reserve space
	ImGui::Dummy(ImVec2(avail.x, total_height));

	// Background
	dl->AddRectFilled(cp, ImVec2(cp.x + avail.x, cp.y + total_height), BG_color, 4.0f);

	// Header - instance info
	{
		char header_text[128];
		if (delay_left > 0) {
			snprintf(header_text, sizeof(header_text), "Clip 0x%08X | Delay: %.2fs | %s",
				inst->clip_id, delay_left,
				inst->paused ? "PAUSED" : (inst->playing ? "WAITING" : "STOPPED"));
		} else {
			snprintf(header_text, sizeof(header_text), "Clip 0x%08X | %.2fs / %.2fs | %s",
				inst->clip_id, current_time, duration,
				inst->paused ? "PAUSED" : (inst->playing ? "PLAYING" : "STOPPED"));
		}

		ImVec2 text_pos = ImVec2(cp.x + margin, cp.y + margin);
		dl->AddText(text_pos, inst->playing ? C1 : text_color, header_text);
	}

	// Timeline area origin (after header, tracks start immediately)
	float timeline_y = cp.y + header_height * 2.0f;
	float timeline_x = cp.x + label_width + margin;

	// Draw tracks
	for (int i = 0; i < clip->iam_tracks.Size; i++) {
		iam_track& track = clip->iam_tracks[i];
		float track_y = timeline_y + (track_height + track_spacing) * i;

		// Track background
		dl->AddRectFilled(
			ImVec2(timeline_x, track_y),
			ImVec2(timeline_x + timeline_width, track_y + track_height),
			BG_track, 2.0f
		);

		// Track label
		{
			char const* type_names[] = { "float", "vec2", "vec4", "int", "color", "float_rel", "vec2_rel", "vec4_rel", "color_rel" };
			char const* type_name = (track.type >= 0 && track.type < 9) ? type_names[track.type] : "?";
			char label[64];
			snprintf(label, sizeof(label), "%s", type_name);
			ImVec2 label_pos = ImVec2(cp.x + margin, track_y + (track_height - ImGui::GetTextLineHeight()) / 2);
			dl->AddText(label_pos, text_color, label);
		}

		// Draw keyframe segments
		bool use_coral = (i % 2 == 1);  // Alternate colors
		ImU32 segment_color = use_coral ? C2_dim : C1_dim;
		ImU32 segment_active = use_coral ? C2 : C1;
		ImU32 segment_highlight = use_coral ? C2_highlight : C1_highlight;
		ImU32 delay_color = IM_COL32(50, 55, 70, 255);  // Dim color for delay zone

		// Draw delay zone if clip has delay
		if (clip_delay > 0) {
			float delay_end_x = timeline_x + (clip_delay / total_duration) * timeline_width;
			float seg_y1 = track_y + 2;
			float seg_y2 = track_y + track_height - 2;
			dl->AddRectFilled(ImVec2(timeline_x, seg_y1), ImVec2(delay_end_x, seg_y2), delay_color, 2.0f);
			// Draw dashed pattern to indicate waiting period
			for (float dx = timeline_x + 4; dx < delay_end_x - 4; dx += 12) {
				dl->AddLine(ImVec2(dx, seg_y1 + 2), ImVec2(dx + 6, seg_y2 - 2), IM_COL32(70, 75, 90, 255), 1.0f);
			}
		}

		for (int k = 0; k < track.keys.Size; k++) {
			keyframe& key = track.keys[k];
			float key_time = key.time;

			// Find next keyframe time (or end of clip)
			float next_time = duration;
			if (k + 1 < track.keys.Size) {
				next_time = track.keys[k + 1].time;
			}

			// Segment positions (offset by clip_delay)
			float x1 = timeline_x + ((clip_delay + key_time) / total_duration) * timeline_width;
			float x2 = timeline_x + ((clip_delay + next_time) / total_duration) * timeline_width;
			float seg_y1 = track_y + 2;
			float seg_y2 = track_y + track_height - 2;

			// Check if current time is within this segment
			bool is_active = (current_time >= key_time && current_time < next_time) && delay_left <= 0;

			// Draw segment
			ImU32 seg_col = is_active ? segment_active : segment_color;
			if (is_active) {
				// Glow effect for active segment
				dl->AddRectFilled(
					ImVec2(x1 - 1, seg_y1 - 1),
					ImVec2(x2 + 1, seg_y2 + 1),
					segment_highlight, 3.0f
				);
			}
			dl->AddRectFilled(ImVec2(x1, seg_y1), ImVec2(x2, seg_y2), seg_col, 2.0f);

			// Draw keyframe marker (diamond/dot at start)
			float marker_size = 8.0f;
			ImVec2 marker_center = ImVec2(x1, track_y + track_height / 2);
			dl->AddCircleFilled(marker_center, marker_size, is_active ? playhead_color : segment_active);

			// Hover detection - keyframe has priority over segment
			ImVec2 mouse = ImGui::GetMousePos();
			float hover_radius = 8.0f;  // Larger hover region for keyframe
			float dx = mouse.x - marker_center.x;
			float dy = mouse.y - marker_center.y;
			bool keyframe_hovered = (dx * dx + dy * dy <= hover_radius * hover_radius);

			if (keyframe_hovered) {
				// Highlight hovered keyframe
				dl->AddCircle(marker_center, marker_size + 3.0f, playhead_color, 0, 2.0f);

				ImGui::BeginTooltip();
				ImGui::Text("Time: %.3fs", key.time);

				// Display value based on type with color preview
				switch (key.type) {
					case iam_chan_float:
						ImGui::Text("Value: %.4f", key.value[0]);
						// Show as grayscale color
						{
							float v = ImClamp(key.value[0], 0.0f, 1.0f);
							ImVec4 col(v, v, v, 1.0f);
							ImGui::ColorButton("##val", col, ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
						}
						break;
					case iam_chan_vec2:
						ImGui::Text("Value: (%.3f, %.3f)", key.value[0], key.value[1]);
						// Show XY as RG color
						{
							float r = ImClamp(key.value[0], 0.0f, 1.0f);
							float g = ImClamp(key.value[1], 0.0f, 1.0f);
							ImVec4 col(r, g, 0.5f, 1.0f);
							ImGui::ColorButton("##val", col, ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
						}
						break;
					case iam_chan_vec4:
						ImGui::Text("Value: (%.3f, %.3f, %.3f, %.3f)", key.value[0], key.value[1], key.value[2], key.value[3]);
						{
							ImVec4 col(key.value[0], key.value[1], key.value[2], key.value[3]);
							ImGui::ColorButton("##val", col, ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
						}
						break;
					case iam_chan_int:
						ImGui::Text("Value: %d", *(int*)&key.value[0]);
						break;
					case iam_chan_color:
						ImGui::Text("Color: (%.3f, %.3f, %.3f, %.3f)", key.value[0], key.value[1], key.value[2], key.value[3]);
						{
							ImVec4 col(key.value[0], key.value[1], key.value[2], key.value[3]);
							ImGui::ColorButton("##val", col, ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
							char const* space_names[] = { "sRGB", "Linear", "HSV", "OKLAB", "OKLCH" };
							if (key.color_space >= 0 && key.color_space < 5) {
								ImGui::SameLine();
								ImGui::Text("(%s)", space_names[key.color_space]);
							}
						}
						break;
					default:
						ImGui::Text("Value: %.4f", key.value[0]);
						break;
				}

				// Easing info
				char const* ease_names[] = {
					"linear", "in_quad", "out_quad", "in_out_quad",
					"in_cubic", "out_cubic", "in_out_cubic",
					"in_quart", "out_quart", "in_out_quart",
					"in_quint", "out_quint", "in_out_quint",
					"in_sine", "out_sine", "in_out_sine",
					"in_expo", "out_expo", "in_out_expo",
					"in_circ", "out_circ", "in_out_circ",
					"in_back", "out_back", "in_out_back",
					"in_elastic", "out_elastic", "in_out_elastic",
					"in_bounce", "out_bounce", "in_out_bounce",
					"steps", "cubic_bezier", "spring", "custom"
				};
				if (key.ease_type >= 0 && key.ease_type < IM_ARRAYSIZE(ease_names))
					ImGui::Text("Ease: %s", ease_names[key.ease_type]);

				ImGui::EndTooltip();
			}
			else if (mouse.x >= x1 && mouse.x <= x2 && mouse.y >= seg_y1 && mouse.y <= seg_y2) {
				// Segment hover tooltip (for easing between keyframes)
				// Highlight hovered segment
				dl->AddRect(ImVec2(x1, seg_y1), ImVec2(x2, seg_y2), playhead_color, 2.0f, 0, 2.0f);

				ImGui::BeginTooltip();
				ImGui::Text("Segment: %.2fs - %.2fs", key_time, next_time);

				// Show easing applied from this keyframe to next
				char const* ease_names[] = {
					"linear", "in_quad", "out_quad", "in_out_quad",
					"in_cubic", "out_cubic", "in_out_cubic",
					"in_quart", "out_quart", "in_out_quart",
					"in_quint", "out_quint", "in_out_quint",
					"in_sine", "out_sine", "in_out_sine",
					"in_expo", "out_expo", "in_out_expo",
					"in_circ", "out_circ", "in_out_circ",
					"in_back", "out_back", "in_out_back",
					"in_elastic", "out_elastic", "in_out_elastic",
					"in_bounce", "out_bounce", "in_out_bounce",
					"steps", "cubic_bezier", "spring", "custom"
				};
				if (key.ease_type >= 0 && key.ease_type < IM_ARRAYSIZE(ease_names))
					ImGui::Text("Easing: %s", ease_names[key.ease_type]);
				else
					ImGui::Text("Easing: linear");

				if (key.has_bezier)
					ImGui::Text("Bezier: (%.2f, %.2f, %.2f, %.2f)", key.bezier[0], key.bezier[1], key.bezier[2], key.bezier[3]);

				if (key.is_spring)
					ImGui::Text("Spring: m=%.1f k=%.1f c=%.1f", key.spring.mass, key.spring.stiffness, key.spring.damping);

				ImGui::EndTooltip();
			}
		}

		// Grid lines (every 0.5s or so)
		float grid_step = 0.5f;
		if (duration > 10.0f) grid_step = 2.0f;
		else if (duration > 5.0f) grid_step = 1.0f;
		for (float t = grid_step; t < duration; t += grid_step) {
			float x = timeline_x + (t / duration) * timeline_width;
			dl->AddLine(ImVec2(x, track_y), ImVec2(x, track_y + track_height), grid_color, 0.5f);
		}
	}

	// If no tracks, show placeholder
	if (clip->iam_tracks.Size == 0) {
		float track_y = timeline_y;
		dl->AddRectFilled(
			ImVec2(timeline_x, track_y),
			ImVec2(timeline_x + timeline_width, track_y + track_height),
			BG_track, 2.0f
		);
		ImVec2 text_pos = ImVec2(timeline_x + 10, track_y + (track_height - ImGui::GetTextLineHeight()) / 2);
		dl->AddText(text_pos, text_color, "No tracks");
	}

	// Calculate tracks bottom position
	float tracks_bottom = timeline_y + (track_height + track_spacing) * ImMax(1, clip->iam_tracks.Size);

	// Time ruler (below tracks)
	float ruler_y = tracks_bottom + 2;
	{
		// Ruler background
		dl->AddRectFilled(
			ImVec2(timeline_x, ruler_y),
			ImVec2(timeline_x + timeline_width, ruler_y + time_ruler_height),
			BG_track
		);

		// Time markers (use total_duration to include delay)
		float time_step = 0.5f;  // Default step
		if (total_duration > 10.0f) time_step = 2.0f;
		else if (total_duration > 5.0f) time_step = 1.0f;
		else if (total_duration < 1.0f) time_step = 0.1f;

		for (float t = 0.0f; t <= total_duration + 0.001f; t += time_step) {
			float x = timeline_x + (t / total_duration) * timeline_width;
			dl->AddLine(ImVec2(x, ruler_y), ImVec2(x, ruler_y + 6), grid_color, 1.0f);

			char time_str[16];
			snprintf(time_str, sizeof(time_str), "%.1fs", t);
			ImVec2 text_size = ImGui::CalcTextSize(time_str);
			if (x + text_size.x / 2 < timeline_x + timeline_width) {
				dl->AddText(ImVec2(x - text_size.x / 2, ruler_y + 6), text_color, time_str);
			}
		}

		// Draw delay/animation boundary marker if delay exists
		if (clip_delay > 0) {
			float delay_x = timeline_x + (clip_delay / total_duration) * timeline_width;
			dl->AddLine(ImVec2(delay_x, ruler_y), ImVec2(delay_x, ruler_y + time_ruler_height), C1_dim, 2.0f);
			ImVec2 text_pos = ImVec2(delay_x + 2, ruler_y + 2);
			dl->AddText(text_pos, C1_dim, "Delay End");
		}
	}

	// Draw playhead (account for delay)
	{
		float effective_time;
		if (delay_left > 0) {
			// During delay: position within the delay zone
			effective_time = clip_delay - delay_left;
		} else {
			// After delay: position within the animation zone
			effective_time = clip_delay + current_time;
		}

		if (effective_time >= 0.0f && effective_time <= total_duration) {
			float playhead_x = timeline_x + (effective_time / total_duration) * timeline_width;
			dl->AddLine(ImVec2(playhead_x, timeline_y), ImVec2(playhead_x, ruler_y + time_ruler_height), playhead_color, 2.0f);

			// Playhead triangle at top
			ImVec2 tri[3] = {
				ImVec2(playhead_x, timeline_y),
				ImVec2(playhead_x - 5, timeline_y - 8),
				ImVec2(playhead_x + 5, timeline_y - 8)
			};
			dl->AddTriangleFilled(tri[0], tri[1], tri[2], playhead_color);
		}
	}
}

#undef IMGUI_STORAGE_PAIR
