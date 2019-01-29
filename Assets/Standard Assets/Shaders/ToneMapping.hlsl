float3 ToneMapReinhard(float3 color)
{
	return color / (1 + color);
}

float3 Uncharted2(float3 x)
{
    float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	float W = 11.2;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

//== ACESFitted ===========================
//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//  All code licensed under the MIT license
//=========================================

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ACESFitted(float3 color)
{
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = saturate(color);

    return color;
}

float3 ToneMap(float3 color)
{
	[branch]
    if (g_toneMapping == 0) // OFF
    {
		// Do nothing
    }
	else if (g_toneMapping == 1) // ACES
	{
		color = ACESFitted(color);
	}
	else if (g_toneMapping == 2) // REINHARD
	{
		color = ToneMapReinhard(color);
	}
	else if (g_toneMapping == 3) // UNCHARTED 2
	{
		color = Uncharted2(color);
	}
	
	return color;
}