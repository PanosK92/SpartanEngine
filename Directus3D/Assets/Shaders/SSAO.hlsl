static const float strength = 5.0f;
static const float2 offset  = float2(resolution.x / 64.0f, resolution.y / 64.0f);
static const float falloff  = 0.0000000f;
static const float radius = 0.007f;
static const float discardDistance = 0.0001f;

#define NUM_SAMPLES	 16
static const float invSamples = 1.0f / (float)NUM_SAMPLES;

// AO sampling directions 
static const float3 AO_SAMPLES[26] = 
{
	float3(0.2196607,0.9032637,0.2254677),
	float3(0.05916681,0.2201506,-0.1430302),
	float3(-0.4152246,0.1320857,0.7036734),
	float3(-0.3790807,0.1454145,0.100605),
	float3(0.3149606,-0.1294581,0.7044517),
	float3(-0.1108412,0.2162839,0.1336278),
	float3(0.658012,-0.4395972,-0.2919373),
	float3(0.5377914,0.3112189,0.426864),
	float3(-0.2752537,0.07625949,-0.1273409),
	float3(-0.1915639,-0.4973421,-0.3129629),
	float3(-0.2634767,0.5277923,-0.1107446),
	float3(0.8242752,0.02434147,0.06049098),
	float3(0.06262707,-0.2128643,-0.03671562),
	float3(-0.1795662,-0.3543862,0.07924347),
	float3(0.06039629,0.24629,0.4501176),
	float3(-0.7786345,-0.3814852,-0.2391262),
	float3(0.2792919,0.2487278,-0.05185341),
	float3(0.1841383,0.1696993,-0.8936281),
	float3(-0.3479781,0.4725766,-0.719685),
	float3(-0.1365018,-0.2513416,0.470937),
	float3(0.1280388,-0.563242,0.3419276),
	float3(-0.4800232,-0.1899473,0.2398808),
	float3(0.6389147,0.1191014,-0.5271206),
	float3(0.1932822,-0.3692099,-0.6060588),
	float3(-0.3465451,-0.1654651,-0.6746758),
	float3(0.2448421,-0.1610962,0.1289366),
};

// Returns linear depth
float GetDepth(float2 uv)
{
	return texDepth.Sample(samplerPoint, uv).g;
}

// Returns normal
float3 GetNormal(float2 uv)
{
	float3 normal = texNormal.Sample(samplerAniso, uv);
	return normalize(UnpackNormal(normal));
}

// Returns a random normal
float3 GetRandomNormal(float2 uv)
{
	float3 randNormal = texNoise.Sample(samplerAniso, uv * offset);
	return normalize(UnpackNormal(randNormal));
}

float SSAO(float2 texCoord)
{
	float3 randNormal = GetRandomNormal(texCoord);
    float3 normal = GetNormal(texCoord);
    float  depth  = GetDepth(texCoord);
	float radius_depth = radius / depth;

	float occlusion = 0.0f;
    for( int i = 0; i < NUM_SAMPLES; ++i )
    {
		float3 ray = radius_depth * reflect(AO_SAMPLES[i], randNormal);
		float2 sampleTexCoords = texCoord + sign(dot(ray, normal)) * ray.xy;

        float sampledDepth = GetDepth(sampleTexCoords);
		float3 sampledNormal = GetNormal(sampleTexCoords);
		float depthDiff = depth - sampledDepth;
			
		float rangeCheck = smoothstep(0.0f, 1.0f, discardDistance / abs(depthDiff));
        occlusion += step(falloff, depthDiff) * (1.0 - dot(sampledNormal, normal)) * 
              (1.0f - smoothstep(falloff, strength, depthDiff)) * rangeCheck;
    }

    occlusion = 1.0f - (occlusion / NUM_SAMPLES);
	occlusion = clamp(pow(occlusion, strength), 0.0f, 1.0f);
	
	return occlusion;
}