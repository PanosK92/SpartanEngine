RWTexture2D<float4> initial_spectrum : register(u9);

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    initial_spectrum[thread_id.xy] = float4(1.0f, 0.0f, 1.0f, 1.0f);
}
