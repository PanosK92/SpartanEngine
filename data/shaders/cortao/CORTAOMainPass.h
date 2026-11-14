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


//Preprocessor checks

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

#ifndef CORTAO_SAMPLE_COUNT
	#error "Pleased define CORTAO_SAMPLE_COUNT to the number of samples to do per pixel."
#endif

#if CORTAO_SAMPLE_COUNT < 1
	#error "CORTAO_SAMPLE_COUNT must be at least 1."
#endif

#ifndef CORTAO_RAY_COUNT
	#error "Please define CORTAO_RAY_COUNT to the number of rays to simulate. Valid values are 8 to 32, inclusive. A good default is 16. If using CORTAO_OCCLUDER_LOOKUP_TABLE, this must match what the lookup table contains."
#endif

#if CORTAO_RAY_COUNT < 8 || CORTAO_RAY_COUNT > 32
	#error "CORTAO_RAY_COUNT must be between 8 and 32, inclusive."
#endif


#ifndef CORTAO_OCCLUDER_LOOKUP_TABLE
	#define CORTAO_OCCLUDER_LOOKUP_TABLE 0
#endif

#ifndef CORTAO_BENT_NORMAL_LOOKUP_TABLE
	#define CORTAO_BENT_NORMAL_LOOKUP_TABLE 0
#endif


#ifndef CORTAO_DEBUG_HOOKS
	#define CORTAO_DEBUG_HOOKS 0
#endif


//Functions to implement

cortao_half3 cortao_loadNormal(cortao_int2 pixelCoords);

cortao_uint cortao_samplePackedData(cortao_float2 texCoords, cortao_float mipmap);

#if CORTAO_OCCLUDER_LOOKUP_TABLE
	cortao_uint cortao_sampleOccluderLookupTable(cortao_float3 texCoords);
#endif

#if CORTAO_BENT_NORMAL_LOOKUP_TABLE
	cortao_half3 cortao_loadBentNormalLookupTable(cortao_uint rayBits);
#endif

void cortao_writeResult(cortao_int2 pixelCoords, cortao_half occlusion, cortao_half3 bentNormal, cortao_half3x3 tangentSpaceTransform, cortao_uint rayBits);


#if CORTAO_DEBUG_HOOKS
	void cortao_debugCenterPixel(cortao_float3 position, cortao_float3 normal, cortao_float radius);
	void cortao_debugCulledOccluder(cortao_uint index, cortao_float3 occluderPosition);
	void cortao_debugPassedOccluder(cortao_uint index, cortao_float3 occluderPosition, cortao_float3 occluderNormal, cortao_float occluderRadius, cortao_uint rayBits);
#endif



//Public structs

struct CORTAOMainPassParameters {
	
	cortao_uint packedResolution;
	cortao_uint randomSeed;
	cortao_float normalBias;
	cortao_float radius;
	
	cortao_float4 unprojectParams;

	cortao_float4 projectionParams;
	
	cortao_float maxApparentCircleRadiusCoefficient;
	cortao_float mipBias;
	cortao_float inverseMaxSlopeCompensation;
	cortao_float backfaceThreshold;
	
	cortao_float backfaceBoost;
	cortao_uint _padding0;
	cortao_uint _padding1;
	cortao_uint _padding2;
};


//Public functions

cortao_half3 cortao_rayDirection(cortao_uint rayIndex) {
	
	cortao_float angle = cortao_float(rayIndex) * CORTAO_GOLDEN_ANGLE;
	cortao_float radius = sqrt((cortao_float(rayIndex) + 0.5) / cortao_float(CORTAO_RAY_COUNT));
	cortao_half3 rayDirection = cortao_half3(cos(angle) * radius, sin(angle) * radius, sqrt(cortao_float(1.0) - radius*radius));
	return rayDirection;
}



//Internal CORTAO stuff

