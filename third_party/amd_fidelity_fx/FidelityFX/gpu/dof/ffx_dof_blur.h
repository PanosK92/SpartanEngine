// This file is part of the FidelityFX SDK.
//
// Copyright (C)2023 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files(the “Software”), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in 
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
// THE SOFTWARE.

#include "ffx_core.h"
#include "ffx_dof_common.h"

// Factor applied to a distance value before checking that it is in range of the blur kernel.
FFX_STATIC const FfxFloat32 FFX_DOF_RANGE_TOLERANCE_FACTOR = 0.98;

// Accumulators for one ring. Used for ring occlusion.
struct FfxDofBucket
{
	FfxFloat32x4 color; // rgb=color sum, a=weight sum
	FfxFloat32 ringCovg; // radius of the ring coverage (average of tileCoc/coc with some clamping)
	FfxFloat32 radius; // radius of the ring center
	FfxUInt32 sampleCount; // number of samples counted
};

// One sample of the input and related variables
struct FfxDofSample
{
	FfxFloat32 coc; // signed circle of confusion in pixels. negative values are far-field.
	FfxBoolean isNear; // whether the sample is in the near-field (coc > 0)
	FfxFloat32x3 color; // color value of the sample
};

// Helper struct to contain all input variables
struct FfxDofInputState
{
	FfxUInt32x2 imageSize; // input pixel size (half res)
	FfxFloat32x2 pxCoord; // pixel coordinates of the kernel center
	FfxFloat32 tileCoc; // coc value bilinearly interpolated from the tile map
	FfxFloat32 centerCoc; // signed coc value at the kernel center
	FfxUInt32 mipLevel; // mip level to use based on coc and MAX_RINGS
	FfxBoolean nearField; // whether the center pixel is in the near field
	FfxUInt32 nSamples, // number of actual samples taken
		nRings; // number of rings to sample (<= MAX_RINGS)
};

// Helper struct to contain accumulation variables
struct FfxDofAccumulators
{
	FfxDofBucket prevBucket, currBucket;
	FfxFloat32x4 nearColor, fillColor;
	FfxFloat32 fillHits;
};

// Merges currBucket into prevBucket. Opacity is ratio of hit/total samples in current ring.
void FfxDofMergeBuckets(inout FfxDofAccumulators acc, FfxFloat32 opacity)
{
	// averaging
	FfxFloat32 prevRC = ffxSaturate(acc.prevBucket.ringCovg / acc.prevBucket.sampleCount);
	FfxFloat32 currRC = ffxSaturate(acc.currBucket.ringCovg / acc.currBucket.sampleCount);

	// occlusion term is calculated as the ratio of the area of intersection of both buckets
	// (being viewed as rings with a radius (centered on the samples) and ring width (=avg coverage))
	// divided by the area of the previous bucket ring.
	FfxFloat32 prevOuter = ffxSaturate(acc.prevBucket.radius + prevRC);
	FfxFloat32 prevInner = (acc.prevBucket.radius - prevRC);
	FfxFloat32 currOuter = ffxSaturate(acc.currBucket.radius + currRC);
	FfxFloat32 currInner = (acc.currBucket.radius - currRC);
	// intersection is between min(outer) and max(inner)
	FfxFloat32 insOuter = min(prevOuter, currOuter);
	FfxFloat32 insInner = max(prevInner, currInner);
	// intersection area formula.
	// ffxSaturate here fixes edge case where prev area = 0 -> ffxSaturate(0/0)=ffxSaturate(nan) = 0
	// The value does not matter in that case, since the previous values will be all zero, but it must be finite.
	FfxFloat32 occlusion = insOuter < insInner ? 0 : ffxSaturate((insOuter * insOuter - sign(insInner) * insInner * insInner) / (prevOuter * prevOuter - sign(prevInner) * prevInner * prevInner));

	FfxFloat32 factor = 1 - opacity * occlusion;
	acc.prevBucket.color = acc.prevBucket.color * factor + acc.currBucket.color;
	// select new radius so that (roughly) covers both rings, so in the middle of the combined ring
	FfxFloat32 newRadius = 0.5 * (max(prevOuter, currOuter) + min(prevInner, currInner));
	// the new coverage should then be the difference between the radius and either bound
	FfxFloat32 newCovg = 0.5 * (max(prevOuter, currOuter) - min(prevInner, currInner));
	acc.prevBucket.sampleCount = FfxUInt32(acc.prevBucket.sampleCount * factor) + acc.currBucket.sampleCount;
	acc.prevBucket.ringCovg = acc.prevBucket.sampleCount * newCovg;
	acc.prevBucket.radius = newRadius;
}

