/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "CarDirt.h"
#include "CarRain.h"
#include "../world/Entity.h"
#include "../profiling/Profiler.h"
//=============================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        constexpr float wet_seconds         = 150.0f; // a soaked deposit stays wet this long on the dirt clock, a damp one less
        constexpr float find_tolerance      = 0.025f; // metres a stamped point may sit off the paint, curvature under a speck
        constexpr uint32_t dust_grains_max  = 20000;  // per frame
        constexpr int32_t stamp_reach_max   = 6;      // texels from the centre of one speck

        uint32_t rain_version = ~0u;
        uint32_t dirt_version = 0;
        Entity* owner         = nullptr;
        float clock           = 0.0f;
        bool active           = false;
        bool dirty            = false;
        float dust_remainder  = 0.0f;
        uint64_t rng_state    = 0x853c49e6748fea9bull;

        vector<uint32_t> color;        // per texel, rgb sqrt encoded albedo and coverage, 8 bits each
        vector<uint32_t> state;        // per texel, wet until in 1/16 s of the clock << 8 | coarseness
        vector<int32_t> pending_slot;  // where the texel's change waits in pending
        vector<SurfaceDirtTexel> pending;
        vector<SurfaceDirtTexel> frame;

        // pcg32, a texel is picked by one draw and kept by the next, a weaker generator correlates the two into stripes along the atlas rows
        float random01()
        {
            const uint64_t old = rng_state;
            rng_state          = old * 6364136223846793005ull + 1442695040888963407ull;
            const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
            const uint32_t rot        = static_cast<uint32_t>(old >> 59u);
            const uint32_t r          = (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
            return static_cast<float>(r >> 8) / 16777216.0f;
        }

        float random_gauss()
        {
            return (random01() + random01() + random01() - 1.5f) * 2.0f;
        }

        // 8 bits with a random rounding, a thousand faint grains still add up instead of rounding away
        uint32_t quantize(float x)
        {
            return static_cast<uint32_t>(clamp(floorf(x * 255.0f + random01()), 0.0f, 255.0f));
        }

        // follows the rain atlas, a new bake or a new car starts clean
        bool sync(Entity* root)
        {
            if (!root || root != CarRain::GetBakedRoot())
                return false;

            const uint32_t texel_count = CarRain::GetTexelCount();
            if (CarRain::GetVersion() != rain_version || owner != root || color.size() != texel_count)
            {
                rain_version = CarRain::GetVersion();
                owner        = root;
                color.assign(texel_count, 0u);
                state.assign(texel_count, 0u);
                pending_slot.assign(texel_count, -1);
                pending.clear();
                frame.clear();
                dirty = false;
                dirt_version++;
            }
            return texel_count > 0;
        }

        void deposit(uint32_t texel, const Vector3& albedo, float amount, float moisture, float coarseness)
        {
            if (texel >= color.size() || amount <= 0.0f)
                return;

            // blend in linear by how much of the texel the new material makes up
            const uint32_t c    = color[texel];
            const float r       = static_cast<float>(c & 0xff) / 255.0f;
            const float g       = static_cast<float>((c >> 8) & 0xff) / 255.0f;
            const float b       = static_cast<float>((c >> 16) & 0xff) / 255.0f;
            const float covered = static_cast<float>(c >> 24) / 255.0f;
            const float share   = amount / max(covered + amount, 1e-4f);
            const Vector3 mixed = Vector3(r * r, g * g, b * b) * (1.0f - share) + albedo * share;
            color[texel] = quantize(sqrtf(max(mixed.x, 0.0f))) | (quantize(sqrtf(max(mixed.y, 0.0f))) << 8) | (quantize(sqrtf(max(mixed.z, 0.0f))) << 16) | (quantize(min(1.0f, covered + amount)) << 24);

            // fresh slurry wets what it lands on again, a dry grain never dries what is still wet
            const uint32_t s        = state[texel];
            const float coarse      = static_cast<float>(s & 0xff) / 255.0f;
            const uint32_t wet_old  = s >> 8;
            const uint32_t wet_new  = static_cast<uint32_t>(min((clock + moisture * wet_seconds) * 16.0f, 16777215.0f));
            state[texel] = (max(wet_old, moisture > 0.0f ? wet_new : 0u) << 8) | quantize(coarse + (coarseness - coarse) * share);

            dirty = true;
            SurfaceDirtTexel change;
            change.texel = texel;
            change.color = color[texel];
            change.state = state[texel];
            if (pending_slot[texel] >= 0)
            {
                pending[pending_slot[texel]] = change;
            }
            else
            {
                pending_slot[texel] = static_cast<int32_t>(pending.size());
                pending.push_back(change);
            }
        }

        // one speck, an ellipse on the paint stretched along t, its rim broken so no two specks share an outline
        void stamp(const Vector3& centre, const Vector3& n, const Vector3& t, const Vector3& b, float radius, float elongation, float amount, const CarDirt::Deposit& d)
        {
            const float s = CarRain::GetTexelSize();
            uint32_t texel;
            if (radius < s * 0.6f)
            {
                // smaller than a texel, it hides its share of it
                if (CarRain::FindTexel(centre, n, find_tolerance, texel))
                {
                    const float share = min(1.0f, (radius * radius * elongation) / (s * s * 0.36f));
                    deposit(texel, d.color, amount * share, d.moisture, d.coarseness);
                }
                return;
            }

            const float along   = radius * elongation;
            const int32_t reach_u = min(stamp_reach_max, static_cast<int32_t>(ceilf(along / s)));
            const int32_t reach_v = min(stamp_reach_max, static_cast<int32_t>(ceilf(radius / s)));
            const float wobble  = random01() * 6.2831853f;
            for (int32_t j = -reach_v; j <= reach_v; j++)
            {
                for (int32_t i = -reach_u; i <= reach_u; i++)
                {
                    const float u   = static_cast<float>(i) * s;
                    const float v   = static_cast<float>(j) * s;
                    const float e   = sqrtf((u / along) * (u / along) + (v / radius) * (v / radius));
                    const float rim = 0.78f + 0.12f * sinf(atan2f(v, u) * 5.0f + wobble) + 0.1f * random01();
                    if (e > rim)
                        continue;

                    if (CarRain::FindTexel(centre + t * u + b * v, n, find_tolerance, texel))
                    {
                        deposit(texel, d.color, amount * (1.0f - 0.45f * e * e), d.moisture, d.coarseness);
                    }
                }
            }
        }
    }

    void CarDirt::Splat(Entity* root, const Vector3& position, const Vector3& normal, const Vector3& velocity, const Deposit& d, float mass)
    {
        if (!sync(root) || mass <= 0.0f)
            return;

        // into the car's frame, the frame the atlas was baked in
        const Quaternion to_car = root->GetRotation().Inverse();
        const Vector3 p         = to_car * (position - root->GetPosition());
        const Vector3 n         = (to_car * normal).Normalized();
        const Vector3 v         = to_car * velocity;
        Vector3 t               = v - n * v.Dot(n);
        const float slide       = t.Length();
        if (slide < 0.05f)
        {
            t = n.Cross(fabsf(n.y) < 0.9f ? Vector3::Up : Vector3::Right);
        }
        t.Normalize();
        const Vector3 b = n.Cross(t).Normalized();

        const float approach = max(0.0f, -v.Dot(n));
        const float energy   = clamp((approach + slide * 0.5f) / 8.0f, 0.15f, 1.5f);
        const float wet      = clamp(d.moisture, 0.0f, 1.0f);
        const float size     = sqrtf(mass);

        // the core, slurry flattens into a blot that smears along the slide, a dry clump only leaves crumbs
        const float core = (0.0025f + 0.0055f * wet) * size * (0.75f + 0.5f * energy);
        stamp(p, n, t, b, core, 1.0f + min(2.5f, slide * 0.07f) * (0.4f + 0.6f * wet), (0.45f + 0.5f * wet), d);

        // satellites, the clump bursts and throws droplets and crumbs ahead along the slide, a few rebound behind it
        const uint32_t satellites = static_cast<uint32_t>((5.0f + 18.0f * wet + 12.0f * energy) * size + random01());
        const float tail          = 0.006f + 0.012f * energy + 0.0015f * slide;
        const float spread        = 0.003f + 0.007f * energy;
        for (uint32_t i = 0; i < satellites; i++)
        {
            float along = -logf(max(random01(), 1e-4f)) * tail;
            if (random01() < 0.18f)
            {
                along *= -0.35f;
            }
            const float across = random_gauss() * spread * (1.0f + along * 25.0f);
            const float radius = CarRain::GetTexelSize() * (0.35f + 1.4f * random01() * random01()) * (0.6f + 0.6f * wet);
            const float amount = (0.3f + 0.55f * random01()) * (0.55f + 0.45f * wet);
            stamp(p + t * along + b * across, n, t, b, radius, 1.0f + 1.5f * wet * min(1.0f, slide * 0.1f), amount, d);
        }

        // the dry part of a clump puffs into powder that settles in a halo around the impact
        const uint32_t powder = static_cast<uint32_t>(28.0f * size * (1.0f - wet) + random01());
        Deposit fine          = d;
        fine.coarseness       = 0.0f;
        fine.moisture         = 0.0f;
        for (uint32_t i = 0; i < powder; i++)
        {
            const Vector3 offset = t * (random_gauss() * 0.025f + 0.01f) + b * (random_gauss() * 0.02f);
            uint32_t texel;
            if (CarRain::FindTexel(p + offset, n, find_tolerance, texel))
            {
                deposit(texel, fine.color, 0.04f + 0.05f * random01(), 0.0f, 0.0f);
            }
        }
    }

    void CarDirt::Dust(Entity* root, const Vector3* wheels, uint32_t wheel_count, const Vector3& velocity, const Deposit& d, float grains)
    {
        if (!sync(root) || grains <= 0.0f)
            return;

        const vector<uint32_t>& paint = CarRain::GetPaintTexels();
        if (paint.empty())
            return;

        SP_PROFILE_CPU();

        const Quaternion to_car = root->GetRotation().Inverse();
        const Vector3 v         = to_car * velocity;
        const float speed       = v.Length();
        const Vector3 travel    = speed > 1.0f ? v / speed : Vector3::Zero;
        Vector3 contacts[4];
        wheel_count = min(wheel_count, 4u);
        for (uint32_t i = 0; i < wheel_count; i++)
        {
            contacts[i] = to_car * (wheels[i] - root->GetPosition());
        }
        const float bottom = CarRain::GetBoxMin().y;

        dust_remainder += grains;
        const uint32_t count = min(static_cast<uint32_t>(dust_remainder), dust_grains_max);
        dust_remainder       = min(dust_remainder - static_cast<float>(count), 1.0f);
        for (uint32_t k = 0; k < count; k++)
        {
            const uint32_t texel = paint[min(static_cast<uint32_t>(random01() * static_cast<float>(paint.size())), static_cast<uint32_t>(paint.size()) - 1)];
            const Vector3 nrm    = CarRain::GetTexelNormal(texel);
            const Vector3 pt     = CarRain::GetTexelPoint(texel);

            // the cloud the tires raise hugs the ground and the wake behind the car curls it back onto the rear faces
            const float height = pt.y - bottom;
            const float low    = expf(-height / 0.35f);
            const float wake   = max(0.0f, -nrm.Dot(travel)) * (0.15f + 0.85f * low);
            float wheel        = 0.0f;
            for (uint32_t i = 0; i < wheel_count; i++)
            {
                const Vector3 offset = pt - contacts[i];
                const float distance = sqrtf(offset.x * offset.x + offset.z * offset.z);
                const float behind   = offset.Dot(travel) < 0.0f ? 1.0f : 0.35f;
                wheel = max(wheel, expf(-distance / 0.45f - height / 0.5f) * behind);
            }
            const float settle = max(0.0f, nrm.y) * 0.06f;
            const float weight = 0.02f + 0.9f * low + 0.6f * wake + 1.2f * wheel + settle;
            if (random01() * 2.4f > weight)
                continue;

            const Vector3 tint = d.color * (0.88f + 0.24f * random01());
            deposit(texel, tint, 0.022f * (0.5f + random01()), d.moisture, d.coarseness);
        }
    }

    void CarDirt::Tick(float delta_time)
    {
        frame.clear();

        Entity* root = CarRain::GetBakedRoot();
        active       = root && CarRain::IsActive() && sync(root);
        if (!active)
            return;

        clock += max(delta_time, 0.0f);

        // hand this frame's changes over, what does not fit waits for the next frame
        const size_t count = min(pending.size(), static_cast<size_t>(SurfaceWater::dirt_texels_max));
        frame.assign(pending.begin(), pending.begin() + count);
        for (const SurfaceDirtTexel& change : frame)
        {
            pending_slot[change.texel] = -1;
        }
        pending.erase(pending.begin(), pending.begin() + count);
        for (size_t i = 0; i < pending.size(); i++)
        {
            pending_slot[pending[i].texel] = static_cast<int32_t>(i);
        }
    }

    void CarDirt::Clear()
    {
        if (!dirty)
            return;

        fill(color.begin(), color.end(), 0u);
        fill(state.begin(), state.end(), 0u);
        fill(pending_slot.begin(), pending_slot.end(), -1);
        pending.clear();
        frame.clear();
        dust_remainder = 0.0f;
        dirty          = false;
        dirt_version++;
    }

    void CarDirt::Release()
    {
        color.clear();
        state.clear();
        pending_slot.clear();
        pending.clear();
        frame.clear();
        owner          = nullptr;
        rain_version   = ~0u;
        active         = false;
        dirty          = false;
        dust_remainder = 0.0f;
        dirt_version++;
    }

    float CarDirt::GetCoverage()
    {
        const vector<uint32_t>& paint = CarRain::GetPaintTexels();
        if (paint.empty() || color.empty())
            return 0.0f;

        double sum = 0.0;
        for (uint32_t texel : paint)
        {
            sum += texel < color.size() ? static_cast<double>(color[texel] >> 24) : 0.0;
        }
        return static_cast<float>(sum / (255.0 * static_cast<double>(paint.size())));
    }

    bool CarDirt::IsActive()
    {
        return active && !color.empty();
    }

    bool CarDirt::IsDirty()
    {
        return dirty;
    }

    uint32_t CarDirt::GetVersion()
    {
        return dirt_version;
    }

    float CarDirt::GetClock()
    {
        return clock;
    }

    const vector<uint32_t>& CarDirt::GetColor()
    {
        return color;
    }

    const vector<uint32_t>& CarDirt::GetState()
    {
        return state;
    }

    const vector<SurfaceDirtTexel>& CarDirt::GetTexels()
    {
        return frame;
    }
}