cortao_float3 cortao_unprojectWithOffset(cortao_float4 unprojectParameters, cortao_float2 texCoords, cortao_float linearDepth, cortao_float3 offset) {
	//Lerp between the frustum corner directions.
	//Optimized by precomputing the difference between the corners, to turn it into FMA.
	cortao_float2 direction = cortao_fma(texCoords, unprojectParameters.xy, unprojectParameters.zw);
	
	#if CORTAO_PERSPECTIVE_PROJECTION
		//Calculate the view space direction as direction * depth. 
		//Also add an optional offset here, as it's free to do with FMA.
		return cortao_fma(cortao_float3(direction, -1.0), cortao_float3(linearDepth, linearDepth, linearDepth), offset);
	#else
		//For orthographic projections, the "direction" is really the starting XY coordinates, and only Z is affected by depth.
		return cortao_float3(direction, -linearDepth) + offset;
	#endif
}

cortao_float3 cortao_unproject(cortao_float4 unprojectParameters, cortao_float2 texCoords, cortao_float linearDepth) {
	return cortao_unprojectWithOffset(unprojectParameters, texCoords, linearDepth, cortao_float3(0, 0, 0));
}


cortao_uint cortao_computeRayOcclusion(
			cortao_float offsetDither,
			cortao_float3x3 offsetTransform, 
			cortao_half3x3 tangentSpaceTransform, 
			cortao_float mipBias, 
			cortao_float3 viewPosition, 
			cortao_half3 normal, 
			cortao_half3 viewDirection, 
			cortao_float maxRadius,
			CORTAOMainPassParameters params
) {
	
	cortao_uint rayBits = 0u;
	
	//Hack to preload the occluder lookup table descriptor, which is marginally faster at higher sample counts.
	#if CORTAO_OCCLUDER_LOOKUP_TABLE && CORTAO_SAMPLE_COUNT > 4
		rayBits = cortao_sampleOccluderLookupTable(cortao_float3(0, 0, 0));
	#endif
	

	//Don't unroll if we have too many samples, to prevent driver compilers from croaking.
	#if CORTAO_SAMPLE_COUNT <= 64
	cortao_unroll
	#endif
	for(cortao_uint i = 0u; i < CORTAO_SAMPLE_COUNT; i++) {
		
		//these are constant for an unrolled loop, so free
		
		//Calculate the golden angle spiral direction
		cortao_float a = cortao_float(i) * CORTAO_GOLDEN_ANGLE;
		cortao_float2 spiral = cortao_float2(cos(a), sin(a));

		cortao_float inverseSampleCount = 1.0 / cortao_float(CORTAO_SAMPLE_COUNT);
	
		//The skew is how much we suck the sample distribution into the center.
		cortao_float skew = 3.5;
		
		//Calculate an inner and outer radius for this sample. Dithering is used to choose a radius between these two values.
		cortao_float innerRadius = pow((cortao_float(i) + 0.0) * inverseSampleCount, skew*0.5);
		cortao_float outerRadius = pow((cortao_float(i) + 1.0) * inverseSampleCount, skew*0.5);
		
		//Precompute the inner and outer spiral positions. The dithered position will lie somewhere on a lerp between the two.
		cortao_float2 innerSpiral = cortao_float2(spiral * innerRadius);
		cortao_float2 outerSpiral = cortao_float2(spiral * outerRadius);

		//Calculate the area of the sampling circle that the sample is randomly moved over,
		//based on both a random rotation and a random radius dither to form a circular strip.
		//The weight is sqaure-rooted to account for the fact that the weight is applied to a radius, rather than an area. 
		cortao_float sampleArea = outerRadius*outerRadius - innerRadius*innerRadius;
		cortao_float sampleWeight = sqrt(sampleArea); //The square root of the percentage of the area of the circle that the sample can be placed in

		//The mip level depends on how far away we are from the center.
		cortao_float sampleMipmap = log2(outerRadius);
		
		//dynamic stuff from here on
		
		//Optimized lerp between the inner and outer sample positions based on a random dither value. 
		cortao_float2 sampleOffset = cortao_fma(outerSpiral - innerSpiral, cortao_float2(offsetDither, offsetDither), innerSpiral);
		
		//Apply the sample transformation matrix and do a perspective division.
		cortao_float3 projectedCoords = cortao_mul(offsetTransform, cortao_float3(sampleOffset, 1.0));
		cortao_float2 sampleCoords = projectedCoords.xy;
		#if CORTAO_PERSPECTIVE_PROJECTION
			sampleCoords /= projectedCoords.z;
		#endif
		
		//Add the precomputed mip bias and the per-sample mip level together to get our mip level, based on logarithmic identities.
		cortao_float mipmap = mipBias + sampleMipmap;
		
		//Sample the center pixel, and unpack depth only for now.
		cortao_uint packedData = cortao_samplePackedData(sampleCoords, mipmap);
		cortao_float sampleDepth = cortao_unpackDepth(packedData);
		
		//Calculate this sample's view space position relative to the center pixel.
		cortao_float3 dPos = cortao_unprojectWithOffset(params.unprojectParams, sampleCoords, sampleDepth, -viewPosition);
		
		//Do an early out if the sample ends up outside the hemisphere (radius and normal check).
		//Skybox samples and samples outside the screen have a linear depth of 0.0, which is always outside the hemisphere.
		cortao_float lengthSqrd = dot(dPos, dPos);
		if(lengthSqrd < maxRadius*maxRadius && dot(dPos, cortao_float3(normal)) > 0.0) {
			
			//Calculate the occluder direction.
			cortao_float inverseLength = cortao_inverseSqrt(lengthSqrd);
			cortao_half3 direction = cortao_half3(dPos * inverseLength);

			//Unpack the sample normal from the packed data we already read.
			cortao_half3 sampleNormal = cortao_decodeNormal(packedData);
			
			//Calculate the slope compensation weight and use it to calculate the normalized visual occluder radius.
			cortao_half slopeCompensation = cortao_inverseSqrt(max(dot(sampleNormal, viewDirection), cortao_half(params.inverseMaxSlopeCompensation)));			
			cortao_float occluderRadius = (maxRadius * sampleWeight) * (slopeCompensation * inverseLength);

			//Heuristically check for backface occluders.
			cortao_half sampleDot = dot(sampleNormal, direction);
			if(sampleDot < cortao_half(params.backfaceThreshold)) {
				//Not a backface occluder. Apply a normal offset to prevent self-occlusion.
				//This moves the occluder circle by 1 radius into the occluder object. As this is done after occluder normalization, 
				//Re-normalize the direction after we apply the offset to get the new direction, but keep the occluder radius the same.
				cortao_half adjustedOffset = cortao_half(-occluderRadius) / cortao_half(max(1.0 - occluderRadius * occluderRadius * 0.5, 0.000001));
				direction = normalize(cortao_fma(sampleNormal, cortao_half3(adjustedOffset, adjustedOffset, adjustedOffset), direction));
			} else {
				//This is a backface occluder. Don't do the normal offset, and optionally boost the slope compensation to avoid light leaking.
				occluderRadius *= cortao_lerp(cortao_half(1.0), slopeCompensation, cortao_half(params.backfaceBoost));
			}

			//Transform the occluder direction into the ray tracing tangent space.
			cortao_half3 hemisphereDirection = cortao_mul(direction, tangentSpaceTransform);
			
			cortao_uint occluderRayBits;
			#if CORTAO_OCCLUDER_LOOKUP_TABLE
			
				//Encode the normal as a spheremap projection, which is used as the lookup coordinates together with the occluder radius.
				cortao_half mul = cortao_inverseSqrt(cortao_fma(hemisphereDirection.z, cortao_half(8.0), cortao_half(8.0)));
				cortao_half2 sphereMapCoordinates = cortao_fma(hemisphereDirection.xy, cortao_half2(mul, mul), cortao_half2(0.5, 0.5));
				cortao_float3 lookupCoords = cortao_float3(sphereMapCoordinates, occluderRadius * sqrt(0.5));
				
				//Sphere map projection fails if the tangent space direction is (0, 0, -1), so ignore the invalid sample we do in that case.
				occluderRayBits = cortao_sampleOccluderLookupTable(lookupCoords) & (hemisphereDirection.z > cortao_half(-0.99) ? 0xFFFFFFFFu : 0u);
			#else
			{
				
				//Calculate the angular size of the occluder. This wraps the occluder around the hemisphere,
				//matching the occluded area of the hemisphere with the area of the circle linearly, to improve the scalability of CORTAO.
				cortao_half occluderCosAngle = cortao_half(max(0.0, 1.0 - occluderRadius * occluderRadius * 0.5));
				
				//Manually check each ray against hte occluder's angular size.
				occluderRayBits = 0u;
				for(cortao_uint j = 0u; j < CORTAO_RAY_COUNT; j++) {
					cortao_half3 rayDirection = cortao_rayDirection(j);
					if(dot(rayDirection, hemisphereDirection) > occluderCosAngle) {
						occluderRayBits |= 1u << j; 
					}
				}
			}
			#endif
			
			rayBits |= occluderRayBits;
			
			#if CORTAO_DEBUG_HOOKS
				cortao_debugPassedOccluder(i, viewPosition + dPos, direction, occluderRadius, occluderRayBits);
			#endif
		} else {
			#if CORTAO_DEBUG_HOOKS
				cortao_debugCulledOccluder(i, viewPosition + dPos);
			#endif
		}

		
	}
	
	return rayBits;
}




