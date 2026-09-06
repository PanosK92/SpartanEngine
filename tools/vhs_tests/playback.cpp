// Runs the production signal functions on D3D11 without opening an engine/window.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
struct Pixel { float r, g, b, a = 1; };
struct Params { float time, hdr = 0, white = 203; unsigned mode = 0; };
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void hr(HRESULT result) { if (FAILED(result)) { char s[64]; std::snprintf(s, sizeof(s), "HRESULT 0x%08lx", result); throw std::runtime_error(s); } }

struct Playback
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<IWICImagingFactory> wic;
    Playback()
    {
        hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
        D3D_FEATURE_LEVEL feature;
        HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                          D3D11_SDK_VERSION, &device, &feature, &context);
        if (FAILED(result)) hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                                D3D11_SDK_VERSION, &device, &feature, &context));
        ComPtr<ID3DBlob> code, errors;
        result = D3DCompileFromFile(L"tools/vhs_tests/dispatch.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    "main_cs", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,
                                    0, &code, &errors);
        if (errors) std::fprintf(stderr, "%s", static_cast<const char*>(errors->GetBufferPointer()));
        hr(result);
        hr(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
        D3D11_BUFFER_DESC cb = {}; cb.ByteWidth = sizeof(Params); cb.Usage = D3D11_USAGE_DEFAULT; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr(device->CreateBuffer(&cb, nullptr, &constants));
        D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr(device->CreateSamplerState(&sd, &sampler));
    }
    std::vector<Pixel> run(const std::vector<Pixel>& input, unsigned width, unsigned height, Params params)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = { input.data(), width * sizeof(Pixel), 0 };
        ComPtr<ID3D11Texture2D> source, result, readback;
        hr(device->CreateTexture2D(&desc, &data, &source));
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        hr(device->CreateTexture2D(&desc, nullptr, &result));
        desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr(device->CreateTexture2D(&desc, nullptr, &readback));
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;
        hr(device->CreateShaderResourceView(source.Get(), nullptr, &srv));
        hr(device->CreateUnorderedAccessView(result.Get(), nullptr, &uav));
        context->UpdateSubresource(constants.Get(), 0, nullptr, &params, 0, 0);
        context->CSSetShader(shader.Get(), nullptr, 0);
        context->CSSetShaderResources(0, 1, srv.GetAddressOf());
        context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
        context->CSSetSamplers(0, 1, sampler.GetAddressOf());
        context->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
        context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        context->CopyResource(readback.Get(), result.Get());
        D3D11_MAPPED_SUBRESOURCE map;
        hr(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &map));
        std::vector<Pixel> output(width * height);
        for (unsigned y = 0; y < height; ++y)
            std::memcpy(output.data() + y * width, static_cast<const char*>(map.pData) + y * map.RowPitch, width * sizeof(Pixel));
        context->Unmap(readback.Get(), 0);
        ID3D11ShaderResourceView* null_srv = nullptr;
        ID3D11UnorderedAccessView* null_uav = nullptr;
        context->CSSetShaderResources(0, 1, &null_srv);
        context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
        return output;
    }
    std::vector<Pixel> load(const wchar_t* path, unsigned width, unsigned height)
    {
        ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICBitmapScaler> scaler; ComPtr<IWICFormatConverter> converter;
        hr(wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder));
        hr(decoder->GetFrame(0, &frame)); hr(wic->CreateBitmapScaler(&scaler));
        hr(scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant));
        hr(wic->CreateFormatConverter(&converter));
        hr(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        std::vector<unsigned char> bytes(width * height * 4);
        hr(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(bytes.size()), bytes.data()));
        std::vector<Pixel> result(width * height);
        for (size_t i = 0; i < result.size(); ++i) result[i] = { bytes[i*4]/255.f, bytes[i*4+1]/255.f, bytes[i*4+2]/255.f, 1 };
        return result;
    }
    void save(const wchar_t* path, const std::vector<Pixel>& pixels, unsigned width, unsigned height)
    {
        ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
        hr(wic->CreateStream(&stream)); hr(stream->InitializeFromFilename(path, GENERIC_WRITE));
        hr(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)); hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        hr(encoder->CreateNewFrame(&frame, nullptr)); hr(frame->Initialize(nullptr)); hr(frame->SetSize(width, height));
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR; hr(frame->SetPixelFormat(&format));
        std::vector<unsigned char> bytes(width * height * 3);
        auto byte = [](float f) { return static_cast<unsigned char>(std::clamp(f, 0.f, 1.f) * 255.f + .5f); };
        for (size_t i = 0; i < pixels.size(); ++i) { bytes[i*3] = byte(pixels[i].b); bytes[i*3+1] = byte(pixels[i].g); bytes[i*3+2] = byte(pixels[i].r); }
        hr(frame->WritePixels(height, width * 3, static_cast<UINT>(bytes.size()), bytes.data())); hr(frame->Commit()); hr(encoder->Commit());
    }
};