FfxFloat32 FfxDofWeight(FfxDofInputState ins, FfxFloat32 coc)
{
	// weight assigned needs to account for energy conservation.
	// If light is spread over a circle of radius coc, then the contribution to this pixel
	// must be weighted with the inverse area of the circle. BUT we cannot simply divide by the
	// area since in-focus samples have coc=0, so clamp the weight to [0;1]. In effect, this means
	// if the sample projects an area less than a pixel in size, all of its energy lands on this pixel.
	// We also normalize to tileCoc and sample count to improve quality of near-field edges and
	// edges during smooth focus transitions. Dividing by the radius (and not its square) is slightly
	// faster without looking wrong, along with a factor 2 multiplication.
	return ffxSaturate(2 * rcp(ins.nSamples) * ins.tileCoc / coc);
}

FfxFloat32 FfxDofCoverage(FfxDofInputState ins, FfxFloat32 coc)
{
	// Coverage is essentially the radius of the sample's projection to the lens aperture.
	// The radius is normalized to the tile CoC and kernel diameter in samples.
	// Add a small bias to account for gaps between sample rings.
	// Clamped to avoid infinity near zero.
	return ffxSaturate(rcp(2 * ins.nRings) * (ins.tileCoc / coc) + rcp(2 * ins.nRings));
}

#ifdef FFX_DOF_CUSTOM_SAMPLES

// declarations only

/// Optional callback for custom blur kernels. Gets the sample offset.
/// @param n Number of samples in the current ring, as returned by FfxDofGetRingSampleCount and divided by the number of merged rings.
/// @param i Index of the sample within the ring
/// @param r Radius of the current ring
/// @return The sample offset. The default implementation returns an approximation of r * sin and cos of (2pi * i/n).
/// @ingroup FfxGPUDof
FfxFloat32x2 FfxDofGetSampleOffset(FfxUInt32 n, FfxUInt32 i, FfxFloat32 r);
/// Optional callback for custom blur kernels. Gets the number of samples in a ring.
/// @param ins Input parameters for the blur kernel. Contains the full number of rings.
/// @param ri Index of the current ring. If rings are being merged, this is the center of the indices.
/// @param mergeRingsCount The number of rings being merged. 1 if the current ring is not merged with any other.
/// @return The number of samples in the ring, assuming no merging. This is divided by the mergeRingsCount to get the actual number of samples.
/// @ingroup FfxGPUDof
FfxUInt32 FfxDofGetRingSampleCount(FfxDofInputState ins, FfxFloat32 ri, FfxUInt32 mergeRingsCount);

#else

FFX_STATIC FfxFloat32 FfxDof_costheta2;
FFX_STATIC FfxFloat32x2 FfxDof_sincos_1, FfxDof_sincos_2;

FfxFloat32x2 FfxDofGetSampleOffset(FfxUInt32 n, FfxUInt32 i, FfxFloat32 r)
{
	// Chebyshev method to compute cos and sin of (2pi * i / n)
	FfxFloat32x2 xy = FfxDof_costheta2 * FfxDof_sincos_1 - FfxDof_sincos_2;
	FfxDof_sincos_2 = FfxDof_sincos_1;
	FfxDof_sincos_1 = xy;

	return r * FfxFloat32x2(xy);
}

FfxUInt32 FfxDofGetRingSampleCount(FfxDofInputState ins, FfxFloat32 ri, FfxUInt32 merge)
{
	FfxUInt32 n = FfxUInt32(6.25 * (ins.nRings - ri)); // approx. pi/asin(1/(2*(nR-ri)))
	FfxFloat32 theta = 6.2831853 * rcp(n) * merge;
	// Read first lane to mark this explicitly as scalar.
#if FFX_HLSL
	FfxDof_costheta2 = WaveReadLaneFirst(cos(theta) * 2.0);
#elif FFX_GLSL
	FfxDof_costheta2 = subgroupBroadcastFirst(cos(theta) * 2.0);
#endif
	FfxDof_sincos_1 = FfxFloat32x2(1, 0);
	FfxDof_sincos_2 = FfxFloat32x2(cos(theta), sin(-theta));
	return n;
}