void cortao_constructSamplingBasis(cortao_float3 normal, out cortao_float3 tangent, out cortao_float3 bitangent) {
	
	//Frisvad's arbitrary orthonormal tangent basis construction.
	cortao_float a = cortao_float(1.0) / (cortao_float(1.0) + normal.z);
	cortao_float b = normal.x * normal.y * a;
	tangent = cortao_float3(cortao_fma(normal.x * normal.x,  a, cortao_float(-1.0)), b, normal.x);
	bitangent = cortao_float3(b, cortao_fma(normal.y * normal.y, a, cortao_float(-1.0)), normal.y);
}

void cortao_constructTangentBasis(cortao_half3 normal, out cortao_half3 tangent, out cortao_half3 bitangent) {
	//Also, frisvad's arbitrary orthonormal tangent basis construction, but copy-pasted and slightly tweaked.
	cortao_half a = cortao_half(1.0) / (cortao_half(1.0) + normal.z);
	cortao_half b = normal.x * normal.y * a;
	tangent = cortao_half3(cortao_fma(normal.x * normal.x,  a, cortao_half(-1.0)), b, normal.x);
	bitangent = cortao_half3(b, cortao_fma(normal.y * normal.y, a, cortao_half(-1.0)), normal.y);
}


cortao_half cortao_resolveOcclusion(cortao_uint rayBits) {
	//Calculate occlusion as 1.0 - occluded rays / total rays
	cortao_uint numBits = cortao_bitCount(rayBits);
	return cortao_fma(cortao_half(numBits), cortao_half(-1.0) / cortao_half(CORTAO_RAY_COUNT), cortao_half(1.0)); //1.0 - numBits/CORTAO_RAY_COUNT;
}






