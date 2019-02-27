float4 Dither_Ordered(float4 color, float2 texcoord)
{
    float ditherBits = 8.0;

    float2 ditherSize	= float2(1.0 / 16.0, 10.0 / 36.0);
    float gridPosition	= frac(dot(texcoord, (g_resolution * ditherSize)) + 0.25);
    float ditherShift	= (0.25) * (1.0 / (pow(2.0, ditherBits) - 1.0));

    float3 RGBShift = float3(ditherShift, -ditherShift, ditherShift);
    RGBShift = lerp(2.0 * RGBShift, -2.0 * RGBShift, gridPosition);

    color.rgb += RGBShift;

    return color;
}