#endif

FfxDofSample FfxDofFetchSample(FfxDofInputState ins, FfxFloat32 ring, FfxUInt32 ringSamples, FfxUInt32 i, FfxUInt32 mipBias)
{
	FfxDofSample result;
	FfxFloat32 rad = ins.tileCoc * rcp(ins.nRings) * FfxFloat32(ins.nRings - ring);
	FfxFloat32x2 sampleOffset = FfxDofGetSampleOffset(ringSamples, i, rad);
	FfxFloat32x2 samplePos = ins.pxCoord + sampleOffset;
	FfxUInt32 mipLevel = ins.mipLevel + mipBias;
	FfxFloat32x4 texval = FfxDofSampleInput(samplePos, mipLevel);
	result.coc = texval.a;
	result.color = texval.rgb;
	result.isNear = result.coc > 0;
	return result;
}

void FfxDofProcessNearSample(FfxDofInputState ins, FfxDofSample s, inout FfxDofAccumulators acc, FfxFloat32 sampleDist)
{
	FfxFloat32 absCoc = abs(s.coc);
	FfxBoolean nearInRange = s.coc >= FFX_DOF_RANGE_TOLERANCE_FACTOR * sampleDist;
	FfxFloat32 nearWeight = FfxDofWeight(ins, absCoc);

	// smooth out the fill hits; the closer to the camera, the less it counts
	acc.fillHits += ffxSaturate(1.0 - s.coc);
	// any sample further away than the center sample (plus one pixel) counts towards filling in the background
	if (ins.nearField && s.coc < ins.centerCoc - 1)
	{
		acc.fillColor += FfxFloat32x4(s.color, 1) * nearWeight;
	}
	acc.nearColor += FfxFloat32x4(s.color, 1) * nearWeight * FfxFloat32(nearInRange);
}

void FfxDofProcessFarSample(FfxDofInputState ins, FfxDofSample s, inout FfxDofAccumulators acc, FfxFloat32 sampleDist, FfxFloat32 ringBorder)
{
	FfxFloat32 clampedFarCoc = max(0, -s.coc);
	FfxBoolean farInRange = -s.coc >= FFX_DOF_RANGE_TOLERANCE_FACTOR * sampleDist;

	FfxFloat32 covg = FfxDofCoverage(ins, clampedFarCoc);
	FfxFloat32 weight = FfxDofWeight(ins, abs(s.coc));

	FfxFloat32x4 color = FfxFloat32x4(s.color, 1) * weight * FfxFloat32(farInRange);

	if (-s.coc >= ringBorder)
	{
		acc.prevBucket.ringCovg += covg;
		acc.prevBucket.color += color;
		acc.prevBucket.sampleCount++;
	}
	else
	{
		acc.currBucket.ringCovg += covg;
		acc.currBucket.color += color;
		acc.currBucket.sampleCount++;
	}
}

void FfxDofProcessNearFar(FfxDofInputState ins, inout FfxDofAccumulators acc)
{
	// base case: both near and far-field are processed.
	// scan outside-in for far-field occlusion
	for (FfxUInt32 ri = 0; ri < ins.nRings; ri++)
	{
		acc.currBucket.color = FFX_BROADCAST_FLOAT32X4(0);
		acc.currBucket.ringCovg = 0;
		acc.currBucket.radius = rcp(ins.nRings) * FfxFloat32(ins.nRings - ri);
		acc.currBucket.sampleCount = 0;

		FfxUInt32 ringSamples = FfxDofGetRingSampleCount(ins, ri, 1);
		FfxFloat32 ringBorder = (ins.nRings - 1 - ri + 2.5) * ins.tileCoc * rcp(0.5 + ins.nRings);
		FfxFloat32 sampleDist = ins.tileCoc * rcp(ins.nRings) * FfxFloat32(ins.nRings - ri);

		// partially unrolled loop
		const FfxUInt32 UNROLL_CNT = 4;
		FfxUInt32 iunr = 0;
		for (iunr = 0; iunr + UNROLL_CNT <= ringSamples; iunr += UNROLL_CNT)
		{
			FFX_DOF_UNROLL
			for (FfxUInt32 i = 0; i < UNROLL_CNT; i++)
			{
				FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, iunr + i, 0);
				FfxDofProcessFarSample(ins, s, acc, sampleDist, ringBorder);
				FfxDofProcessNearSample(ins, s, acc, sampleDist);
			}
		}
		for (FfxUInt32 i = iunr; i < ringSamples; i++)
		{
			FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, i, 0);
			FfxDofProcessFarSample(ins, s, acc, sampleDist, ringBorder);
			FfxDofProcessNearSample(ins, s, acc, sampleDist);
		}

		FfxFloat32 opacity = FfxFloat32(acc.currBucket.sampleCount) / FfxFloat32(ringSamples);
		FfxDofMergeBuckets(acc, opacity);
	}
}