cortao_half3 cortao_resolveBentNormal(cortao_uint rayBits) {
	
	#if CORTAO_BENT_NORMAL_LOOKUP_TABLE
		return cortao_loadBentNormalLookupTable(rayBits);
		
	#else
		//Calculate the bent normal as the sum of all unoccluded rays.
		cortao_half3 bentNormal = cortao_half3(0, 0, 0);
		cortao_unroll for(cortao_uint i = 0; i < CORTAO_RAY_COUNT; i++) {
			
			cortao_half3 rayDirection = cortao_rayDirection(i);
			
			bentNormal += rayDirection * ((rayBits & (1u << i)) == 0u ? cortao_half(1.0) : cortao_half(0.0));
		}
		return normalize(bentNormal); //Normalize the bent normal in the end.
	#endif
}

void cortao_mainPass(cortao_uint2 pixelCoords, CORTAOMainPassParameters params) {
	
	//check if our pixel is out-of-bounds and early out in that case
	cortao_uint2 resolution = cortao_getResolution(params.packedResolution);
	if(pixelCoords.x >= resolution.x || pixelCoords.y >= resolution.y) {
		return;
	}
	
	cortao_float2 texCoords = (cortao_float2(pixelCoords) + 0.5) / cortao_float2(resolution);
	
	cortao_float linearDepth = cortao_unpackDepth(cortao_samplePackedData(texCoords, 0.0));
	if(linearDepth == 0.0) {
		//This is a skybox pixel, ignore it.
		return;
	}
	
	cortao_half3 normal = cortao_loadNormal(cortao_int2(pixelCoords));

	cortao_float3 viewPosition = cortao_unproject(params.unprojectParams, texCoords, linearDepth);

	
	//Setup a set of random variables used during the setup code
	cortao_uint2 random = cortao_hash(cortao_seed(pixelCoords) + cortao_unpack16(params.randomSeed));
	
	cortao_float randomSampleRotation = cortao_float(random.x) * (cortao_float(CORTAO_PI2) / cortao_float(0xFFFFFFFFu));
	cortao_float offsetDither = cortao_float(random.y) * (1.0 / cortao_float(0xFFFFFFFFu));
	cortao_half randomTangentRotation = cortao_half(cortao_float(random.x ^ random.y ^ params.randomSeed) * (cortao_float(CORTAO_PI2) / cortao_float(0xFFFFFFFFu)));
	cortao_float2 sampleRotation = cortao_float2(sin(randomSampleRotation), cos(randomSampleRotation));

	
	#if CORTAO_PERSPECTIVE_PROJECTION
		
		//To enforce a maximum screenspace radius, we precompute the maximum radius as a linear function of depth.
		cortao_float clampedRadius = min(params.radius, linearDepth * params.maxApparentCircleRadiusCoefficient);
		
		//As we get closer to the sampling sphere, it starts to appear as a larger and larger surface due to the horizon.
		//This code calculates the required expanded circle radius to sample the entire sphere.
		cortao_float inverseCircleDistance = cortao_inverseSqrt(linearDepth*linearDepth - clampedRadius*clampedRadius);
		cortao_float apparentCircleRadius = clampedRadius * inverseCircleDistance;
		cortao_float circleRadius = apparentCircleRadius * linearDepth;

		cortao_float3 viewDirection = -normalize(viewPosition);

		//Precompute the constant portion of the mip bias. 
		//Due to our sample projection, the sample pattern becomes stretched closer to the edge of the screen. Increase the mip bias to compensate.
		cortao_float stretchMultiplier = cortao_inverseSqrt(viewDirection.z);
		cortao_float baseMipBias = log2(apparentCircleRadius * stretchMultiplier) + params.mipBias;

		//Push the center pixel's position out slightly in the direction of the normal to avoid self-shadowing.
		cortao_float pixelSize = params.normalBias * linearDepth;
		viewPosition = cortao_fma(cortao_float3(normal), cortao_float3(pixelSize, pixelSize, pixelSize), viewPosition);
		
		//Build a 2D samnple rotation matrix.
		//Instead of scaling the circle tangents later, we bake the radius scaling in here, as it's cheaper to precompute.
		cortao_float2 scaledRotation = sampleRotation * circleRadius;
		cortao_float3x3 rotationScaleMatrix = cortao_float3x3(
			scaledRotation.x, -scaledRotation.y, 0.0,
			scaledRotation.y,  scaledRotation.x, 0.0,
			0.0, 0.0, 1.0
		);
		
		//Construct the 3D sampling circle's tangents.
		cortao_float3 right;
		cortao_float3 up;
		cortao_constructSamplingBasis(viewDirection, right, up);

		//Randomly flip the right vector.
		//This randomly switches between a clockwise and counter-clockwise sample pattern spiral, which cancels out some errors.
		if((random.x & 1u) == 0u) 
		{
			right = -right;
		}
		
		//Construct a 3x3 matrix that takes 2D sample positions and transforms them onto the 3D circle.
		cortao_float3x3 circleMatrix = cortao_float3x3(
			right, 
			up, 
			viewPosition
		);
		
		//A simplified, truncated projection matrix.
		//Only supports FOV and off-center projection; nothing else:
		//The NDC --> texture space transformation is baked into these constants on the CPU side.
		cortao_float3x3 projectionMatrix = cortao_float3x3(
			params.projectionParams.x, 0.0, 0.0,
			0.0, params.projectionParams.y, 0.0,
			params.projectionParams.zw, -1.0
		);

		//Multiply the 3 matrices together
		cortao_float3x3 offsetTransform = cortao_mul(projectionMatrix, cortao_mul(circleMatrix, rotationScaleMatrix));
		
		
	#else


		//In ortho projections, many parameters end up as constants.
		cortao_float clampedRadius = params.radius;
		cortao_half3 viewDirection = cortao_half3(0, 0, 1);
		cortao_float baseMipBias = params.mipBias;
	
		//Push the center pixel's position out slightly in the direction of the normal to avoid self-shadowing.
		cortao_float pixelSize = params.normalBias;
		viewPosition = cortao_fma(cortao_float3(normal), cortao_float3(pixelSize, pixelSize, pixelSize), viewPosition);
		
		//Build the simplified orthographic sample transformation.
		//This is really just a 3x2 matrix, but use a 3x3 matrix to match the perspective codepath.
		//Assume that the compiler can optimize away the division by 1.0 in the sample loop.
		cortao_float3x3 rotationScaleMatrix = cortao_float3x3(
			sampleRotation.x, -sampleRotation.y, 0.0,
			sampleRotation.y,  sampleRotation.x, 0.0,
			0.0, 0.0, 1.0
		);
		
		cortao_float3x3 projectionMatrix = cortao_float3x3(
				clampedRadius * params.projectionParams.x, 0.0, 0.0,
				0.0, clampedRadius * params.projectionParams.y, 0.0,
				texCoords, 1.0
		);
		
		cortao_float3x3 offsetTransform = cortao_mul(projectionMatrix, rotationScaleMatrix);
	#endif
	
	
	//Generate a tangent basis for the ray directions
	cortao_half3 t;
	cortao_half3 bt;
	cortao_constructTangentBasis(normal, t, bt);
#if 1
	//Randomly rotate the tangent and bitangent around the normal
	cortao_half s = sin(randomTangentRotation);
	cortao_half c = cos(randomTangentRotation);
	cortao_half3 tangent = c * t + s * bt;
	cortao_half3 bitangent = c * bt - s * t;
#else
	//It's cheaper to not do the rotation, but this is only acceptable with many rays and few samples, so disabled for now.
	cortao_half3 tangent = t;
	cortao_half3 bitangent = bt;
#endif
	cortao_half3x3 tangentSpaceTransform = cortao_half3x3(tangent, bitangent, normal);
	

	#if CORTAO_DEBUG_HOOKS
		cortao_debugCenterPixel(viewPosition, normal, clampedRadius);
	#endif
	
	
	
	cortao_uint rayBits = cortao_computeRayOcclusion(offsetDither, offsetTransform, tangentSpaceTransform, baseMipBias, viewPosition, normal, cortao_half3(viewDirection), clampedRadius, params);

	
	cortao_half occlusion = cortao_resolveOcclusion(rayBits);
	cortao_half3 bentNormal = cortao_mul(tangentSpaceTransform, cortao_resolveBentNormal(rayBits)); //Transform tangent space bent normal back to view space.
	
	cortao_writeResult(cortao_int2(pixelCoords), occlusion, bentNormal, tangentSpaceTransform, rayBits);
}
