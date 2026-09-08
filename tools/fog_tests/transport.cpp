#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include "../../data/shaders/shared_fog.h"
using Microsoft::WRL::ComPtr;
using namespace spartan::fog;
static void check(HRESULT r) { if (FAILED(r)) throw std::runtime_error("D3D11 operation failed"); }
static void near_value(double actual, double expected, double tolerance, const char* label)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
    {
        std::printf("FAIL %s: %.9g, expected %.9g (tolerance %.9g)\n", label, actual, expected, tolerance);
        throw std::runtime_error(label);
    }
}
int main()
{
    try
    {
        near_value(fog_slice_to_distance(0), 0, 0, "camera boundary");
        near_value(fog_slice_to_distance(fog_near_slices / fog_depth), fog_detail_far, 0.0001, "detail boundary");
        near_value(fog_slice_to_distance(1), fog_far, 0.01, "far boundary");
        float previous = -1;
        for (unsigned i = 0; i <= 16000; ++i)
        {
            float u = float(i) / 16000;
            float d = fog_slice_to_distance(u);
            if (d <= previous) throw std::runtime_error("grid is not monotone");
            near_value(fog_distance_to_slice(d), u, 2e-6, "CPU grid round trip");
            previous = d;
        }
        for (float sigma : {0.0f, 1e-9f, 0.0004f, 0.036f, 0.0675f, 0.2025f, 20.0f})
        for (float d : {0.0f, 0.01f, 0.2f, 1.0f, 40.0f, 6000.0f, 32000.0f})
        {
            double reference = sigma == 0 ? d : -std::expm1(-double(sigma) * d) / sigma;
            near_value(fog_segment_weight(sigma, d), reference, std::max(1e-7, reference * 5e-5), "CPU Beer integral");
            // A transparent interface must not change the homogeneous answer.
            double a = d * 0.37, b = d - a;
            double split = fog_segment_weight(sigma, float(a)) + std::exp(-sigma * a) * fog_segment_weight(sigma, float(b));
            near_value(split, reference, std::max(1e-7, reference * 8e-5), "split transport");
            double tau = double(sigma) * d;
            double centroid = tau < 0.001 ? d * (0.5 - tau / 12 + tau * tau * tau / 720)
                : 1 / double(sigma) - d * std::exp(-tau) / -std::expm1(-tau);
            near_value(fog_segment_centroid(sigma, d), centroid, std::max(1e-6, centroid * 0.0003), "CPU contribution centroid");
        }
        // A kilometre-wide cell must not absorb any water before the interface.
        for (float length : {0.1f, 2.0f, 100.0f, 1000.0f, 6000.0f})
        {
            float air_length = length * 0.4f;
            near_value(fog_layered_transmittance(0.0004f, 0.2025f, length, 0.6f, false, air_length),
                std::exp(-0.0004 * air_length), 3e-5, "no foreground water absorption");
        }
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level;
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, &level, &context));
        ComPtr<ID3DBlob> code, errors;
        HRESULT result = D3DCompileFromFile(L"tools/fog_tests/transport.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "main_cs", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (errors) std::fprintf(stderr, "%s", static_cast<const char*>(errors->GetBufferPointer()));
        check(result);
        ComPtr<ID3D11ComputeShader> shader;
        check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 12288 * 16;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = 16;
        ComPtr<ID3D11Buffer> output, staging;
        check(device->CreateBuffer(&desc, nullptr, &output));
        D3D11_UNORDERED_ACCESS_VIEW_DESC view = {};
        view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        view.Buffer.NumElements = 12288;
        ComPtr<ID3D11UnorderedAccessView> uav;
        check(device->CreateUnorderedAccessView(output.Get(), &view, &uav));
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        check(device->CreateBuffer(&desc, nullptr, &staging));
        context->CSSetShader(shader.Get(), nullptr, 0);
        ID3D11UnorderedAccessView* target = uav.Get();
        context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
        context->Dispatch(192, 1, 1);
        context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped;
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        const float* values = static_cast<const float*>(mapped.pData);
        for (unsigned i = 0; i < 4096; ++i)
        {
            double u = double(i) / 4095;
            double d = values[i * 4];
            double sigma = i == 0 ? 0 : std::exp2(-24.0 + double(i % 257) * 0.12);
            double integral = sigma == 0 ? d : -std::expm1(-sigma * d) / sigma;
            near_value(values[i * 4 + 1], u, 3e-6, "GPU grid round trip");
            near_value(values[i * 4 + 2], integral, std::max(2e-5, integral * 0.0002), "GPU integrated scattering");
            near_value(values[i * 4 + 3], std::exp(-sigma * d), 2e-5, "GPU integrated transmittance");
        }
        for (unsigned i = 0; i < 4096; ++i)
        {
            const float* row = values + (4096 + i) * 4;
            double length = row[0], partial = row[1];
            double fraction = double(i % 31) / 30;
            bool water_first = (i & 1) != 0;
            double first_length = length * (water_first ? fraction : 1 - fraction);
            double a = std::min(partial, first_length), b = std::max(partial - first_length, 0.0);
            double e0 = water_first ? 0.2025 : 0.0004, e1 = water_first ? 0.0004 : 0.2025;
            double s0 = water_first ? 0.008 : 0.00038, s1 = water_first ? 0.00038 : 0.008;
            double expected = s0 * -std::expm1(-e0 * a) / e0 + std::exp(-e0 * a) * s1 * -std::expm1(-e1 * b) / e1;
            near_value(row[2], expected, std::max(1e-6, expected * 0.0003), "GPU partial interface scattering");
            near_value(row[3], std::exp(-e0 * a - e1 * b), 3e-5, "GPU partial interface transmission");
        }
        for (unsigned i = 0; i < 4096; ++i)
        {
            const float* row = values + (8192 + i) * 4;
            double sigma = row[0], d = row[1], tau = sigma * d;
            double centroid = tau < 0.001 ? d * (0.5 - tau / 12 + tau * tau * tau / 720)
                : 1 / sigma - d * std::exp(-tau) / -std::expm1(-tau);
            near_value(row[2], centroid, std::max(1e-6, centroid * 0.0003), "GPU contribution centroid");
            near_value(row[3], -d * 0.3, std::max(1e-6, d * 1e-6), "GPU water interface reconstruction");
        }
        context->Unmap(staging.Get(), 0);
        std::puts("PASS 16001 grid cases, 152 CPU transport cases, 12288 GPU transport cases");
    }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
