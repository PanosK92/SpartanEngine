#include "../common.hlsl"

static const uint SPECTRUM_TEX_SIZE = 512;

RWTexture2D<float4> initial_spectrum : register(u9);

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // Heavily Inspired by Acerola's Implementation:
    // https://github.com/GarrettGunnell/Water/blob/main/Assets/Shaders/FFTWater.compute
    float2 h0 = initial_spectrum[thread_id.xy].rg;
    float2 h0conj = initial_spectrum[uint2((SPECTRUM_TEX_SIZE - thread_id.x) % SPECTRUM_TEX_SIZE, (SPECTRUM_TEX_SIZE - thread_id.y) % SPECTRUM_TEX_SIZE)].rg;

    initial_spectrum[thread_id.xy] = float4(h0, h0conj.x, -h0conj.y);
}
