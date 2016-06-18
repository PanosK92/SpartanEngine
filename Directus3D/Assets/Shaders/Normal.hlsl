float3 UnpackNormal(float3 normal)
{
	return normal * 2.0f - 1.0f;
}

float3 PackNormal(float3 normal)
{
	return normal * 0.5f + 0.5f;
}

float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 normalW, float3 tangentW, float strength)
{
	normalMapSample = 2.0f * normalMapSample - 1.0f; // unpack normal
	normalMapSample = normalize(normalMapSample); // normalize normal
	
	// normal intensity
	normalMapSample.r *= strength;
	normalMapSample.g *= strength;
	
	float3 N = normalW;
	float3 T = normalize(tangentW - dot(tangentW, N) * N); // re-orthogonalize T with respect to N
	float3 B = cross(N, T); // calculate the perpendicular vector B with the cross product of T and N
	
	// construct TBN matrix
	float3x3 TBN = float3x3(T, B, N); 
	
	float3 bumpedNormal = normalize(mul(normalMapSample, TBN)); // transform from tangent space to world space
	
    return bumpedNormal;
}