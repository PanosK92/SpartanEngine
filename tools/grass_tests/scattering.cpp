// Exercise the production HLSL math directly, not a copy of its implementation.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using std::max;
float saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
#include "../../data/shaders/subsurface_scattering.hlsl"

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    constexpr double pi = 3.141592653589793;
    constexpr int samples = 20000;
    double integral = 0;
    for (int i = 0; i < samples; ++i)
    {
        float nl = -1.0f + (static_cast<float>(i) + 0.5f) * (2.0f / samples);
        integral += subsurface_wrapped_diffuse(nl) * (4.0 * pi / samples);
    }
    require(std::abs(integral - 1.0) < 1e-5, "Wrapped scattering must integrate to one");

    // Uniform illumination may not be amplified for any camera angle, including
    // grazing views, or any scattering fraction. Integrate over the light sphere.
    for (int view_step = 0; view_step <= 10; ++view_step)
    {
        float nv = static_cast<float>(view_step) / 10.0f;
        float vx = std::sqrt(1.0f - nv * nv);
        for (bool thin : {false, true})
        {
            double irradiance = 0;
            for (int i = 0; i < 400; ++i)
            {
                float nl = -1.0f + (static_cast<float>(i) + 0.5f) / 200.0f;
                float lx = std::sqrt(1.0f - nl * nl);
                for (int azimuth = 0; azimuth < 128; ++azimuth)
                {
                    float phase = static_cast<float>(2.0 * pi * (azimuth + 0.5) / 128);
                    float v_minus_l = -(nv * nl + vx * lx * std::cos(phase));
                    float response = subsurface_diffuse_response(nl, v_minus_l, thin);
                    require(std::isfinite(response) && response >= 0, "Finite, nonnegative scattering");
                    irradiance += response * (4.0 * pi / (400 * 128));
                    float delta = subsurface_diffuse_response(nl + 1e-5f, v_minus_l, thin) - response;
                    require(std::abs(delta) < 1e-4f, "Continuous response through the terminator");
                }
            }
            require(irradiance <= 1.00001, "Scattering must not amplify a uniform light field");
        }
    }
    require(subsurface_diffuse_response(-1, 1, false) == 0, "Solid surface must not transmit through its back");
    require(subsurface_diffuse_response(-1, 1, true) > 0, "Thin leaves must transmit backlighting");
    require(subsurface_diffuse_response(-1, 1, true) > subsurface_diffuse_response(-1, 0, true), "Forward transmission should be stronger");
    std::cout << "PASS scattering: normalization, bounded energy, all view angles, continuity, thin/solid separation\n";
}
