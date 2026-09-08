import fs from 'node:fs';
const out='binaries/bloom_tests';
fs.mkdirSync(out,{recursive:true});
const header=`
#define THREAD_GROUP_COUNT_X 8
#define THREAD_GROUP_COUNT_Y 8
static const uint sampler_bilinear_clamp=0;
cbuffer TestParams : register(b0) { float4 parameters; };
float3 pass_get_f3_value(){return parameters.xyz;}
float get_effective_exposure(){return parameters.w;}
float3 radiometric_to_photometric(float3 color){return color*683.0f;}
Texture2D<float4> tex : register(t0);
Texture2D<float4> tex2 : register(t1);
RWTexture2D<float4> tex_uav : register(u0);
SamplerState samplers[1] : register(s0);
`;
const production=fs.readFileSync('data/shaders/bloom.hlsl','utf8');
fs.writeFileSync(`${out}/bloom_fixture.hlsl`,production.replace('#include "common.hlsl"',header));
const baseline=`${out}/bloom_before.hlsl`;
if(fs.existsSync(baseline))fs.writeFileSync(`${out}/baseline_fixture.hlsl`,fs.readFileSync(baseline,'utf8').replace('#include "common.hlsl"',header));
// Matches the old generic 2x2 mip averaging on the even-size benchmark.
fs.writeFileSync(`${out}/box_fixture.hlsl`,header+`
[numthreads(8,8,1)]void main_cs(uint3 tid:SV_DispatchThreadID){
 uint w,h;tex_uav.GetDimensions(w,h);if(any(tid.xy>=uint2(w,h)))return;
 tex_uav[tid.xy]=tex.SampleLevel(samplers[0],(float2(tid.xy)+.5f)/float2(w,h),0);
}`);
console.log('Generated bloom GPU fixtures from production HLSL.');
