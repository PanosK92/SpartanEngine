// Production bloom shaders, executed over real per-mip GPU views.
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
struct Pixel {float r=0,g=0,b=0,a=1;};
void check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
void hr(HRESULT r){if(FAILED(r)){char s[64];std::snprintf(s,sizeof(s),"HRESULT 0x%08lx",r);throw std::runtime_error(s);}}
struct Texture {
    unsigned width=0,height=0;
    DXGI_FORMAT format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    ComPtr<ID3D11Texture2D> resource;
    std::vector<ComPtr<ID3D11ShaderResourceView>> srv;
    std::vector<ComPtr<ID3D11UnorderedAccessView>> uav;
};
struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    Gpu(){
        D3D_FEATURE_LEVEL level;
        hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));
        D3D11_BUFFER_DESC b={};b.ByteWidth=16;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr(device->CreateBuffer(&b,nullptr,&constants));
        D3D11_SAMPLER_DESC s={};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
        hr(device->CreateSamplerState(&s,&sampler));
        context->CSSetSamplers(0,1,sampler.GetAddressOf());
    }
    ComPtr<ID3D11ComputeShader> compile(const wchar_t* path,const char* stage){
        D3D_SHADER_MACRO macros[]={{stage,"1"},{nullptr,nullptr}};
        ComPtr<ID3DBlob> code,errors;HRESULT r=D3DCompileFromFile(path,macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main_cs","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors)std::fprintf(stderr,"%s",static_cast<const char*>(errors->GetBufferPointer()));hr(r);
        ComPtr<ID3D11ComputeShader> shader;hr(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));return shader;
    }
    Texture texture(unsigned w,unsigned h,unsigned levels=1,DXGI_FORMAT format=DXGI_FORMAT_R32G32B32A32_FLOAT){
        Texture t;t.width=w;t.height=h;t.format=format;
        D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=levels;d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;hr(device->CreateTexture2D(&d,nullptr,&t.resource));
        for(unsigned i=0;i<levels;i++){
            D3D11_SHADER_RESOURCE_VIEW_DESC s={};s.Format=format;s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;s.Texture2D.MostDetailedMip=i;s.Texture2D.MipLevels=1;
            ComPtr<ID3D11ShaderResourceView> srv;hr(device->CreateShaderResourceView(t.resource.Get(),&s,&srv));t.srv.push_back(srv);
            D3D11_UNORDERED_ACCESS_VIEW_DESC u={};u.Format=format;u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;u.Texture2D.MipSlice=i;
            ComPtr<ID3D11UnorderedAccessView> uav;hr(device->CreateUnorderedAccessView(t.resource.Get(),&u,&uav));t.uav.push_back(uav);
        }return t;
    }
    void upload(Texture& t,const std::vector<Pixel>& pixels){
        check(pixels.size()==size_t(t.width)*t.height,"input size");
        context->UpdateSubresource(t.resource.Get(),0,nullptr,pixels.data(),t.width*sizeof(Pixel),0);
    }
    void run(ID3D11ComputeShader* shader,Texture& input,unsigned inputMip,Texture& output,unsigned outputMip,float x,Texture* second=nullptr){
        // Clear previous SRV/UAV bindings before rebinding the same resource's mips.
        ID3D11ShaderResourceView* empty[2]={};ID3D11UnorderedAccessView* noUav=nullptr;
        context->CSSetShaderResources(0,2,empty);context->CSSetUnorderedAccessViews(0,1,&noUav,nullptr);
        float p[]={x,0,0,.001f};context->UpdateSubresource(constants.Get(),0,nullptr,p,0,0);
        context->CSSetConstantBuffers(0,1,constants.GetAddressOf());context->CSSetShader(shader,nullptr,0);
        ID3D11ShaderResourceView* views[]={input.srv[inputMip].Get(),second?second->srv[0].Get():nullptr};
        context->CSSetShaderResources(0,2,views);context->CSSetUnorderedAccessViews(0,1,output.uav[outputMip].GetAddressOf(),nullptr);
        context->Dispatch((std::max(1u,output.width>>outputMip)+7)/8,(std::max(1u,output.height>>outputMip)+7)/8,1);
        context->CSSetShaderResources(0,2,empty);context->CSSetUnorderedAccessViews(0,1,&noUav,nullptr);
    }
    std::vector<Pixel> read(Texture& t,unsigned mip=0){
        unsigned w=std::max(1u,t.width>>mip),h=std::max(1u,t.height>>mip);
        D3D11_TEXTURE2D_DESC d={};t.resource->GetDesc(&d);d.Width=w;d.Height=h;d.MipLevels=1;d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;hr(device->CreateTexture2D(&d,nullptr,&staging));context->CopySubresourceRegion(staging.Get(),0,0,0,0,t.resource.Get(),mip,nullptr);
        D3D11_MAPPED_SUBRESOURCE map;hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));std::vector<Pixel> pixels(size_t(w)*h);
        for(unsigned y=0;y<h;y++){
            const char* row=static_cast<const char*>(map.pData)+map.RowPitch*y;
            if(t.format==DXGI_FORMAT_R32G32B32A32_FLOAT)std::memcpy(pixels.data()+y*w,row,w*sizeof(Pixel));
            else for(unsigned x=0;x<w;x++){const HALF* c=reinterpret_cast<const HALF*>(row)+x*4;pixels[y*w+x]={XMConvertHalfToFloat(c[0]),XMConvertHalfToFloat(c[1]),XMConvertHalfToFloat(c[2]),XMConvertHalfToFloat(c[3])};}
        }context->Unmap(staging.Get(),0);return pixels;
    }
};
struct Bloom {
    Gpu& gpu;Texture input,pyramid,output;
    ComPtr<ID3D11ComputeShader> pre,down,up,blend;
    unsigned levels=1;bool baseline;
    Bloom(Gpu& g,unsigned w,unsigned h,bool old=false,bool half=false):gpu(g),baseline(old){
        unsigned bw=std::max(1u,w/2),bh=std::max(1u,h/2);
        for(unsigned edge=std::min(bw,bh);edge>=(old?64u:16u);edge/=2)levels++;
        input=g.texture(w,h);output=g.texture(w,h);pyramid=g.texture(bw,bh,levels,half?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT);
        const wchar_t* source=old?L"binaries/bloom_tests/baseline_fixture.hlsl":L"binaries/bloom_tests/bloom_fixture.hlsl";
        pre=g.compile(source,old?"LUMINANCE":"PREFILTER");up=g.compile(source,"UPSAMPLE_BLEND_MIP");blend=g.compile(source,"BLEND_FRAME");
        down=g.compile(old?L"binaries/bloom_tests/box_fixture.hlsl":source,"DOWNSAMPLE");
    }
    void dispatch(float intensity=1,float scatter=.7f){
        gpu.run(pre.Get(),input,0,pyramid,0,0);
        for(unsigned i=1;i<levels;i++)gpu.run(down.Get(),pyramid,i-1,pyramid,i,0);
        for(unsigned i=levels-1;i>0;i--)gpu.run(up.Get(),pyramid,i,pyramid,i-1,scatter);
        gpu.run(blend.Get(),input,0,output,0,intensity,&pyramid);
    }
    std::vector<Pixel> render(const std::vector<Pixel>& pixels,float intensity=1,float scatter=.7f){gpu.upload(input,pixels);dispatch(intensity,scatter);return gpu.read(output);}
};
double energy(const std::vector<Pixel>& pixels){double sum=0;for(auto p:pixels){check(std::isfinite(p.r)&&std::isfinite(p.g)&&std::isfinite(p.b),"nonfinite output");check(std::min({p.r,p.g,p.b})>=-1e-6,"negative output");sum+=p.r;}return sum;}
void close(double a,double b,double tolerance,const char* label){if(!std::isfinite(a)||std::abs(a-b)>tolerance){std::printf("FAIL %s: %.9g expected %.9g tolerance %.5g\n",label,a,b,tolerance);throw std::runtime_error(label);}}
std::vector<Pixel> point(unsigned size,float x,float value=10000){
    std::vector<Pixel> p(size*size);int ix=int(x);float f=x-ix;
    p[size/2*size+ix]={value*(1-f),0,0,1};p[size/2*size+ix+1]={value*f,0,0,1};return p;
}
void save(const char* path,unsigned w,unsigned h,const std::vector<Pixel>& pixels,float gain){
    BITMAPFILEHEADER f={};BITMAPINFOHEADER i={};unsigned stride=(w*3+3)&~3u;
    f.bfType=0x4d42;f.bfOffBits=sizeof(f)+sizeof(i);f.bfSize=f.bfOffBits+stride*h;i.biSize=sizeof(i);i.biWidth=w;i.biHeight=-int(h);i.biPlanes=1;i.biBitCount=24;i.biCompression=BI_RGB;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(&f),sizeof(f));out.write(reinterpret_cast<char*>(&i),sizeof(i));
    std::vector<unsigned char> row(stride);
    auto encode=[gain](float v){v=std::max(v*gain,0.f);v=v/(1+v);v=v<=.0031308f?12.92f*v:1.055f*std::pow(v,1/2.4f)-.055f;return static_cast<unsigned char>(std::clamp(v*255.f,0.f,255.f));};
    for(unsigned y=0;y<h;y++){for(unsigned x=0;x<w;x++){auto p=pixels[y*w+x];row[x*3]=encode(p.b);row[x*3+1]=encode(p.g);row[x*3+2]=encode(p.r);}out.write(reinterpret_cast<char*>(row.data()),stride);}
}
void movement(Gpu& gpu,bool baseline){
    constexpr unsigned n=256;Bloom bloom(gpu,n,n,baseline,true);std::vector<double> energies;
    for(unsigned step=0;step<=128;step++){
        auto p=point(n,96+step/8.f);bloom.render(p);auto glow=gpu.read(bloom.pyramid);energies.push_back(energy(glow)*4);
    }
    auto [lo,hi]=std::minmax_element(energies.begin(),energies.end());double mean=std::accumulate(energies.begin(),energies.end(),0.)/energies.size();
    std::printf("%s moving highlight: halo energy range %.6f%% of mean, min %.6f max %.6f\n",baseline?"BEFORE":"AFTER",(*hi-*lo)/mean*100,*lo,*hi);
    if(!baseline)check((*hi-*lo)/mean<.005,"moving bloom energy varies more than 0.5%");
    // Linearity at subpixel positions: no coverage-dependent nonlinear weighting.
    auto a=bloom.render(point(n,127));auto b=bloom.render(point(n,128));auto half=bloom.render(point(n,127.5f));
    double difference=0,total=0;
    for(size_t i=0;i<half.size();i++){difference+=std::abs(half[i].r-(a[i].r+b[i].r)*.5);total+=half[i].r;}
    std::printf("%s subpixel linearity error %.6f%%\n",baseline?"BEFORE":"AFTER",difference/total*100);
    if(!baseline)check(difference/total<.0001,"subpixel response is nonlinear");
}
void timing(Gpu& gpu,unsigned w,unsigned h){
    Bloom bloom(gpu,w,h,false,true);gpu.upload(bloom.input,std::vector<Pixel>(size_t(w)*h,Pixel{10,2,1,1}));for(int i=0;i<8;i++)bloom.dispatch();
    D3D11_QUERY_DESC d={D3D11_QUERY_TIMESTAMP_DISJOINT,0};ComPtr<ID3D11Query> disjoint,start,end;hr(gpu.device->CreateQuery(&d,&disjoint));d.Query=D3D11_QUERY_TIMESTAMP;hr(gpu.device->CreateQuery(&d,&start));hr(gpu.device->CreateQuery(&d,&end));
    gpu.context->Begin(disjoint.Get());gpu.context->End(start.Get());for(int i=0;i<32;i++)bloom.dispatch();gpu.context->End(end.Get());gpu.context->End(disjoint.Get());gpu.context->Flush();
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data={};while(gpu.context->GetData(disjoint.Get(),&data,sizeof(data),0)==S_FALSE)Sleep(1);
    UINT64 a,b;hr(gpu.context->GetData(start.Get(),&a,sizeof(a),0));hr(gpu.context->GetData(end.Get(),&b,sizeof(b),0));check(!data.Disjoint,"disjoint GPU timer");
    std::printf("GPU %ux%u FP16 bloom: %.4f ms (D3D11 harness, 32 chains)\n",w,h,(b-a)*1000./data.Frequency/32);
}
void chromaticity(Gpu& gpu){
    constexpr unsigned n=256;Bloom bloom(gpu,n,n,false,true);double worst=0;
    // The showroom's actual warm, red and cyan light chromaticities. Measure the
    // halo itself before tone mapping, including the low-energy outer levels.
    for(Pixel color:std::vector<Pixel>{{1,.653803825f,.342766613f,1},{1,.25f,.35f,1},{.3f,.75f,1,1}})
    for(float strength:{1.f,1000.f,60000.f}){
        auto input=point(n,127.375f,strength);
        for(auto& p:input){float value=p.r;p.r=value*color.r;p.g=value*color.g;p.b=value*color.b;}
        bloom.render(input);auto halo=gpu.read(bloom.pyramid);energy(halo);
        double reference_sum=0,error_r=0,error_g=0,error_b=0;
        for(auto p:halo){
            // The tested colors have a unit peak channel. Use that channel so
            // the metric does not amplify quantization by dividing by weak red.
            double reference=color.r==1?p.r:p.b;
            reference_sum+=reference;
            error_r+=std::abs(p.r-reference*color.r);
            error_g+=std::abs(p.g-reference*color.g);
            error_b+=std::abs(p.b-reference*color.b);
        }
        double error=std::max({error_r,error_g,error_b});
        // The dim-point tails reach FP16 subnormals. Allow one absolute half-float
        // subnormal step per texel per filtering level, plus 0.2% relative error.
        double subnormal_budget=halo.size()*bloom.levels*std::ldexp(1.0,-24);
        check(error<=.002*reference_sum+subnormal_budget,"bloom halo lost source chromaticity");
        if(strength>=1000)worst=std::max(worst,error/reference_sum);
    }
    std::printf("PASS warm/red/cyan halo chromaticity (FP16 precision), worst HDR channel-ratio error %.6f%%\n",worst*100);
}
int tests(){
    Gpu gpu;
    for(auto [w,h]:std::vector<std::pair<unsigned,unsigned>>{{1,1},{3,1},{31,19},{127,73},{256,256},{801,451}}){
        Bloom bloom(gpu,w,h,false,true);
        for(float value:{0.f,.18f,32.f,60000.f}){
            Pixel color={value,value*.5f,value*.25f,.37f};auto input=std::vector<Pixel>(size_t(w)*h,color);auto output=bloom.render(input);
            energy(output);for(auto p:output){close(p.r,color.r,std::max(1e-6f,value*.0003f),"DC red");close(p.g,color.g,std::max(1e-6f,value*.0003f),"DC green");close(p.a,.37,1e-6,"alpha preservation");}
            auto disabled=bloom.render(input,0);for(size_t i=0;i<input.size();i++)close(disabled[i].r,input[i].r,0,"disabled identity");
        }
        auto black=bloom.render(std::vector<Pixel>(size_t(w)*h));close(energy(black),0,0,"no history or stale tiles");
    }
    std::puts("PASS DC gain, HDR range, alpha, disable, stale-frame reset, tiny/odd dimensions");
    movement(gpu,false);
    chromaticity(gpu);
    if(std::filesystem::exists("binaries/bloom_tests/baseline_fixture.hlsl"))movement(gpu,true);
    // Colored lamps, a thin white tube, and a small specular highlight.
    const unsigned w=768,h=432;std::vector<Pixel> scene(w*h);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        if(std::hypot(float(x)-180,float(y)-205)<18)scene[y*w+x]={400,40,3,1};
        if(x>360&&x<365&&y>110&&y<315)scene[y*w+x]={2000,2000,2000,1};
        if(std::hypot(float(x)-565,float(y)-205)<3)scene[y*w+x]={10000,3500,300,1};
    }
    Bloom bloom(gpu,w,h,false,true);save("binaries/bloom_tests/after.bmp",w,h,bloom.render(scene),.03f);
    if(std::filesystem::exists("binaries/bloom_tests/baseline_fixture.hlsl")){Bloom old(gpu,w,h,true,true);save("binaries/bloom_tests/before.bmp",w,h,old.render(scene),.03f);}
    timing(gpu,1920,1080);timing(gpu,3840,2160);
    std::puts("PASS bloom GPU regression suite");return 0;
}
int main(){try{return tests();}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
