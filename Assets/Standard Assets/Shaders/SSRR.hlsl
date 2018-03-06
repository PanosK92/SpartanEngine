static const int g_maxBinarySearchStep = 20;
static const int g_maxRayStep = 40;
static const float g_depthbias = 0.01f;
static const float g_rayStepScale = 1.05f;
static const float g_maxThickness = 1.8f / farPlane; // far plane
static const float g_maxRayLength = 20.0f;

float Noise(float2 seed)
{
	return frac(sin(dot(seed.xy, float2(12.9898f, 78.233f)) * 43758.5453f));
}

float3 GetTexCoordXYLinearDepthZ(float3 viewPos)
{
	float4 projPos = mul(float4(viewPos, 1.0f), mProjection);
	projPos.xy /= projPos.w;
	projPos.xy = projPos.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
	projPos.z = projPos.w / farPlane;

	return projPos.xyz;
}

float4 BinarySearch( float3 dir, float3 viewPos )
{
	float3 texCoord = float3(0.0f, 0.0f, 0.0f);
	float srcdepth = 0.0f;
	float depthDiff = 0.0f;

	[loop]
	for ( int i = 0; i < g_maxBinarySearchStep; ++i )
	{
		texCoord = GetTexCoordXYLinearDepthZ( viewPos );
		srcdepth = texDepth.Sample(samplerAniso, texCoord.xy).x;
		depthDiff = srcdepth.x - texCoord.z;

		if (depthDiff > 0.0f)
		{
			viewPos += dir;
			dir *= 0.5f;
		}

		viewPos -= dir;
	}

	texCoord = GetTexCoordXYLinearDepthZ( viewPos );	
	srcdepth = texDepth.Sample(samplerAniso, texCoord.xy).x;
	depthDiff = abs(srcdepth - texCoord.z);
	if (texCoord.z < 0.9f && depthDiff < g_depthbias )
	{
		return texLastFrame.Sample(samplerAniso, texCoord.xy);
	}

	return float4(0.0f, 0.0f, 0.0f, 0.0f);
}

float3 SSRR(float3 viewDirW, float3 normalW)
{
    float3 viewPos = mul(float4(viewDirW, 1.0f), mView).xyz;
	float3 viewNormal = mul(float4(normalW, 1.0f), mView).xyz;
	
	float3 incidentVec = normalize(viewPos);
	viewNormal = normalize(viewNormal);

	float3 reflectionVector = reflect(incidentVec, viewNormal);
	reflectionVector = normalize(reflectionVector);
	reflectionVector *= g_rayStepScale;

	float3 reflectPos = viewPos + reflectionVector;

	[loop]
	for ( int i = 0; i < g_maxRayStep; ++i )
	{
		float3 texCoord = GetTexCoordXYLinearDepthZ( reflectPos );
		float srcdepth = texDepth.Sample(samplerAniso, texCoord.xy).x;

		if ( texCoord.z - srcdepth > 0 && texCoord.z - srcdepth < g_maxThickness )
		{
			float4 reflectColor = BinarySearch( reflectionVector, reflectPos );

			float edgeFade = 1.0f - pow(length(texCoord.xy - 0.5f) * 2.0f, 2.0f);
			reflectColor.a *= pow( 0.75f, (length( reflectPos - viewPos ) / g_maxRayLength) ) * edgeFade;
			return reflectColor.rgb;
		}
		else
		{
			reflectPos = viewPos + ( ( i + Noise( texCoord.xy ) ) * reflectionVector );
		}
	}

	return float3(0.0f, 0.0f, 0.0f);
}