void FfxDofProcessNearOnly(FfxDofInputState ins, inout FfxDofAccumulators acc)
{
	// variant with the assumption that all samples are near field
	for (FfxUInt32 ri = 0; ri < ins.nRings; ri++)
	{
		FfxUInt32 ringSamples = FfxDofGetRingSampleCount(ins, ri, 1);
		FfxFloat32 sampleDist = ins.tileCoc * rcp(ins.nRings) * FfxFloat32(ins.nRings - ri);

		// partially unrolled loop
		const FfxUInt32 UNROLL_CNT = 4;
		FfxUInt32 iunr = 0;
		for (iunr = 0; iunr + UNROLL_CNT <= ringSamples; iunr += UNROLL_CNT)
		{
			FFX_DOF_UNROLL
			for (FfxUInt32 i = 0; i < UNROLL_CNT; i++)
			{
				FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, iunr + i, 0);
				FfxDofProcessNearSample(ins, s, acc, sampleDist);
			}
		}
		for (FfxUInt32 i = iunr; i < ringSamples; i++)
		{
			FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, i, 0);
			FfxDofProcessNearSample(ins, s, acc, sampleDist);
		}
	}
}

void FfxDofProcessFarOnly(FfxDofInputState ins, inout FfxDofAccumulators acc)
{
	// variant with the assumption that all samples are far field
	// scan outside-in for far-field occlusion
	for (FfxUInt32 ri = 0; ri < ins.nRings; ri++)
	{
		acc.currBucket.color = FFX_BROADCAST_FLOAT32X4(0);
		acc.currBucket.ringCovg = 0;
		acc.currBucket.radius = rcp(ins.nRings) * FfxFloat32(ins.nRings - ri);
		acc.currBucket.sampleCount = 0;

		FfxUInt32 ringSamples = FfxDofGetRingSampleCount(ins, ri, 1);
		FfxFloat32 ringBorder = (ins.nRings - 1 - ri + 2.5) * ins.tileCoc * rcp(0.5 + ins.nRings);
		FfxFloat32 sampleDist = ins.tileCoc * rcp(ins.nRings) * (ins.nRings - ri);

		// partially unrolled loop
		const FfxUInt32 UNROLL_CNT = 4;
		FfxUInt32 iunr = 0;
		for (iunr = 0; iunr + UNROLL_CNT <= ringSamples; iunr += UNROLL_CNT)
		{
			FFX_DOF_UNROLL
			for (FfxUInt32 i = 0; i < UNROLL_CNT; i++)
			{
				FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, iunr + i, 0);
				FfxDofProcessFarSample(ins, s, acc, sampleDist, ringBorder);
			}
		}
		for (FfxUInt32 i = iunr; i < ringSamples; i++)
		{
			FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, i, 0);
			FfxDofProcessFarSample(ins, s, acc, sampleDist, ringBorder);
		}

		FfxFloat32 opacity = FfxFloat32(acc.currBucket.sampleCount) / FfxFloat32(ringSamples);
		FfxDofMergeBuckets(acc, opacity);
	}
}

