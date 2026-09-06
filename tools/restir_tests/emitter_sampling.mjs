// Runs the extracted HLSL sampler as C++ float arithmetic. Invoke from a VS developer shell.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const shader = fs.readFileSync(process.argv[2] ?? 'data/shaders/restir_reservoir.hlsl', 'utf8');
const start = shader.indexOf('void sample_spherical_triangle(');
const end = shader.indexOf('// path flags', start);
if (start < 0 || end < 0) throw new Error('Sampler block not found');
const sampler = shader.slice(start, end).replace(/out\s+(float3|float)\s+/g, '$1& ');
const directory = path.resolve('binaries/restir_tests');
fs.mkdirSync(directory, {recursive:true});
const source = path.join(directory, 'emitter_sampling.cpp');
fs.writeFileSync(source, `
#include <cmath>
#include <algorithm>
#include <iostream>
#include <random>
using std::min; using std::max; using std::clamp; using std::isnan; using std::isinf;
// The standard float overloads match HLSL precision in the extracted production body.
using std::sqrt; using std::sin; using std::cos; using std::acos; using std::abs; using std::atan2;
constexpr float PI=3.14159265358979323846f;
struct float2 { float x,y; };
struct float3 {
 float x,y,z; float3()=default; float3(float a,float b,float c):x(a),y(b),z(c){}
 float3 operator-() const{return {-x,-y,-z};}
};
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float3 operator-(float3 a,float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float3 operator*(float a,float3 b){return b*a;}
float3 operator/(float3 a,float b){return {a.x/b,a.y/b,a.z/b};}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float3 cross(float3 a,float3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float length(float3 a){return sqrt(dot(a,a));}
float3 normalize(float3 a){return a/length(a);}
float lerp(float a,float b,float t){return a+(b-a)*t;}
float saturate(float a){return clamp(a,0.0f,1.0f);}
${sampler}

int main(){
 std::mt19937 rng(123); std::uniform_real_distribution<float> uniform(0.0f,1.0f);
 int failures=0;
 for(float distance : {1.0f,4.0f,12.0f,30.0f,60.0f,120.0f,240.0f}){
  float3 o(0,1,0), a(distance,3.1f,0), b(distance+1,3.1f,0), c(distance,3.1f,2);
  int outside=0; double sum=0, cosine_sum=0;
  for(int i=0;i<20000;i++){
   float3 d; float inv_pdf;
   sample_spherical_triangle(o,a,b,c,{uniform(rng),uniform(rng)},d,inv_pdf);
   float3 p=o+d*((a.y-o.y)/d.y); float b1=p.x-distance,b2=p.z/2;
   if(!std::isfinite(p.x)||!std::isfinite(p.z)||!(inv_pdf>0)||b1<-.001f||b2<-.001f||b1+b2>1.001f) outside++;
   sum+=inv_pdf;
   cosine_sum+=inv_pdf*d.y;
  }
  // Independent double precision quadrature of area*cos(theta)/distance^2.
  double integral=0, cosine_integral=0; constexpr int n=500;
  for(int i=0;i<n;i++)for(int j=0;j<n;j++){
   double su=std::sqrt((i+.5)/n),v=(j+.5)/n;
   double x=distance+su*(1-v),y=double(a.y)-o.y,z=2*su*v;
   integral+=y/std::pow(x*x+y*y+z*z,1.5);
   cosine_integral+=y*y/std::pow(x*x+y*y+z*z,2.0);
  }
  integral/=double(n)*n;
  cosine_integral/=double(n)*n;
  double relative=std::abs(sum/20000/integral-1);
  double cosine_relative=std::abs(cosine_sum/20000/cosine_integral-1);
  std::cout<<"distance="<<distance<<" outside="<<outside<<" solid-angle error="<<relative<<" diffuse integral error="<<cosine_relative<<'\\n';
  if(outside||relative>.015||cosine_relative>.015)failures++;
 }
 return failures?1:0;
}
`);
const exe = path.join(directory, 'emitter_sampling.exe');
const compile = spawnSync(process.env.CXX ?? 'cl', ['/nologo','/EHsc','/std:c++17','/O2','/fp:strict',source,`/Fe:${exe}`,`/Fo:${path.join(directory,'emitter_sampling.obj')}`], {encoding:'utf8'});
if (compile.status !== 0) { console.error(compile.error ?? compile.stdout + compile.stderr); process.exit(1); }
const run = spawnSync(exe, [], {encoding:'utf8'});
process.stdout.write(run.stdout ?? '');
process.stderr.write(run.stderr ?? '');
process.exit(run.status ?? 1);
