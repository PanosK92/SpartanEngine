// Hemisphere sample kernel generated according to the needs of the SSAO approach described here:
// http://john-chapman-graphics.blogspot.co.uk/2013/01/ssao-tutorial.html
static const float3 sampleKernel[64] = 
{
	float3(0.04977, -0.04471, 0.04996),
	float3(0.01457, 0.01653, 0.00224),
	float3(-0.04065, -0.01937, 0.03193),
	float3(0.01378, -0.09158, 0.04092),
	float3(0.05599, 0.05979, 0.05766),
	float3(0.09227, 0.04428, 0.01545),
	float3(-0.00204, -0.0544, 0.06674),
	float3(-0.00033, -0.00019, 0.00037),
	float3(0.05004, -0.04665, 0.02538),
	float3(0.03813, 0.0314, 0.03287),
	float3(-0.03188, 0.02046, 0.02251),
	float3(0.0557, -0.03697, 0.05449),
	float3(0.05737, -0.02254, 0.07554),
	float3(-0.01609, -0.00377, 0.05547),
	float3(-0.02503, -0.02483, 0.02495),
	float3(-0.03369, 0.02139, 0.0254),
	float3(-0.01753, 0.01439, 0.00535),
	float3(0.07336, 0.11205, 0.01101),
	float3(-0.04406, -0.09028, 0.08368),
	float3(-0.08328, -0.00168, 0.08499),
	float3(-0.01041, -0.03287, 0.01927),
	float3(0.00321, -0.00488, 0.00416),
	float3(-0.00738, -0.06583, 0.0674),
	float3(0.09414, -0.008, 0.14335),
	float3(0.07683, 0.12697, 0.107),
	float3(0.00039, 0.00045, 0.0003),
	float3(-0.10479, 0.06544, 0.10174),
	float3(-0.00445, -0.11964, 0.1619),
	float3(-0.07455, 0.03445, 0.22414),
	float3(-0.00276, 0.00308, 0.00292),
	float3(-0.10851, 0.14234, 0.16644),
	float3(0.04688, 0.10364, 0.05958),
	float3(0.13457, -0.02251, 0.13051),
	float3(-0.16449, -0.15564, 0.12454),
	float3(-0.18767, -0.20883, 0.05777),
	float3(-0.04372, 0.08693, 0.0748),
	float3(-0.00256, -0.002, 0.00407),
	float3(-0.0967, -0.18226, 0.29949),
	float3(-0.22577, 0.31606, 0.08916),
	float3(-0.02751, 0.28719, 0.31718),
	float3(0.20722, -0.27084, 0.11013),
	float3(0.0549, 0.10434, 0.32311),
	float3(-0.13086, 0.11929, 0.28022),
	float3(0.15404, -0.06537, 0.22984),
	float3(0.05294, -0.22787, 0.14848),
	float3(-0.18731, -0.04022, 0.01593),
	float3(0.14184, 0.04716, 0.13485),
	float3(-0.04427, 0.05562, 0.05586),
	float3(-0.02358, -0.08097, 0.21913),
	float3(-0.14215, 0.19807, 0.00519),
	float3(0.15865, 0.23046, 0.04372),
	float3(0.03004, 0.38183, 0.16383),
	float3(0.08301, -0.30966, 0.06741),
	float3(0.22695, -0.23535, 0.19367),
	float3(0.38129, 0.33204, 0.52949),
	float3(-0.55627, 0.29472, 0.3011),
	float3(0.42449, 0.00565, 0.11758),
	float3(0.3665, 0.00359, 0.0857),
	float3(0.32902, 0.0309, 0.1785),
	float3(-0.08294, 0.51285, 0.05656),
	float3(0.86736, -0.00273, 0.10014),
	float3(0.45574, -0.77201, 0.00384),
	float3(0.41729, -0.15485, 0.46251),
	float3(-0.44272, -0.67928, 0.1865)
};

static const float intensity = 8.0f;
static const int kernelSize = 4;
static const float radius = 0.2f;
static const float bias = 0.2f;
static const float2 noiseScale  = float2(resolution.x / 64.0f, resolution.y / 64.0f);

// Returns a random normal
float3 GetRandomNormal(float2 texCoord)
{
	float3 randNormal = texNoise.Sample(samplerAniso, texCoord * noiseScale).rgb;
	return normalize(UnpackNormal(randNormal));
}

float doAmbientOcclusion(float2 texCoord, float3 position, float3 normal)
{
	float3 originPos 	= position;
	float3 sampledPos 	= GetPositionWorldFromDepth(texDepth, samplerAniso, mViewProjectionInverse, texCoord);
	float3 diff 		= sampledPos - originPos;
	 
	float3 v = normalize(diff);
	float d = length(diff) * noiseScale;
	float occlusion = max(0.0f, dot(normal, v) - bias) * (1.0f / (1.0f + d));
    float rangeCheck = smoothstep(0.0f, 1.0f, radius / abs(originPos - sampledPos));
	
	return occlusion * rangeCheck;
}

float SSAO(float2 texCoord)
{
	float3 position 	= GetPositionWorldFromDepth(texDepth, samplerAniso, mViewProjectionInverse, texCoord);
	float3 randNormal 	= GetRandomNormal(texCoord);
    float3 normal 		= GetNormalUnpacked(texNormal, samplerAniso, texCoord);
	float originDepth 	= GetDepthLinear(texDepth, samplerAniso, nearPlane, farPlane, texCoord);
	float radius_depth 	= radius / originDepth;
	float occlusion 	= 0.0f;
	
	[unroll(kernelSize)]
    for( int i = 0; i < kernelSize; i++)
    {
		float2 coord1 = reflect(sampleKernel[i], randNormal) * radius_depth;
		float2 coord2 = float2(coord1.x - coord1.y, coord1.x + coord1.y);

		//if(dot(normal, randNormal) < 0.0f)
            //randNor *= -1.0;
			
		float acc = 0.0f;
		acc += doAmbientOcclusion(texCoord + coord1 * 0.25f, position, normal);
		acc += doAmbientOcclusion(texCoord + coord2 * 0.5f, position, normal);
		acc += doAmbientOcclusion(texCoord + coord1 * 0.75f, position, normal);
		acc += doAmbientOcclusion(texCoord + coord2, position, normal);
		
		occlusion += acc * intensity;
    }

    occlusion /= (float)kernelSize * 4.0f;
	occlusion = 1.0f - occlusion;
	
	return clamp(occlusion, 0.0f, 1.0f);
}