void FfxDofProcessNearColorOnly(FfxDofInputState ins, inout FfxDofAccumulators acc)
{
	// variant with the assumption that all samples are near and equally weighed
	for (FfxUInt32 ri = 0; ri < ins.nRings;)
	{
		// merge inner rings if possible
		FfxUInt32 merge = min(min(1 << ri, FFX_DOF_MAX_RING_MERGE), ins.nRings - ri);
		FfxFloat32 rif = ri + 0.5 * merge - 0.5;
		FfxUInt32 weight = merge * merge;
		FfxUInt32 ringSamples = FfxDofGetRingSampleCount(ins, rif, merge) / merge;
		FfxUInt32 mipBias = 2 * FfxUInt32(log2(merge));
		FfxFloat32 sampleDist = ins.tileCoc * rcp(ins.nRings) * (FfxFloat32(ins.nRings) - rif);
		FfxHalfOpt distThresh = FfxHalfOpt(FFX_DOF_RANGE_TOLERANCE_FACTOR * sampleDist);

		FfxHalfOpt3 nearColorAcc = FfxHalfOpt3(0, 0, 0);
        FfxUInt32 hitCount = 0;
		// We presume that all samples are in range
		// partially unrolled loop (x6)
		const FfxUInt32 UNROLL_CNT = 6;
		FfxUInt32 iunr = 0;
		for (iunr = 0; iunr + UNROLL_CNT <= ringSamples; iunr += UNROLL_CNT)
		{
			FFX_DOF_UNROLL
			for (FfxUInt32 i = 0; i < UNROLL_CNT; i++)
			{
				FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, iunr + i, mipBias);
				FfxBoolean inRange = FfxHalfOpt(s.coc) >= distThresh;
				nearColorAcc += FfxHalfOpt(inRange) * FfxHalfOpt3(s.color);
				hitCount += FfxUInt32(inRange);
			}
		}
		for (FfxUInt32 i = iunr; i < ringSamples; i++)
		{
			FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, i, mipBias);
			FfxBoolean inRange = FfxHalfOpt(s.coc) >= distThresh;
			nearColorAcc += FfxHalfOpt(inRange) * FfxHalfOpt3(s.color);
			hitCount += FfxUInt32(inRange);
		}
		acc.nearColor.rgb += nearColorAcc * FfxHalfOpt(weight);
		acc.nearColor.a += FfxHalfOpt(hitCount) * FfxHalfOpt(weight);
		ri += merge;
	}
}

void FfxDofProcessFarColorOnly(FfxDofInputState ins, inout FfxDofAccumulators acc)
{
	FfxFloat32 nSamples = 0;
	for (FfxUInt32 ri = 0; ri < ins.nRings; )
	{
		// merge inner rings if possible
		FfxUInt32 merge = min(min(1 << ri, FFX_DOF_MAX_RING_MERGE), ins.nRings - ri);
		FfxFloat32 rif = ri + 0.5 * merge - 0.5;
		FfxUInt32 weight = merge * merge;
		FfxUInt32 ringSamples = FfxDofGetRingSampleCount(ins, rif, merge) / merge;
		FfxUInt32 mipBias = 2 * FfxUInt32(log2(merge));
		FfxFloat32 sampleDist = ins.tileCoc * rcp(ins.nRings) * (FfxFloat32(ins.nRings) - rif);
		FfxHalfOpt distThresh = -FfxHalfOpt(FFX_DOF_RANGE_TOLERANCE_FACTOR * sampleDist);

		FfxHalfOpt3 colorAcc = FfxHalfOpt3(0, 0, 0);
		FfxUInt32 hitCount = 0;
		// partially unrolled loop (x12, then x6, then x3)
		FfxUInt32 iunr = 0;
		for (; iunr + 12 <= ringSamples; iunr += 12)
		{
			FFX_DOF_UNROLL
			for (FfxUInt32 i = 0; i < 12; i++)
			{
				FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, iunr + i, mipBias);
				FfxBoolean inRange = FfxHalfOpt(s.coc) <= distThresh;
				colorAcc += FfxHalfOpt(inRange) * FfxHalfOpt3(s.color);
				hitCount += FfxUInt32(inRange);
			}
		}
		for (; iunr + 6 <= ringSamples; iunr += 6)
		{
			FFX_DOF_UNROLL
			for (FfxUInt32 i = 0; i < 6; i++)
			{
				FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, iunr + i, mipBias);
				FfxBoolean inRange = FfxHalfOpt(s.coc) <= distThresh;
				colorAcc += FfxHalfOpt(inRange) * FfxHalfOpt3(s.color);
				hitCount += FfxUInt32(inRange);
			}
		}
		for (; iunr + 3 <= ringSamples; iunr += 3)
		{
			FFX_DOF_UNROLL
			for (FfxUInt32 i = 0; i < 3; i++)
			{
				FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, iunr + i, mipBias);
				FfxBoolean inRange = FfxHalfOpt(s.coc) <= distThresh;
				colorAcc += FfxHalfOpt(inRange) * FfxHalfOpt3(s.color);
				hitCount += FfxUInt32(inRange);
			}
		}
		for (FfxUInt32 i = iunr; i < ringSamples; i++)
		{
			FfxDofSample s = FfxDofFetchSample(ins, ri, ringSamples, iunr + i, mipBias);
			FfxBoolean inRange = FfxHalfOpt(s.coc) <= distThresh;
			colorAcc += FfxHalfOpt(inRange) * FfxHalfOpt3(s.color);
			hitCount += FfxUInt32(inRange);
		}
		acc.prevBucket.color.rgb += colorAcc * FfxHalfOpt(weight);
		nSamples += hitCount * weight;
		ri += merge;
	}
	acc.prevBucket.color.a = nSamples;
	acc.prevBucket.ringCovg = nSamples;
	acc.prevBucket.sampleCount = FfxUInt32(nSamples);
}

