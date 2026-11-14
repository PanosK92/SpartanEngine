// CORTAO Reference Implementation v1.0.2
//
// MIT License
//
// Copyright (c) 2026 Daniel Isheden
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef CORTAO_GLSL
	#define CORTAO_GLSL 0
#endif

#ifndef CORTAO_HLSL
	#define CORTAO_HLSL 0
#endif

#if CORTAO_GLSL && CORTAO_HLSL
	#error "Please define exactly one of CORTAO_GLSL and CORTAO_HLSL to 1"
#endif

#ifndef CORTAO_16_BIT
	#define CORTAO_16_BIT 0
#endif

#if CORTAO_GLSL
	#define cortao_bool bool
	#define cortao_bool2 bvec2
	#define cortao_bool3 bvec3
	#define cortao_bool4 bvec4

	#define cortao_float float
	#define cortao_float2 vec2
	#define cortao_float3 vec3
	#define cortao_float4 vec4
	#define cortao_float3x3 mat3x3
	#define cortao_float4x4 mat4x4

	#define cortao_int int
	#define cortao_int2 ivec2
	#define cortao_int3 ivec3
	#define cortao_int4 ivec4

	#define cortao_uint uint
	#define cortao_uint2 uvec2
	#define cortao_uint3 uvec3
	#define cortao_uint4 uvec4
	
	#if CORTAO_16_BIT
		#define cortao_half float16_t
		#define cortao_half2 f16vec2
		#define cortao_half3 f16vec3
		#define cortao_half4 f16vec4
		#define cortao_half3x3 f16mat3x3
		#define cortao_half4x4 f16mat4x4

		#define cortao_short int16_t
		#define cortao_short2 i16vec2
		#define cortao_short3 i16vec3
		#define cortao_short4 i16vec4

		#define cortao_ushort uint16_t
		#define cortao_ushort2 u16vec2
		#define cortao_ushort3 u16vec3
		#define cortao_ushort4 u16vec4
		
	#else
		#define cortao_half float
		#define cortao_half2 vec2
		#define cortao_half3 vec3
		#define cortao_half4 vec4
		#define cortao_half3x3 mat3x3
		#define cortao_half4x4 mat4x4

		#define cortao_short int
		#define cortao_short2 ivec2
		#define cortao_short3 ivec3
		#define cortao_short4 ivec4

		#define cortao_ushort uint
		#define cortao_ushort2 uvec2
		#define cortao_ushort3 uvec3
		#define cortao_ushort4 uvec4
	#endif
	
	

	#define cortao_constant const

	//Please add '#extension GL_EXT_control_flow_attributes : enable' to enable loop unrolling
	#ifdef GL_EXT_control_flow_attributes
		#define cortao_unroll [[unroll]]
	#else
		#define cortao_unroll
	#endif
	
	//Functions

	#define cortao_float_to_uint(f) floatBitsToUint(f)
	#define cortao_uint_to_float(i) uintBitsToFloat(i)

	#define cortao_fma(a, b, c) fma(a, b, c)
	
	#define cortao_mul(a, b) ((a) * (b))

	#define cortao_lerp(x, y, a) mix(x, y, a)
	
	#define cortao_inverseSqrt(x) inversesqrt(x)

	#define cortao_bitCount(x) bitCount(x)
	
	#define cortao_unpack16(x) cortao_ushort2(x & 0xFFFFu, x >> 16u)

#elif CORTAO_HLSL
	#define cortao_bool bool
	#define cortao_bool2 bool2
	#define cortao_bool3 bool3
	#define cortao_bool4 bool4
	
	#define cortao_float float
	#define cortao_float2 float2
	#define cortao_float3 float3
	#define cortao_float4 float4
	#define cortao_float3x3 float3x3
	#define cortao_float4x4 float4x4
	
	#define cortao_int int
	#define cortao_int2 int2
	#define cortao_int3 int3
	#define cortao_int4 int4

	#define cortao_uint uint
	#define cortao_uint2 uint2
	#define cortao_uint3 uint3
	#define cortao_uint4 uint4
	
	
	#if CORTAO_16_BIT
		#define cortao_half float16_t
		#define cortao_half2 float16_t2
		#define cortao_half3 float16_t3
		#define cortao_half4 float16_t4
		#define cortao_half3x3 float16_t3x3
		#define cortao_half4x4 float16_t4x4

		#define cortao_short int16_t
		#define cortao_short2 int16_t2
		#define cortao_short3 int16_t3
		#define cortao_short4 int16_t4

		#define cortao_ushort uint16_t
		#define cortao_ushort2 uint16_t2
		#define cortao_ushort3 uint16_t3
		#define cortao_ushort4 uint16_t4
	#else
		#define cortao_half float
		#define cortao_half2 float2
		#define cortao_half3 float3
		#define cortao_half4 float4
		#define cortao_half3x3 float3x3
		#define cortao_half4x4 float4x4
	
		#define cortao_short int
		#define cortao_short2 int2
		#define cortao_short3 int3
		#define cortao_short4 int4
	
		#define cortao_ushort uint
		#define cortao_ushort2 uint2
		#define cortao_ushort3 uint3
		#define cortao_ushort4 uint4
	#endif



	#define cortao_constant static const

	#define cortao_unroll [unroll]

	#define cortao_float_to_uint(f) asuint(f)
	#define cortao_uint_to_float(i) asfloat(i)

	#define cortao_fma(a, b, c) ((a) * (b) + (c))
	
	#define cortao_mul(a, b) mul(b, a) //Flipped!

	#define cortao_lerp(x, y, a) lerp(x, y, a)

	#define cortao_inverseSqrt(x) rsqrt(x)

	#define cortao_bitCount(x) countbits(x)
	
	#define cortao_unpack16(x) cortao_ushort2(x & 0xFFFFu, x >> 16u)


