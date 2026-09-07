// Copyright(c) 2015-2026 Panos Karabelas. Licensed under the MIT license.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spartan::terrain_habitat
{
    inline uint32_t hash(uint32_t x)
    {
        x = (x ^ (x >> 16)) * 0x7feb352du;
        x = (x ^ (x >> 15)) * 0x846ca68bu;
        return x ^ (x >> 16);
    }

    inline float smooth(float x)
    {
        x = std::clamp(x, 0.0f, 1.0f);
        return x*x*(3.0f-2.0f*x);
    }

    inline float noise(float x, float z, uint32_t seed)
    {
        const int32_t ix = static_cast<int32_t>(std::floor(x));
        const int32_t iz = static_cast<int32_t>(std::floor(z));
        const float u = smooth(x-ix), v = smooth(z-iz);
        const auto sample = [seed](int32_t a, int32_t b)
        {
            return static_cast<float>(hash(static_cast<uint32_t>(a)*73856093u ^ static_cast<uint32_t>(b)*19349663u ^ seed) & 0xffffu)/65535.0f;
        };
        const float a = sample(ix,iz)*(1-u)+sample(ix+1,iz)*u;
        const float b = sample(ix,iz+1)*(1-u)+sample(ix+1,iz+1)*u;
        return a*(1-v)+b*v;
    }

    // World-space fields span tile boundaries. All species in a habitat share
    // the same field; the scatter seed changes individuals, not the forest edge.
    inline float weight(uint32_t habitat, float x, float z)
    {
        if (habitat == 0) return 1.0f;
        const float broad = noise(x/430.0f,z/430.0f,71431u);
        const float edge = noise(x/95.0f,z/95.0f,11939u);
        const float forest = smooth((broad*.78f+edge*.22f-.40f)/.25f);
        if (habitat == 1) return forest; // woodland: groves with real open gaps
        if (habitat == 2) // olive groves occupy gentler open country (height/slope gates live in the layer)
            return (1.0f-forest)*smooth((noise(x/230.0f,z/230.0f,937u)-.32f)/.30f);
        if (habitat == 3) // scrub ties the forest fringe to patches of open ground
            return (.30f+.70f*(1.0f-forest))*smooth((noise(x/70.0f,z/70.0f,521u)-.25f)/.40f);
        if (habitat == 4) // broken rock bands, further constrained by mineral/slope rules
            return .15f+.85f*smooth((noise(x/170.0f,z/300.0f,2081u)-.30f)/.40f);
        return 1.0f;
    }

    inline uint32_t variant(uint32_t tile, uint32_t instance, uint32_t seed, uint32_t count)
    {
        return count ? hash(tile*1000003u ^ instance*7919u ^ seed*31u) % count : 0;
    }
}