int main()
{
    try
    {
        std::filesystem::create_directories("binaries/vhs_tests/sequence");
        Playback playback;
        const float rate = 60000.f / 1001.f;
        unsigned w = 720, h = 480;
        std::vector<Pixel> gray(w * h, {.5f, .5f, .5f, 1});
        auto first = playback.run(gray, w, h, {100.1f / rate});
        auto same = playback.run(gray, w, h, {100.8f / rate});
        auto next = playback.run(gray, w, h, {101.1f / rate});
        check(std::memcmp(first.data(), same.data(), first.size() * sizeof(Pixel)) == 0, "noise changes inside a held video field");
        check(std::memcmp(first.data(), next.data(), first.size() * sizeof(Pixel)) != 0, "noise does not evolve across fields");
        double mean = 0;
        for (unsigned y = 20; y < 450; ++y) for (unsigned x = 20; x < w-20; ++x)
        {
            Pixel p = first[y*w+x]; mean += (p.r + p.g + p.b) / 3;
            check(std::isfinite(p.r) && std::isfinite(p.g) && std::isfinite(p.b) && p.a == 1, "invalid output");
            check(p.r >= 0 && p.r <= 1 && p.g >= 0 && p.g <= 1 && p.b >= 0 && p.b <= 1, "out of range SDR output");
        }
        mean /= 430 * (w-40);
        check(std::abs(mean - .5) < .005, "gray level changed by a grade or scanline mask");
        std::puts("PASS held 59.94 Hz field timing, evolving noise, gray-level preservation and finite output");

        // Independent brightness and isoluminant colour patterns at the same frequency.
        auto pattern = gray;
        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x)
        {
            float wave = std::sin(float(x) * 6.2831853f / 16.f);
            pattern[y*w+x] = y < 240 ? Pixel{.5f+.2f*wave, .5f+.2f*wave, .5f+.2f*wave, 1}
                : Pixel{.5f+.956f*.16f*wave, .5f-.272f*.16f*wave, .5f-1.106f*.16f*wave, 1};
        }
        auto filtered = playback.run(pattern, w, h, {100.1f / rate});
        double luma_energy = 0, chroma_energy = 0;
        for (unsigned x = 30; x < w-30; ++x)
        {
            Pixel a = filtered[100*w+x], b = filtered[340*w+x];
            float ya = .299f*a.r+.587f*a.g+.114f*a.b-.5f;
            float ib = .596f*b.r-.274f*b.g-.322f*b.b;
            luma_energy += ya*ya; chroma_energy += ib*ib;
        }
        check(luma_energy > 5 && chroma_energy < luma_energy * .04, "chroma bandwidth is not substantially below luma");
        playback.save(L"binaries/vhs_tests/bandwidth_input.png", pattern, w, h);
        playback.save(L"binaries/vhs_tests/bandwidth_output.png", filtered, w, h);
        std::printf("PASS separate luma/chroma bandwidth; energy ratio %.5f\n", chroma_energy/luma_energy);

        unsigned tracking_fields = 0;
        std::vector<Pixel> column(480, {.5f,.5f,.5f,1});
        for (unsigned field = 0; field < 600; ++field)
        {
            auto transport = playback.run(column, 1, 480, {(field+.1f)/rate, 0, 203, 1});
            bool tracking = false;
            for (unsigned y = 0; y < 480; ++y)
            {
                const Pixel p = transport[y];
                check(y >= 474 || p.b == 0, "head switching escaped the bottom six lines");
                check(y < 477 || p.b == 1, "missing head switching interval");
                if (p.g == 0 && p.b == 0) check(std::abs(p.r) < .8f, "healthy transport has excessive picture displacement");
                tracking |= p.g > 0;
            }
            tracking_fields += tracking;
        }
        check(tracking_fields > 0 && tracking_fields < 40, "tracking damage is missing or too persistent");
        std::printf("PASS 600 transport fields; %u brief tracking fields; head switching confined to the bottom\n", tracking_fields);

        auto low = playback.run(gray, 720, 480, {2,0,203,1});
        std::vector<Pixel> high_input(1440 * 960, {.5f,.5f,.5f,1});
        auto high = playback.run(high_input, 1440, 960, {2,0,203,1});
        for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 720; ++x)
            check(std::memcmp(&low[y*720+x], &high[(y*2)*1440+x*2], sizeof(Pixel)) == 0, "transport depends on output pixels");
        auto odd = playback.run(std::vector<Pixel>(721*481, {.5f,.5f,.5f,1}), 721, 481, {2});
        check(odd.back().a == 1 && std::isfinite(odd.back().r), "partial dispatch group was not covered");
        std::puts("PASS resolution-independent transport and non-multiple-of-eight dispatch size");

        for (float mode : {1.f, 2.f})
        {
            auto encoded = playback.run(gray, w, h, {0,mode,203,2});
            auto decoded = playback.run(encoded, w, h, {0,mode,203,3});
            check(std::abs(decoded[w*100+100].r - .5f) < .0002f, "HDR transfer round trip failed");
            auto white = playback.run(std::vector<Pixel>(1, {1,1,1,1}), 1, 1, {0,mode,203,2});
            const float expected = mode == 2 ? 203.f/80.f : .5806889f; // ST 2084 at 203 cd/m2
            check(std::abs(white[0].r - expected) < .0002f, "HDR paper-white luminance incorrect");
            auto hdr_filtered = playback.run(encoded, w, h, {100.1f/rate,mode,203,0});
            auto hdr_sdr = playback.run(hdr_filtered, w, h, {0,mode,203,3});
            check(std::abs(hdr_sdr[w*100+100].r - first[w*100+100].r) < .001f, "HDR and SDR tape processing differ");
        }
        std::puts("PASS HDR10 and scRGB paper white, round trip and equivalent signal processing");

        if (std::filesystem::exists("binaries/screenshot_0.png"))
        {
            w = 1280; h = 626;
            auto room = playback.load(L"binaries/screenshot_0.png", w, h);
            playback.save(L"binaries/vhs_tests/backrooms_input.png", room, w, h);
            for (unsigned field = 0; field < 90; ++field)
            {
                auto frame = playback.run(room, w, h, {(field+.1f)/rate});
                wchar_t path[160]; std::swprintf(path, 160, L"binaries/vhs_tests/sequence/frame_%03u.png", field);
                playback.save(path, frame, w, h);
                if (field == 30) playback.save(L"binaries/vhs_tests/backrooms_vhs.png", frame, w, h);
            }
            std::puts("PASS rendered 90 consecutive Backrooms playback fields to binaries/vhs_tests/sequence");
        }
        return 0;
    }
    catch (const std::exception& e) { std::fprintf(stderr, "FAIL %s\n", e.what()); return 1; }
}
