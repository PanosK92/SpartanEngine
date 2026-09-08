// Generate isolated fixtures from production functions; no alternate formula under test.
import fs from 'node:fs';
import {createHash} from 'node:crypto';
const out = 'binaries/lighting_tests';
fs.mkdirSync(out, {recursive:true});
// Published MIT reference, pinned so the comparison cannot silently change.
const referencePath = `${out}/gt7_reference.cpp`;
if (!fs.existsSync(referencePath)) {
    const response = await fetch('https://blog.selfshadow.com/publications/s2025-shading-course/pdi/supplemental/gt7_tone_mapping.cpp');
    if (!response.ok) throw new Error(`GT7 reference download: ${response.status}`);
    fs.writeFileSync(referencePath,await response.text());
}
const referenceHash=createHash('sha256').update(fs.readFileSync(referencePath)).digest('hex');
if(referenceHash!=='94df7e7e310b9423ebb5aaaa29416f376ebbae66e0948fb259f8688314b29822')throw new Error('GT7 reference checksum mismatch');
const read = p => fs.readFileSync(p, 'utf8');
function body(text, signature) {
    const start = text.indexOf(signature);
    if (start < 0) throw new Error(`Missing ${signature}`);
    const open = text.indexOf('{', start);
    let depth = 1, end = open + 1;
    while (depth && end < text.length) { if (text[end] === '{') depth++; if (text[end] === '}') depth--; end++; }
    if (depth) throw new Error(`Unclosed ${signature}`);
    return text.slice(open, end);
}
const light = read('source/world/components/Light.cpp');
const camera = read('source/world/components/Camera.h');
const color = read('source/rendering/Color.cpp');
fs.writeFileSync(`${out}/cpu_functions.h`, `
#include "../../data/shaders/shared_lighting.h"
namespace lighting = spartan::lighting;
using std::max; using std::min; using std::clamp;
constexpr float pi=3.14159265358979323846f;
enum class LightType {Directional,Point,Spot,Area,Max};
struct TestLight {
 LightType m_light_type; float m_intensity_photometric=800, m_angle_rad=pi/6, m_area_width=2, m_area_height=3;
 float intensity() const ${body(light, 'float Light::GetIntensityRadiometric() const')}
};
struct TestCamera {
 float m_aperture=5.6f,m_shutter_speed=1.0f/125,m_iso=200;
 float exposure() const ${body(camera, 'float GetExposure() const')}
};
#define SP_ASSERT assert
void temperature_to_color(float temperature_kelvin,float& r,float& g,float& b)
${body(color, 'static void temperature_to_color(')}
`);
const common = read('data/shaders/common.hlsl');
// Use function extraction rather than depending on comments/line endings.
const functions = ['float radiometric_to_photometric(float value)', 'float3 radiometric_to_photometric(float3 value)',
    'float photometric_to_radiometric(float value)', 'float3 photometric_to_radiometric(float3 value)', 'float get_effective_exposure()']
    .map(signature => `${signature}\n${body(common,signature)}`).join('\n');
const header = `
#include "../../data/shaders/shared_lighting.h"
#include "../../data/shaders/common_colorspace.hlsl"
#define THREAD_GROUP_COUNT_X 8
#define THREAD_GROUP_COUNT_Y 8
static const float LUMINOUS_EFFICACY_MAX = lighting_luminous_efficacy;
static const float PI=3.14159265358979323846f;
static const float PI2=6.28318530718f;
static const float INV_PI=0.31830988618f;
struct Frame { float camera_exposure; float camera_exposure_mode; float hdr_enabled; float hdr_max_nits;
               float hdr_sdr_white_nits; float delta_time; float pad0; float pad1; };
cbuffer TestConstants : register(b0) { Frame buffer_frame; float4 pass_values; };
float3 pass_get_f3_value(){return pass_values.xyz;}
Texture2D<float4> tex : register(t0);
Texture2D<float4> tex2 : register(t1);
Texture2D<float> tex_effective_exposure : register(t1);
RWTexture2D<float4> tex_uav : register(u0);
SamplerState samplers[1] : register(s0);
${functions}
`;
for (const name of ['output','auto_exposure']) {
    const source = read(`data/shaders/${name}.hlsl`);
    fs.writeFileSync(`${out}/${name}_fixture.hlsl`, source.replace('#include "common.hlsl"', header));
}
const iblLine = read('data/shaders/light_image_based.hlsl').split(/\r?\n/).find(x=>/float3 diffuse_ibl\s*=/.test(x));
fs.writeFileSync(`${out}/ibl_fixture.hlsl`, header + `
#include "../../data/shaders/spherical_harmonics.hlsl"
struct TestSurface {float3 albedo;};
[numthreads(8,8,1)] void main_cs(uint3 tid:SV_DispatchThreadID) {
    float3 radiance = tex[tid.xy].rgb;
    float3 sh[9]; for(uint i=0;i<9;i++) sh[i]=0;
    sh[0]=radiance * sqrt(4.0f*PI);
    float3 diffuse_skysphere=sh_irradiance_l2(float3(0,1,0),sh,1);
    float3 bounce_boost=1, diffuse_energy=1;
    TestSurface surface; surface.albedo=0.18f;
    ${iblLine}
    tex_uav[tid.xy]=float4(diffuse_ibl,1);
}
`);
console.log('Generated CPU, output, metering and white-furnace fixtures from production code.');
const reservoir=read('data/shaders/restir_reservoir.hlsl');
fs.writeFileSync(`${out}/rectangle_fixture.hlsl`,header+`
void sample_spherical_rectangle(float3 origin,float3 rect_origin,float3 ex,float3 ey,float2 xi,out float3 out_pos,out float out_solid_angle)
${body(reservoir,'void sample_spherical_rectangle(')}
[numthreads(8,8,1)] void main_cs(uint3 tid:SV_DispatchThreadID) {
 float4 v=tex[tid.xy];float3 pos;float omega;
 sample_spherical_rectangle(float3(0,0,v.b),float3(-1,-1,0),float3(2,0,0),float3(0,2,0),v.rg,pos,omega);
 tex_uav[tid.xy]=float4(pos,omega);
}
`);
