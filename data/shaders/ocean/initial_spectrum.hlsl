//static const float PI = 3.14159265f;

#include "../common.hlsl"

static const float G = 9.81f;
static const uint SPECTRUM_TEX_SIZE = 512;
static const uint LENGTH_SCALE = SPECTRUM_TEX_SIZE / 8;

RWTexture2D<float4> initial_spectrum : register(u9);

// Heavily Inspired by Acerola's Implementation:
// https://github.com/GarrettGunnell/Water/blob/main/Assets/Shaders/FFTWater.compute
float2 UniformToGaussian(float u1, float u2)
{
    float R = sqrt(-2.0f * log(u1));
    float theta = 2.0f * PI * u2;

    return float2(R * cos(theta), R * sin(theta));
}

float Dispersion(float kMag, float depth)
{
    return sqrt(G * kMag * tanh(min(kMag * depth, 20)));
}

float DispersionDerivative(float kMag, float depth)
{
    float th = tanh(min(kMag * depth, 20));
    float ch = cosh(kMag * depth);
    return G * (depth * kMag / ch / ch + th) / Dispersion(kMag, depth) / 2.0f;
}

float NormalizationFactor(float s)
{
    float s2 = s * s;
    float s3 = s2 * s;
    float s4 = s3 * s;
    if (s < 5)
        return -0.000564f * s4 + 0.00776f * s3 - 0.044f * s2 + 0.192f * s + 0.163f;
    else
        return -4.80e-08f * s4 + 1.07e-05f * s3 - 9.53e-04f * s2 + 5.90e-02f * s + 3.93e-01f;
}

float DonelanBannerBeta(float x)
{
    if (x < 0.95f)
        return 2.61f * pow(abs(x), 1.3f);
    if (x < 1.6f)
        return 2.28f * pow(abs(x), -1.3f);

    float p = -0.4f + 0.8393f * exp(-0.567f * log(x * x));
    return pow(10.0f, p);
}

float DonelanBanner(float theta, float omega, float peakOmega)
{
    float beta = DonelanBannerBeta(omega / peakOmega);
    float sech = 1.0f / cosh(beta * theta);
    return beta / 2.0f / tanh(beta * 3.1416f) * sech * sech;
}

float Cosine2s(float theta, float s)
{
    return NormalizationFactor(s) * pow(abs(cos(0.5f * theta)), 2.0f * s);
}

float SpreadPower(float omega, float peakOmega)
{
    if (omega > peakOmega)
        return 9.77f * pow(abs(omega / peakOmega), -2.5f);
    else
        return 6.97f * pow(abs(omega / peakOmega), 5.0f);
}

float DirectionSpectrum(float theta, float omega, float peakOmega, float swell, float spreadBlend, float angle)
{
    float s = SpreadPower(omega, peakOmega) + 16 * tanh(min(omega / peakOmega, 20)) * swell * swell;

    return lerp(2.0f / 3.1415f * cos(theta) * cos(theta), Cosine2s(theta - angle, s), spreadBlend);
}

float TMACorrection(float omega, float depth)
{
    float omegaH = omega * sqrt(depth / G);
    if (omegaH <= 1.0f)
        return 0.5f * omegaH * omegaH;
    if (omegaH < 2.0f)
        return 1.0f - 0.5f * (2.0f - omegaH) * (2.0f - omegaH);

    return 1.0f;
}

float JONSWAP(float omega, float peakOmega, float gamma, float scale, float alpha, float depth)
{
    float sigma = (omega <= peakOmega) ? 0.07f : 0.09f;

    float r = exp(-(omega - peakOmega) * (omega - peakOmega) / 2.0f / sigma / sigma / peakOmega / peakOmega);

    float oneOverOmega = 1.0f / (omega + 1e-6f);
    float peakOmegaOverOmega = peakOmega / omega;
    return scale * TMACorrection(omega, depth) * alpha * G * G
		* oneOverOmega * oneOverOmega * oneOverOmega * oneOverOmega * oneOverOmega
		* exp(-1.25f * peakOmegaOverOmega * peakOmegaOverOmega * peakOmegaOverOmega * peakOmegaOverOmega)
		* pow(abs(gamma), r);
}

float ShortWavesFade(float kLength, float shortWavesFade)
{
    return exp(-shortWavesFade * shortWavesFade * kLength * kLength);
}

float hash(uint n)
{
    // integer hash copied from Hugo Elias
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 0x789221U) + 0x1376312589U;
    return float(n & uint(0x7fffffffU)) / float(0x7fffffff);
}

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    initial_spectrum.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);
    
    uint seed = thread_id.x + SPECTRUM_TEX_SIZE * thread_id.y + SPECTRUM_TEX_SIZE;
    seed += pass_get_f2_value().x; // seed += frame_number

    OceanParameters params = surface.ocean_parameters;
    
    float halfN = SPECTRUM_TEX_SIZE / 2.0f;

    float deltaK = 2.0f * PI / LENGTH_SCALE;
    float2 K = (thread_id.xy - halfN) * deltaK;
    float kLength = length(K);

    seed += hash(seed) * 10;
    float4 uniformRandSamples = float4(hash(seed), hash(seed * 2), hash(seed * 3), hash(seed * 4));
    float2 gauss1 = UniformToGaussian(uniformRandSamples.x, uniformRandSamples.y);
    float2 gauss2 = UniformToGaussian(uniformRandSamples.z, uniformRandSamples.w);

    if (params.lowCutoff <= kLength && kLength <= params.highCutoff)
    {
        float kAngle = atan2(K.y, K.x);
        float omega = Dispersion(kLength, params.depth);
        float dOmegadk = DispersionDerivative(kLength, params.depth);

        float spectrum = JONSWAP(omega, params.peakOmega, params.gamma, params.scale, params.alpha, params.depth) * DirectionSpectrum(kAngle, omega, params.peakOmega, params.swell, params.spreadBlend, params.angle) * ShortWavesFade(kLength, params.shortWavesFade);
        
        initial_spectrum[thread_id.xy] = float4(float2(gauss2.x, gauss1.y) * float2(sqrt(2 * spectrum * abs(dOmegadk) / kLength * deltaK * deltaK).xx), 0.0f, 0.0f);
    }
    else
    {
        initial_spectrum[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
}