// prepare values for the tile. Return classification.
FfxUInt32 FfxDofPrepareTile(FfxUInt32x2 id, out FfxDofInputState ins)
{
	FfxFloat32x2 dilatedCocSigned = FfxDofSampleDilatedRadius(id);
	FfxFloat32x2 dilatedCoc = abs(dilatedCocSigned);
	FfxFloat32 tileRad = max(dilatedCoc.x, dilatedCoc.y);

	// check if copying the tile is good enough
#if FFX_HLSL
	if (WaveActiveAllTrue(tileRad < 0.5)) return 0;
#elif FFX_GLSL
	if (subgroupAll(tileRad < 0.5)) return 0;
#endif

	FfxFloat32 idealRingCount = // kernel radius in pixels -> one sample per pixel
#if FFX_HLSL
		WaveActiveMax(ceil(tileRad)); 
#elif FFX_GLSL
		subgroupMax(ceil(tileRad));
#endif
	ins.nRings = FfxUInt32(idealRingCount);
	ins.mipLevel = 0;
	if (idealRingCount > MaxRings())
	{
		ins.nRings = MaxRings();
		// use a higher mip to cover the missing rings.
		// for every factor 2 decrease of the rings, increase mips by 1.
		ins.mipLevel = FfxUInt32(log2(idealRingCount * rcp(MaxRings())));
	}
	ins.tileCoc = tileRad;

	FfxFloat32x2 texcoord = FfxFloat32x2(id) + FfxFloat32x2(0.25, 0.25); // shift to center of top-left pixel in quad
	// Add noise to reduce banding (if too noisy, this could be disabled)
	{
		/* hash22 adapted from https://www.shadertoy.com/view/4djSRW
		Copyright (c)2014 David Hoskins.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/
		FfxFloat32x3 p3 = ffxFract(texcoord.xyx * FfxFloat32x3(0.1031, 0.1030, 0.0973));
		p3 += dot(p3, p3.yzx + 33.33);
		texcoord += ffxFract((p3.xx+p3.yz)*p3.zy) * 0.5 - 0.25;
	}
	ins.pxCoord = texcoord;

	FfxFloat32 centerCoc = FfxDofLoadInput(id).a;
	ins.nearField = centerCoc > 1;
	ins.centerCoc = abs(centerCoc);

	ins.nSamples = 0;
#ifdef FFX_DOF_CUSTOM_SAMPLES
	for (FfxUInt32 ri = 0; ri < ins.nRings; ri++)
	{
		ins.nSamples += FfxDofGetRingSampleCount(ins, ri, 1);
	}
#else
	// due to rounding this will likely over-approximate, but that should be okay.
	ins.nSamples = FfxUInt32(6.25 * 0.5 * (ins.nRings * (ins.nRings + 1)));
#endif

#if FFX_HLSL
	// simple check: is there even any near/far that we would sample?
	// See ProcessNearSample: No relevant code is executed if the center is not near and no sample is near
	// (use the CoC of the dilated tile minimum depth as the proxy for any sample)
	FfxBoolean waveNeedsNear = WaveActiveAnyTrue(dilatedCocSigned.x > -1);
	// See ProcessFarSample: All weights will be 0 if no sample is in the far field.
	// (use the CoC of dilated tile max depth as proxy)
	FfxBoolean waveNeedsFar = WaveActiveAnyTrue(dilatedCocSigned.y < 1);

	FfxBoolean waveColorOnly = WaveActiveAllTrue(dilatedCocSigned.x - dilatedCocSigned.y < 0.5);
#elif FFX_GLSL
	// as above
	FfxBoolean waveNeedsNear = subgroupAny(dilatedCocSigned.x > -1);
	FfxBoolean waveNeedsFar = subgroupAny(dilatedCocSigned.y < 1);
	FfxBoolean waveColorOnly = subgroupAll(dilatedCocSigned.x - dilatedCocSigned.y < 0.5);
#endif

	return FfxUInt32(waveColorOnly) * 4 + FfxUInt32(waveNeedsNear) * 2 + FfxUInt32(waveNeedsFar);
}

/// Blur pass entry point. Runs in 8x8x1 thread groups and computes transient near and far outputs.
///
/// @param pixel Coordinate of the pixel (SV_DispatchThreadID)
/// @param halfImageSize Resolution of the source image (half resolution) in pixels
/// @ingroup FfxGPUDof
void FfxDofBlur(FfxUInt32x2 pixel, FfxUInt32x2 halfImageSize)
{
	FfxDofResetMaxTileRadius();

	FfxDofInputState ins;
	FfxUInt32 tileClass = FfxDofPrepareTile(pixel, ins);
	ins.imageSize = halfImageSize;

	// initialize accumulators
	FfxDofAccumulators acc;
	acc.prevBucket.color = FfxFloat32x4(0, 0, 0, 0);
	acc.prevBucket.ringCovg = 0;
	acc.prevBucket.radius = 0;
	acc.prevBucket.sampleCount = 0;

	FfxFloat32 centerWeight = FfxDofWeight(ins, ins.centerCoc);
	FfxFloat32 centerCovg = FfxDofCoverage(ins, ins.centerCoc);
	FfxFloat32x4 centerColor = FfxFloat32x4(FfxDofLoadInput(pixel).rgb, 1) * centerWeight;
	acc.nearColor = ins.nearField ? centerColor : FfxFloat32x4(0, 0, 0, 0);
	acc.fillColor = FfxFloat32x4(0, 0, 0, 0);
	acc.fillHits = 0;

	switch (tileClass)
	{
	case 0:
	case 4:
		FfxDofStoreFar(pixel, FfxHalfOpt4(FfxDofLoadInput(pixel).rgb, 1));
		FfxDofStoreNear(pixel, FfxHalfOpt4(0, 0, 0, 0));
		return;
	case 1:
		FfxDofProcessFarOnly(ins, acc); break;
	case 2:
		FfxDofProcessNearOnly(ins, acc); break;
	case 3:
	case 7:
		FfxDofProcessNearFar(ins, acc); break;
	case 5:
		FfxDofProcessFarColorOnly(ins, acc); break;
	case 6:
		FfxDofProcessNearColorOnly(ins, acc); break;
	}

	// process center
	acc.currBucket.ringCovg = ins.nearField ? 0 : centerCovg;
	acc.currBucket.color = ins.nearField ? FfxFloat32x4(0, 0, 0, 0) : centerColor;
	acc.currBucket.radius = 0;
	acc.currBucket.sampleCount = 1;
	FfxDofMergeBuckets(acc, 1);

	acc.prevBucket.color += acc.fillColor;
	FfxFloat32 fgOpacity = 1.0 - FfxFloat32(acc.fillHits) / ins.nSamples;
	fgOpacity *= sign(acc.nearColor.a);

	FfxFloat32x4 ffTarget = acc.prevBucket.color / acc.prevBucket.color.a;
	FfxFloat32x4 ffOutput = !any(isnan(ffTarget)) ? ffTarget : FfxFloat32x4(0, 0, 0, 0);
	FfxFloat32x3 nfTarget = acc.nearColor.rgb / acc.nearColor.a;
	FfxFloat32x4 nfOutput = FfxFloat32x4(!any(isnan(nfTarget)) ? nfTarget : FfxFloat32x3(0, 0, 0), ffxSaturate(fgOpacity));

	FfxDofStoreFar(pixel, FfxHalfOpt4(ffOutput));
	FfxDofStoreNear(pixel, FfxHalfOpt4(nfOutput));
}
