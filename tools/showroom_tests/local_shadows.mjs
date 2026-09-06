// Execute the production local-shadow sampler and dispatcher with analytic blockers.
// Run from the repository root in a Visual Studio developer shell.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

const shader = fs.readFileSync(process.argv[2] ?? 'data/shaders/ray_traced_shadows.hlsl', 'utf8');
const helperStart = shader.indexOf('static const uint AREA_SHADOW_SAMPLES');
const helperEnd = shader.indexOf('[shader("raygeneration")]', helperStart);
const dispatchStart = shader.indexOf('for (uint light_i = 1u;');
const dispatchEnd = shader.indexOf('[shader("miss")]', dispatchStart);
if (helperStart < 0 || helperEnd < 0 || dispatchStart < 0 || dispatchEnd < 0)
    throw new Error('Local shadow generation is missing');
const helpers = shader.slice(helperStart, helperEnd);
const dispatch = shader.slice(dispatchStart, dispatchEnd).trim().replace(/\}\s*$/, '');
const directory = path.resolve('binaries/showroom_tests');
fs.mkdirSync(directory, {recursive:true});
const cpp = path.join(directory, 'local_shadows.cpp');
fs.writeFileSync(cpp, `
#include <cmath>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
using uint = unsigned; using std::min; using std::max; using std::clamp;
struct uint2 { uint x,y; };
struct float2 { float x,y; float2(float a,float b):x(a),y(b){} };
float2 operator+(float2 a,float b){return {a.x+b,a.y+b};}
float2 operator-(float2 a,float b){return {a.x-b,a.y-b};}
float2 operator/(float2 a,float2 b){return {a.x/b.x,a.y/b.y};}
struct float3 {
 float x,y,z; float3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
 float3 operator-() const{return {-x,-y,-z};}
 float3& operator+=(float3 b){x+=b.x;y+=b.y;z+=b.z;return *this;}
};
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float3 operator-(float3 a,float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float3 operator/(float3 a,float b){return {a.x/b,a.y/b,a.z/b};}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float length(float3 a){return std::sqrt(dot(a,a));}
float3 normalize(float3 a){return a/length(a);}
float3 cross(float3 a,float3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
struct LightParameters {
 uint flags=0; float3 position={0,2,0},direction={0,-1,0},direction_right={1,0,0};
 float range=20,area_width=1,area_height=1;
};
constexpr uint nrd_local_shadow_max=4;
constexpr float SHADOW_RAY_MAX_DISTANCE=1000;
struct Shadow {float visibility=1,hit=0;};
Shadow output[4]; uint rays=0; float plane_y=1; bool half_plane=true;
void write_local_shadow(uint2, uint slice, float visibility,float hit,float,float){
 assert(slice<4); output[slice]={visibility,hit};
}
float2 trace_opaque_shadow(float3 origin,float3 direction,float t_max){
 rays++; assert(std::abs(length(direction)-1)<1e-5f); assert(t_max>0 && t_max<=1000);
 float t=(plane_y-origin.y)/direction.y;
 if(t>=0.001f && t<t_max && (!half_plane || origin.x+direction.x*t<0)) return {0,t};
 return {1,0};
}
${helpers}
std::vector<LightParameters> light_parameters;
struct {uint cluster_light_count;} buffer_frame;
void run(){
 for(auto& result:output)result={}; rays=0;
 uint2 launch_id={0,0}; float camera_distance=1; float3 pos_ws={0,0,0},normal_ws={0,1,0};
 buffer_frame.cluster_light_count=uint(light_parameters.size());
 ${dispatch}
}
LightParameters area(float x,uint slot){LightParameters l;l.position.x=x;l.flags=(1u<<6)|(1u<<3)|(slot<<8);return l;}
int main(){
 // An inactive sun plus three area lights, with shuffled slot assignments.
 light_parameters={{},area(-2,3),area(0,1),area(2,2)}; run();
 assert(rays==3*AREA_SHADOW_SAMPLES);
 assert(output[2].visibility==0 && output[0].visibility==0.5f && output[1].visibility==1);
 assert(output[3].visibility==1);
 std::cout<<"PASS three independent area lights: blocked, penumbra, clear; inactive sun\\n";
 half_plane=false; plane_y=0.02f; run();
 assert(output[0].visibility==0 && output[0].hit>0 && output[0].hit<0.08f);
 plane_y=3; run(); for(auto s:output)assert(s.visibility==1);
 std::cout<<"PASS close blockers remain opaque; blockers beyond emitters do not cast shadows\\n";
 plane_y=1;
 for(uint type:{1u,2u}){
  auto l=area(0,1); l.flags=(1u<<type)|(1u<<3)|(1u<<8); light_parameters={{},l};run();
  assert(rays==1 && output[0].visibility==0);
 }
 std::cout<<"PASS point and spot lights trace their assigned slot\\n";
 for(uint flags:{0u,(1u<<6)|(1u<<3),(1u<<6)|(1u<<8),(1u<<6)|(1u<<3)|(7u<<8)}){
  auto l=area(0,1);l.flags=flags;light_parameters={{},l};run();assert(rays==0);
 }
 auto l=area(0,1);l.range=1;light_parameters={{},l};run();assert(rays==0);
 l=area(0,1);l.direction={0,1,0};light_parameters={{},l};run();assert(rays==0);
 // Range is measured from the rectangle, not its center, just like attenuation.
 l=area(4,1);l.area_width=10;l.range=3;light_parameters={{},l};run();assert(rays==AREA_SHADOW_SAMPLES);
 std::cout<<"PASS invalid/unshadowed slots, range, one-sided emission, wide-emitter range\\n";
 for(float2 size:{float2(1,1),float2(0.1f,2.34f),float2(2.34f,0.1f)}){
  float2 mean={0,0};
  for(uint s=0;s<AREA_SHADOW_SAMPLES;s++){
   auto uv=area_shadow_rect_uv(s,size.x,size.y);
   assert(std::abs(uv.x)<0.5f && std::abs(uv.y)<0.5f);mean.x+=uv.x;mean.y+=uv.y;
  }
  assert(std::abs(mean.x)<1e-6f && std::abs(mean.y)<1e-6f);
 }
 std::cout<<"PASS panel and vertical/horizontal tube sample bounds and symmetry\\n";
}
`);
const exe = path.join(directory, 'local_shadows.exe');
const compiled = spawnSync('cl.exe', ['/nologo','/std:c++17','/EHsc','/O2',cpp,`/Fe${exe}`,`/Fo${directory}/`], {encoding:'utf8'});
if (compiled.status !== 0) throw new Error(compiled.error ?? compiled.stdout + compiled.stderr);
const result = spawnSync(exe, [], {encoding:'utf8'});
console.log(result.stdout);
if (result.status !== 0) throw new Error(result.stderr || `Shadow regression failed (${result.status})`);
