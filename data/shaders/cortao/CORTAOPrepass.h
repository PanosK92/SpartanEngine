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


#include "CORTAOBase.h"

#ifndef CORTAO_PERSPECTIVE_PROJECTION
	#define CORTAO_PERSPECTIVE_PROJECTION 0
#endif

#ifndef CORTAO_ORTHOGRAPHIC_PROJECTION
	#define CORTAO_ORTHOGRAPHIC_PROJECTION 0
#endif

#if CORTAO_PERSPECTIVE_PROJECTION && CORTAO_ORTHOGRAPHIC_PROJECTION
	#error "Please define exactly one of CORTAO_PERSPECTIVE_PROJECTION and CORTAO_ORTHOGRAPHIC_PROJECTION to 1"
#endif

#if !CORTAO_PERSPECTIVE_PROJECTION && !CORTAO_ORTHOGRAPHIC_PROJECTION
	#error "Please define exactly one of CORTAO_PERSPECTIVE_PROJECTION and CORTAO_ORTHOGRAPHIC_PROJECTION to 1"
#endif



cortao_float cortao_loadDepth(cortao_int2 pixelCoords);
cortao_float3 cortao_loadNormal(cortao_int2 pixelCoords);

void cortao_writePackedData(cortao_int2 pixelCoords, cortao_int mipLevel, cortao_uint data);


struct CORTAOPrepassParameters {
	cortao_uint packedResolution;
	cortao_float depthScale;
	cortao_float depthOffset;
	cortao_uint randomSeed;
	
	cortao_uint mipCount;
	cortao_uint _padding0;
	cortao_uint _padding1;
	cortao_uint _padding2;
};


void cortao_prepass(cortao_uint2 pixelCoords, CORTAOPrepassParameters params) {
	
	//check if our pixel is out-of-bounds and early out in that case
	cortao_uint2 resolution = cortao_getResolution(params.packedResolution);
	if(pixelCoords.x >= resolution.x || pixelCoords.y >= resolution.y) {
		return;
	}
	
	cortao_float depth = cortao_loadDepth(cortao_int2(pixelCoords));
	
	cortao_uint packedData = 0u;
	
	cortao_float skyBoxDepth = cortao_float(params.depthScale < 0.0); //Handles both standard and inverted depth
	if(depth != skyBoxDepth) {
		
		//Calculate the linear depth
		#if CORTAO_PERSPECTIVE_PROJECTION
			cortao_float linearDepth = 1.0 / cortao_fma(depth, params.depthScale, params.depthOffset);
		#else
			cortao_float linearDepth = cortao_fma(depth, params.depthScale, params.depthOffset);
		#endif
		
		cortao_float3 normal = cortao_loadNormal(cortao_int2(pixelCoords)); 
		
		//Pack a spheremap encoded normal
		cortao_uint seed = cortao_hash(cortao_seed(pixelCoords) + params.randomSeed);
		cortao_float2 dither = cortao_float2(cortao_unpack16(seed)) * (1.0 / cortao_float(0xFFFFu));
		cortao_uint mask = (1 << CORTAO_PACKED_NORMAL_BITS) - 1;
		cortao_uint packedNormal = cortao_encodeNormal(normal, dither);
		
		//Merge the linear depth and the packed normal
		packedData = (cortao_float_to_uint(linearDepth) & ~mask) | (packedNormal & mask);
	}
	
	cortao_writePackedData(cortao_int2(pixelCoords), 0, packedData);

	cortao_unroll for(cortao_int mipLevel = 1; mipLevel < 8; mipLevel++) {

		cortao_uint2 nextCoords = pixelCoords >> 1u;
		
		cortao_uint2 dither = cortao_uint2(params.randomSeed, params.randomSeed) >> cortao_uint2(mipLevel*2, mipLevel*2 + 1); 
		
		cortao_bool wrongPixel = cortao_bool(((pixelCoords.x ^ nextCoords.y ^ dither.x) | (pixelCoords.y ^ nextCoords.x ^ dither.y)) & 0x1u);

		pixelCoords = nextCoords;
		resolution = max(resolution >> 1u, 1u);

		if(any(cortao_bool4(mipLevel >= params.mipCount, wrongPixel, pixelCoords.x >= resolution.x, pixelCoords.y >= resolution.y))) {
			return;
		}
		
		cortao_writePackedData(cortao_int2(pixelCoords), mipLevel, packedData);
	}
	
}