// Executes the production HLSL bodies in a small D3D11 compute harness.
// Full production shaders are separately compiled for Vulkan with DXC.
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#undef near
#define main gt7_reference_main
#include "../../binaries/lighting_tests/gt7_reference.cpp"
#undef main
using Microsoft::WRL::ComPtr;
struct Pixel {float r,g,b,a=1;};
struct Params {
    float exposure=1, automatic=0, hdr=0, peak=1000;
    float white=203, dt=1.f/60, pad0=0, pad1=0;
    float mapper=4, compensation=0, force_sdr=0, pad2=0;
};
void hr(HRESULT result) {if(FAILED(result)) {char s[64];std::snprintf(s,sizeof(s),"HRESULT 0x%08lx",result);throw std::runtime_error(s);}}
void near(double actual,double expected,double tolerance,const char* label) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>tolerance) {
        std::printf("FAIL %s: %.9g != %.9g (tol %.4g)\n",label,actual,expected,tolerance);
        throw std::runtime_error(label);
    }
}
struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Buffer> constants;
    Gpu() {
        D3D_FEATURE_LEVEL level;
        HRESULT result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
        if(FAILED(result)) hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));
        D3D11_BUFFER_DESC d={};d.ByteWidth=sizeof(Params);d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr(device->CreateBuffer(&d,nullptr,&constants));
    }
    ComPtr<ID3D11ComputeShader> compile(const wchar_t* path) {
        ComPtr<ID3DBlob> code,errors;
        HRESULT result=D3DCompileFromFile(path,nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main_cs","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors) std::fprintf(stderr,"%s",static_cast<const char*>(errors->GetBufferPointer()));
        hr(result);ComPtr<ID3D11ComputeShader> shader;
        hr(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));return shader;
    }
    std::vector<Pixel> run(ID3D11ComputeShader* shader,const std::vector<Pixel>& pixels,Params p,float previous=0,bool meter=false) {
        const unsigned w=64,h=64;
        if(pixels.size()!=w*h) throw std::runtime_error("fixture size");
        D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial={pixels.data(),w*sizeof(Pixel),0};
        ComPtr<ID3D11Texture2D> input,history,output,readback;
        hr(device->CreateTexture2D(&d,&initial,&input));
        std::vector<Pixel> prev(w*h,Pixel{previous,previous,previous});initial.pSysMem=prev.data();
        hr(device->CreateTexture2D(&d,&initial,&history));
        d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;hr(device->CreateTexture2D(&d,nullptr,&output));
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;hr(device->CreateTexture2D(&d,nullptr,&readback));
        ComPtr<ID3D11ShaderResourceView> srv,prev_srv;ComPtr<ID3D11UnorderedAccessView> uav;
        hr(device->CreateShaderResourceView(input.Get(),nullptr,&srv));hr(device->CreateShaderResourceView(history.Get(),nullptr,&prev_srv));
        hr(device->CreateUnorderedAccessView(output.Get(),nullptr,&uav));
        ID3D11ShaderResourceView* views[]={srv.Get(),prev_srv.Get()};
        context->UpdateSubresource(constants.Get(),0,nullptr,&p,0,0);
        context->CSSetConstantBuffers(0,1,constants.GetAddressOf());context->CSSetShader(shader,nullptr,0);
        context->CSSetShaderResources(0,2,views);context->CSSetUnorderedAccessViews(0,1,uav.GetAddressOf(),nullptr);
        context->Dispatch(meter?1:w/8,meter?1:h/8,1);
        context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE map;
        hr(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&map));
        std::vector<Pixel> result(w*h);
        for(unsigned y=0;y<h;y++)std::memcpy(result.data()+y*w,static_cast<const char*>(map.pData)+map.RowPitch*y,w*sizeof(Pixel));
        context->Unmap(readback.Get(),0);views[0]=views[1]=nullptr;ID3D11UnorderedAccessView* empty=nullptr;
        context->CSSetShaderResources(0,2,views);context->CSSetUnorderedAccessViews(0,1,&empty,nullptr);return result;
    }
};
std::vector<Pixel> uniform(float nits) {return std::vector<Pixel>(4096,Pixel{nits/683,nits/683,nits/683});}
double linear(double x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
double pq_nits(double x) {
    const double m1=2610./16384,m2=2523./32,c1=3424./4096,c2=2413./128,c3=2392./128;
    double p=std::pow(x,1/m2);return 10000*std::pow(std::max(p-c1,0.)/(c2-c3*p),1/m1);
}
int run_tests() {
    Gpu gpu;auto output=gpu.compile(L"binaries/lighting_tests/output_fixture.hlsl");
    auto meter=gpu.compile(L"binaries/lighting_tests/auto_exposure_fixture.hlsl");
    auto ibl=gpu.compile(L"binaries/lighting_tests/ibl_fixture.hlsl");
    auto rectangle=gpu.compile(L"binaries/lighting_tests/rectangle_fixture.hlsl");Params p;
    // Neutral gray is relative to paper white at the camera/output boundary.
    auto gray=gpu.run(output.Get(),uniform(.18f),p)[0];
    near(linear(gray.r),.1776,.003,"GT7 middle gray");
    for(float mapper : {0.f,1.f,2.f,3.f,4.f,5.f}) {
        p.mapper=mapper;
        for(float level : {0.f,.001f,.01f,.18f,1.f,10.f,1000.f}) {
            auto c=gpu.run(output.Get(),uniform(level),p)[0];
            near(c.r,c.g,.0004,"neutral R/G");near(c.r,c.b,.0004,"neutral R/B");
            if(level==0)near(c.r,0,.0001,"black");
        }
    }
    p.mapper=4;
    double hdr_worst=0,reference_worst=0;
    std::vector<Pixel> colors(4096);
    for(size_t i=0;i<colors.size();i++) {
        float v=std::exp2(-10.f+20.f*float(i)/float(colors.size()-1));
        colors[i]=Pixel{v/683,v*float((i*17)%101)/100/683,v*float((i*37)%101)/100/683};
    }
    for(float peak : {250.f,1000.f,4000.f,10000.f}) {
        p.peak=peak;p.hdr=1;auto pq=gpu.run(output.Get(),colors,p);
        p.hdr=2;auto scrgb=gpu.run(output.Get(),colors,p);
        GT7ToneMapping reference;reference.initializeAsHDR(peak);
        for(size_t i=0;i<colors.size();i++) {
            Pixel c=colors[i];float input[3]={
                (0.6274040f*c.r+0.3292820f*c.g+0.0433136f*c.b)*683*2.5f,
                (0.0690970f*c.r+0.9195400f*c.g+0.0113612f*c.b)*683*2.5f,
                (0.0163916f*c.r+0.0880132f*c.g+0.8955950f*c.b)*683*2.5f};
            float expected[3];reference.applyToneMapping(input,expected);
            Pixel s=scrgb[i];double scrgb2020[3]={
                (.6274040*s.r+.3292820*s.g+.0433136*s.b)*80,
                (.0690970*s.r+.9195400*s.g+.0113612*s.b)*80,
                (.0163916*s.r+.0880132*s.g+.8955950*s.b)*80};
            double pqValues[3]={pq_nits(pq[i].r),pq_nits(pq[i].g),pq_nits(pq[i].b)};
            for(int k=0;k<3;k++) {
                double tol=std::max(.015,peak*.0005);
                near(pqValues[k],scrgb2020[k],tol,"HDR10/scRGB nits parity");
                // GPU transcendental approximations differ from the CPU libm
                // after the steep PQ inverse; allow 0.1% of display peak here.
                near(pqValues[k],expected[k]*100,std::max(.015,peak*.001),"published GT7 reference");
                if(pqValues[k]>peak+tol)throw std::runtime_error("HDR peak exceeded");
                hdr_worst=std::max(hdr_worst,std::abs(pqValues[k]-scrgb2020[k]));
                reference_worst=std::max(reference_worst,std::abs(pqValues[k]-expected[k]*100));
            }
        }
    }
    p=Params{};p.mapper=0; // auto exposure speed zero = immediate
    for(float level : {.00001f,.01f,1.f,100.f,10000.f,100000.f}) {
        p.compensation=0;float base=gpu.run(meter.Get(),uniform(level),p,0,true)[0].r;
        p.compensation=1;float plus=gpu.run(meter.Get(),uniform(level),p,0,true)[0].r;
        p.compensation=-1;float minus=gpu.run(meter.Get(),uniform(level),p,0,true)[0].r;
        near(plus/base,2,1e-5,"auto +1 stop");near(minus/base,.5,1e-5,"auto -1 stop");
    }
    // Log-space exponential adaptation should be independent of frame rate.
    float adapted[3];int j=0;p.compensation=0;p.mapper=1;
    for(int hz : {30,60,144}) {p.dt=1.f/hz;float history=.0001f;for(int i=0;i<hz;i++)history=gpu.run(meter.Get(),uniform(1),p,history,true)[0].r;adapted[j++]=history;}
    near(adapted[0],adapted[1],1e-6,"30/60 Hz adaptation");near(adapted[1],adapted[2],1e-6,"60/144 Hz adaptation");
    // White furnace: L=1 everywhere -> Lambert gray card returns L*0.18, not pi*L*0.18.
    auto furnace=gpu.run(ibl.Get(),std::vector<Pixel>(4096,Pixel{1,1,1}),p)[0];
    near(furnace.r,.18,1e-6,"diffuse sky white furnace");
    for(double distance : {1.,3.,10.,30.,100.}) {
        std::vector<Pixel> samples(4096);
        for(int y=0;y<64;y++)for(int x=0;x<64;x++)samples[y*64+x]=Pixel{(x+.5f)/64,(y+.5f)/64,float(distance)};
        auto rect=gpu.run(rectangle.Get(),samples,p);double x_mean=0,x2_mean=0;
        double omega=4*std::atan(1/(distance*std::sqrt(distance*distance+2)));
        for(auto s:rect) {near(s.b,0,1e-5,"rectangle sample plane");
            if(std::abs(s.r)>1.0001||std::abs(s.g)>1.0001)throw std::runtime_error("sample outside emitter");
            near(s.a,omega,omega*.002,"rectangle solid angle");x_mean+=s.r/4096;x2_mean+=s.r*s.r/4096;}
        double reference_x2=0;
        for(int y=0;y<512;y++)for(int x=0;x<512;x++) {
            double px=-1+(x+.5)*2/512,py=-1+(y+.5)*2/512;
            reference_x2+=px*px*distance/std::pow(px*px+py*py+distance*distance,1.5)*4/(512*512)/omega;
        }
        near(x_mean,0,.002,"rectangle sample symmetry");near(x2_mean,reference_x2,.002,"rectangle sampling second moment");
    }
    auto coplanar=gpu.run(rectangle.Get(),std::vector<Pixel>(4096,Pixel{.5f,.5f,0}),p)[0];
    near(coplanar.a,0,1e-8,"coplanar rectangle solid angle");
    near(coplanar.r,0,1e-6,"coplanar finite sample");
    std::printf("PASS GPU gray %.6f; neutral/black all mappers; 16,384 GT7 reference colors; HDR parity max %.6f nits; reference max %.6f nits; exposure stops/adaptation; IBL white furnace; rectangle sampling\n",linear(gray.r),hdr_worst,reference_worst);
    return 0;
}
int main() {try {return run_tests();} catch(const std::exception& e) {std::fflush(stdout);std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