#else
	#error "Please define exactly one of CORTAO_GLSL and CORTAO_HLSL to 1"
#endif



#ifndef CORTAO_PACKED_NORMAL_BITS
	#define CORTAO_PACKED_NORMAL_BITS 10
#endif

#if CORTAO_PACKED_NORMAL_BITS < 4 || CORTAO_PACKED_NORMAL_BITS > 20
	#error "CORTAO_PACKED_NORMAL_BITS must be between 4 and 20, inclusive."
#endif

#if (CORTAO_PACKED_NORMAL_BITS & 1) == 1
	#error "CORTAO_PACKED_NORMAL_BITS must be even when CORTAO_PACKED_DATA is CORTAO_PACKED_DATA_NORMAL."
#endif


cortao_constant cortao_float CORTAO_PI = cortao_float(3.1415926535897932384626433832795);
cortao_constant cortao_float CORTAO_PI2 = cortao_float(6.283185307179586476925286766559); //PI * 2.0
cortao_constant cortao_float CORTAO_GOLDEN_ANGLE = cortao_float(2.3999632297286533222315555066336); //PI * (3.0 - sqrt(5.0))


cortao_uint cortao_seed(cortao_uint2 coords) {
	cortao_uint2 q = coords * cortao_uint2(1719413u*929u, 140473u*2467u*11u);
	return q.x ^ q.y;
}

cortao_uint cortao_hash(cortao_uint n) {
    return n * (n ^ (n >> 15u));
}

cortao_uint2 cortao_hash(cortao_uint2 n) {
    return n * (n ^ (n >> 15u));
}


cortao_uint cortao_encodeNormal(cortao_float3 normal, cortao_float2 dither) {

	cortao_uint bitsPerComponent = cortao_uint(CORTAO_PACKED_NORMAL_BITS / 2);
	
	cortao_uint componentMask = (1u << bitsPerComponent) - 1u;
	cortao_uint componentMax = componentMask >> 1u;

	cortao_float2 encodedNormal = normal.xy * cortao_inverseSqrt(cortao_fma(normal.z, 2.0, 2.0));
	 
	cortao_int2 values = cortao_int2(floor(cortao_fma(encodedNormal, cortao_float2(componentMax, componentMax), dither)));

	return (cortao_uint(values.x) & componentMask) | ((cortao_uint(values.y) & componentMask) << bitsPerComponent);
}


cortao_half3 cortao_decodeNormal(cortao_uint packedData) {

	cortao_ushort bitsPerComponent = cortao_ushort(CORTAO_PACKED_NORMAL_BITS / 2);
	
	cortao_uint componentMask = (1u << bitsPerComponent) - 1u;
	cortao_uint componentMax = componentMask >> 1u;
	
	#if CORTAO_16_BIT
		cortao_ushort bits = cortao_ushort(16);
	#else
		cortao_ushort bits = cortao_ushort(32);
	#endif
	
	cortao_short2 leftShift = cortao_short2(bits - cortao_ushort2(bitsPerComponent, bitsPerComponent*2));
	cortao_short2 rightShift = cortao_short2(bits - bitsPerComponent, bits - bitsPerComponent);
	cortao_short2 values = cortao_short2((cortao_short2(packedData, packedData) << leftShift) >> rightShift);

	cortao_half2 e = cortao_half2(values) * cortao_half(1.0 / cortao_float(componentMax));
	
	cortao_half z = cortao_fma(-e.x, e.x, cortao_fma(-e.y, e.y, cortao_half(1.0)));
	return cortao_half3(e * (cortao_half(2.0) * sqrt(z)), cortao_fma(z, cortao_half(2.0), cortao_half(-1.0)));
}




cortao_float cortao_unpackDepth(cortao_uint packedData) {
	return cortao_uint_to_float(packedData);
}

cortao_float4 cortao_unpackDepth(cortao_uint4 packedData) {
	return cortao_uint_to_float(packedData);
}

cortao_ushort2 cortao_getResolution(cortao_uint packedResolution) {
	return cortao_unpack16(packedResolution);
}
