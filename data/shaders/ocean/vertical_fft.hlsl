#include "fft_common.hlsl"

RWTexture2D<float4> displacement_spectrum : register(u10);
RWTexture2D<float4> slope_spectrum : register(u11);

[numthreads(512, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    displacement_spectrum[thread_id.yx] = FFT(thread_id.x, displacement_spectrum[thread_id.yx]);
    slope_spectrum[thread_id.yx] = FFT(thread_id.x, slope_spectrum[thread_id.yx]);
}
