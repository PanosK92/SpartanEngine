/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ======================
#include "pch.h"
#include "CarRain.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Render.h"
#include "../geometry/Mesh.h"
#include "../core/ThreadPool.h"
#include "../profiling/Profiler.h"
#include <array>
#include <vector>
#include <cfloat>
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // water and air at 20 c
        constexpr float water_density      = 1000.0f;
        constexpr float air_density        = 1.2f;
        constexpr float air_viscosity      = 1.5e-5f; // kinematic, m^2/s
        constexpr float surface_tension    = 0.072f;  // n/m
        constexpr float vapour_diffusivity = 2.5e-5f; // m^2/s
        constexpr float vapour_saturation  = 0.0173f; // kg/m^3

        // waxed paint and glass
        constexpr float hysteresis_dry   = 0.25f;  // cosine of the receding minus the advancing contact angle
        constexpr float hysteresis_wet   = 0.08f;  // over paint that is already wet the contact line hardly snags
        constexpr float contact_friction = 0.8f;   // n s/m^2 per metre of contact radius, a 2 mm drop creeps down a vertical panel at about 5 cm/s
        constexpr float drag_coefficient = 0.8f;
        constexpr float boundary_layer   = 0.6f;   // share of the free stream a millimetre tall drop feels inside the boundary layer
        constexpr float wake_shelter     = 0.25f;  // faces looking downstream sit in separated, slow air
        constexpr float residue_depth    = 5.0e-6f; // metres of water a sliding drop leaves on the paint it wets

        // rain
        constexpr float rain_rate_max      = 25.0f;  // mm/h at full rain
        constexpr float rain_fall_speed    = 7.0f;   // m/s, terminal speed of a 2 mm drop
        constexpr float rain_drop_diameter = 0.001f; // metres, mean of the marshall palmer size spectrum at full rain
        constexpr float rain_retained      = 0.7f;   // of an impact's mass that stays on the paint, the rest splashes away
        constexpr uint32_t rain_samples    = 4096;   // texels the rain is evaluated on per step, each stands for its share of the paint

        // drops
        constexpr float drop_mass_min      = 5.0e-8f; // kg, 0.05 mg, anything lighter is micro water
        constexpr float drop_mass_nucleate = 7.0e-7f; // kg, this much micro water on one texel has coalesced into a drop
        constexpr float micro_life         = 900.0f;  // seconds on the evaporation clock the micro water of a texel lasts
        constexpr float micro_radius       = 1.5e-4f; // metres, a typical micro droplet, sets how much faster it dries in the airflow
        constexpr float humidity_rain      = 0.97f;
        constexpr float humidity_dry       = 0.72f;
        constexpr float humidity_time      = 300.0f;  // seconds for the air to dry out once the rain stops
        constexpr float transfer_tolerance = 0.015f;  // metres a point may sit off the map it walks onto
        constexpr float step_texels        = 0.5f;    // longest move per substep
        constexpr uint32_t steps_max       = 24;
        constexpr uint32_t merges_max      = 4;
        constexpr float step_max           = 1.0f / 30.0f;
        constexpr float prewarm_seconds    = 20.0f;
        constexpr float prewarm_step       = 0.05f;

        // atlas
        constexpr float surface_none   = 1.0e30f;
        constexpr float texel_size_min = 0.004f;
        constexpr uint32_t texels_cap  = 4000000;
        constexpr uint32_t atlas_max   = 4096;

        enum : uint8_t
        {
            kind_empty = 0, // no paint
            kind_face  = 1, // paint facing along this map's axis and side
            kind_other = 2  // paint that belongs to another map, a drop walking onto it changes map
        };

        enum : uint8_t
        {
            state_held       = 0,
            state_detached   = 1,
            state_evaporated = 2
        };

        struct face_rect
        {
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t w = 0;
            uint32_t h = 0;
        };

        struct drop
        {
            Vector3 position = Vector3::Zero; // car local, on the paint
            Vector3 velocity = Vector3::Zero; // car local, along the paint, m/s
            Vector3 normal   = Vector3::Up;   // car local, of the paint under it
            Vector3 lean     = Vector3::Zero; // force along the paint over the pinning force
            float mass       = 0.0f;          // kg, 0 for a free slot
            float seed       = 0.0f;
            uint32_t texel   = 0;
            uint8_t face     = 0;
            uint8_t state    = state_held;
        };

        // atlas
        Entity* baked_root         = nullptr;
        uint32_t state_version     = 0;
        uint32_t atlas_w           = 0;
        uint32_t atlas_h           = 0;
        float texel_size           = texel_size_min;
        Vector3 box_min            = Vector3::Zero;
        array<face_rect, 6> faces  = {};
        vector<float> surface;        // the gpu copy, axis coordinate where the texel belongs to its own map
        vector<float> height;         // axis coordinate of the outermost paint, whichever map it belongs to
        vector<uint32_t> texel_info;  // packed normal (3 x snorm8) and kind
        vector<uint32_t> face_texels; // every texel of paint that belongs to its map, where rain can land
        vector<float> micro;          // milligrams (negative once swept) and clock stamp per texel
        vector<int32_t> occupant;     // the drop whose centre sits on the texel
        vector<int32_t> pending_slot; // where the texel's change waits in texels_pending

        // simulation
        vector<drop> drops;
        vector<uint32_t> drops_free;
        vector<CarRainDropGpu> drops_gpu;
        vector<CarRainTexelGpu> texels_pending;
        vector<CarRainTexelGpu> texels_frame;
        float clock        = 10000.0f; // a texel stamped at 0 has long dried
        float humidity     = humidity_dry;
        bool active        = false;
        uint32_t rng_state = 0x9e3779b9u;

        float random01()
        {
            rng_state ^= rng_state << 13;
            rng_state ^= rng_state >> 17;
            rng_state ^= rng_state << 5;
            return static_cast<float>(rng_state >> 8) / 16777216.0f;
        }

        uint32_t hash(uint32_t x, uint32_t y)
        {
            uint32_t h = x * 1664525u + y * 22695477u + 1013904223u;
            h ^= h >> 16;
            h *= 0x7feb352du;
            h ^= h >> 15;
            h *= 0x846ca68bu;
            h ^= h >> 16;
            return h;
        }

        float get(const Vector3& v, uint32_t axis)
        {
            return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
        }

        void set(Vector3& v, uint32_t axis, float value)
        {
            (axis == 0 ? v.x : (axis == 1 ? v.y : v.z)) = value;
        }

        // the two axes a map spans, the same pairs as rain_plane() in common_rain.hlsl
        uint32_t axis_u(uint32_t axis) { return axis == 0 ? 1 : 0; }
        uint32_t axis_v(uint32_t axis) { return axis == 2 ? 1 : 2; }

        uint32_t pack_info(const Vector3& n, uint8_t kind)
        {
            auto q = [](float x)
            {
                return static_cast<uint32_t>(static_cast<int32_t>(roundf(clamp(x, -1.0f, 1.0f) * 127.0f)) & 0xff);
            };
            return q(n.x) | (q(n.y) << 8) | (q(n.z) << 16) | (static_cast<uint32_t>(kind) << 24);
        }

        Vector3 info_normal(uint32_t info)
        {
            auto d = [](uint32_t b)
            {
                return static_cast<float>(static_cast<int8_t>(b & 0xff)) / 127.0f;
            };
            return Vector3(d(info), d(info >> 8), d(info >> 16)).Normalized();
        }

        uint8_t info_kind(uint32_t info)
        {
            return static_cast<uint8_t>(info >> 24);
        }

        uint8_t face_of(const Vector3& n)
        {
            const float ax = fabsf(n.x);
            const float ay = fabsf(n.y);
            const float az = fabsf(n.z);
            const uint32_t axis = ax > ay ? (ax > az ? 0 : 2) : (ay > az ? 1 : 2);
            return static_cast<uint8_t>(axis * 2 + (get(n, axis) < 0.0f ? 1 : 0));
        }

        uint8_t face_at(uint32_t texel)
        {
            const uint32_t x = texel % atlas_w;
            const uint32_t y = texel / atlas_w;
            for (uint8_t f = 0; f < 6; f++)
            {
                const face_rect& r = faces[f];
                if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
                    return f;
            }
            return 0;
        }

        bool locate(uint32_t face, const Vector3& p, uint32_t& texel)
        {
            const uint32_t axis = face / 2;
            const uint32_t u    = axis_u(axis);
            const uint32_t v    = axis_v(axis);
            const face_rect& r  = faces[face];
            const float fu      = (get(p, u) - get(box_min, u)) / texel_size + 1.0f;
            const float fv      = (get(p, v) - get(box_min, v)) / texel_size + 1.0f;
            if (fu < 0.0f || fv < 0.0f || fu >= static_cast<float>(r.w) || fv >= static_cast<float>(r.h))
                return false;

            texel = (r.y + static_cast<uint32_t>(fv)) * atlas_w + r.x + static_cast<uint32_t>(fu);
            return true;
        }

        Vector3 texel_position(uint32_t texel, uint32_t face, float fx, float fy)
        {
            const uint32_t axis = face / 2;
            const face_rect& r  = faces[face];
            const float x       = static_cast<float>(texel % atlas_w - r.x) - 1.0f + fx;
            const float y       = static_cast<float>(texel / atlas_w - r.y) - 1.0f + fy;
            Vector3 p;
            set(p, axis_u(axis), get(box_min, axis_u(axis)) + x * texel_size);
            set(p, axis_v(axis), get(box_min, axis_v(axis)) + y * texel_size);
            set(p, axis, height[texel]);
            return p;
        }

        // contact radius of a cap meeting waxed paint at about 90 degrees, a half sphere
        float radius_of(float mass)
        {
            return cbrtf(3.0f * mass / (2.0f * pi * water_density));
        }

        float residue_mg()
        {
            return residue_depth * water_density * texel_size * texel_size * 1.0e6f;
        }

        // micro droplets evaporate on the clock, which runs fast in dry, moving air and nearly stops in a downpour
        float micro_now(uint32_t texel)
        {
            const float left = max(0.0f, 1.0f - (clock - micro[texel * 2 + 1]) / micro_life);
            return fabsf(micro[texel * 2]) * left;
        }

        void micro_set(uint32_t texel, float mg, bool swept)
        {
            micro[texel * 2]     = swept ? -mg : mg;
            micro[texel * 2 + 1] = clock;

            CarRainTexelGpu change;
            change.texel = texel;
            change.mass  = micro[texel * 2];
            change.stamp = clock;
            if (pending_slot[texel] >= 0)
            {
                texels_pending[pending_slot[texel]] = change;
            }
            else
            {
                pending_slot[texel] = static_cast<int32_t>(texels_pending.size());
                texels_pending.push_back(change);
            }
        }

        // how hard the contact line snags on a texel, per metre of contact radius
        // specks and scratches make every spot a little different, which is what makes a running drop zig-zag
        float pinning(uint32_t texel)
        {
            const uint32_t x   = texel % atlas_w;
            const uint32_t y   = texel / atlas_w;
            const float coarse = static_cast<float>(hash(x >> 2, y >> 2) & 0xffff) / 65535.0f;
            const float fine   = static_cast<float>(hash(x + 7919, y + 104729) & 0xffff) / 65535.0f;
            const float wet    = min(1.0f, micro_now(texel) / max(residue_mg(), 1e-6f));
            return 2.0f * surface_tension * (hysteresis_dry + (hysteresis_wet - hysteresis_dry) * wet) * (0.6f + 0.4f * coarse + 0.4f * fine);
        }

        // walks a point onto the paint, over an edge onto the neighbouring map, false once it has left the car
        bool project(uint8_t& face, Vector3& p, uint32_t& texel)
        {
            if (!locate(face, p, texel))
                return false;

            uint32_t info = texel_info[texel];
            uint8_t kind  = info_kind(info);
            if (kind == kind_empty)
                return false;

            if (kind == kind_other)
            {
                Vector3 q = p;
                set(q, face / 2, height[texel]);
                const uint8_t next = face_of(info_normal(info));
                uint32_t texel_next;
                if (!locate(next, q, texel_next) || info_kind(texel_info[texel_next]) != kind_face)
                    return false;
                if (fabsf(height[texel_next] - get(q, next / 2)) > transfer_tolerance)
                    return false;

                face  = next;
                texel = texel_next;
                p     = q;
            }

            set(p, face / 2, height[texel]);
            return true;
        }

        void drop_remove(uint32_t index)
        {
            drop& d = drops[index];
            if (occupant[d.texel] == static_cast<int32_t>(index))
            {
                occupant[d.texel] = -1;
            }
            d.mass     = 0.0f;
            d.velocity = Vector3::Zero;
            drops_free.push_back(index);
        }

        // the first drop within reach of a circle on the paint, texel is where the circle's centre sits
        int32_t drop_touching(uint32_t texel, uint32_t face, const Vector3& p, float radius, int32_t ignore)
        {
            const face_rect& r = faces[face];
            const int32_t x    = static_cast<int32_t>(texel % atlas_w);
            const int32_t y    = static_cast<int32_t>(texel / atlas_w);
            const int32_t reach = min(3, static_cast<int32_t>(ceilf((radius + 0.004f) / texel_size)));
            for (int32_t dy = -reach; dy <= reach; dy++)
            {
                const int32_t ty = y + dy;
                if (ty < static_cast<int32_t>(r.y) || ty >= static_cast<int32_t>(r.y + r.h))
                    continue;
                for (int32_t dx = -reach; dx <= reach; dx++)
                {
                    const int32_t tx = x + dx;
                    if (tx < static_cast<int32_t>(r.x) || tx >= static_cast<int32_t>(r.x + r.w))
                        continue;
                    const int32_t other = occupant[static_cast<uint32_t>(ty) * atlas_w + static_cast<uint32_t>(tx)];
                    if (other < 0 || other == ignore)
                        continue;
                    const drop& o = drops[other];
                    if ((o.position - p).Length() < radius + radius_of(o.mass))
                        return other;
                }
            }
            return -1;
        }

        // the drops touch and become one, it sits at their centre of mass and carries their momentum
        void drop_merge(uint32_t survivor, uint32_t absorbed)
        {
            drop& a     = drops[survivor];
            drop& b     = drops[absorbed];
            const float mass = a.mass + b.mass;
            Vector3 p   = (a.position * a.mass + b.position * b.mass) / mass;
            a.velocity  = (a.velocity * a.mass + b.velocity * b.mass) / mass;
            if (b.mass > a.mass)
            {
                a.seed = b.seed;
                a.face = b.face;
            }
            a.mass = mass;
            drop_remove(absorbed);

            uint32_t texel;
            uint8_t face = a.face;
            if (project(face, p, texel))
            {
                if (occupant[a.texel] == static_cast<int32_t>(survivor))
                {
                    occupant[a.texel] = -1;
                }
                a.position = p;
                a.face     = face;
                a.texel    = texel;
                a.normal   = info_normal(texel_info[texel]);
            }
        }

        // a running drop gathers the micro droplets under it and wets the paint behind it with a thin residue
        void drop_sweep(drop& d)
        {
            const float radius = radius_of(d.mass);
            const face_rect& r = faces[d.face];
            const int32_t x    = static_cast<int32_t>(d.texel % atlas_w);
            const int32_t y    = static_cast<int32_t>(d.texel / atlas_w);
            const int32_t reach = radius > texel_size * 0.5f ? 1 : 0;
            const float residue = residue_mg();
            for (int32_t dy = -reach; dy <= reach; dy++)
            {
                for (int32_t dx = -reach; dx <= reach; dx++)
                {
                    const int32_t tx = x + dx;
                    const int32_t ty = y + dy;
                    if (tx < static_cast<int32_t>(r.x) || tx >= static_cast<int32_t>(r.x + r.w) || ty < static_cast<int32_t>(r.y) || ty >= static_cast<int32_t>(r.y + r.h))
                        continue;
                    const uint32_t t = static_cast<uint32_t>(ty) * atlas_w + static_cast<uint32_t>(tx);
                    if (info_kind(texel_info[t]) != kind_face)
                        continue;
                    // the corners of the window lie outside a round footprint
                    if ((dx != 0 || dy != 0) && (texel_position(t, d.face, 0.5f, 0.5f) - d.position).Length() > radius + texel_size * 0.5f)
                        continue;

                    const float gathered = micro_now(t);
                    const float left     = min(residue, max(0.0f, d.mass * 1.0e6f + gathered - 0.05f));
                    d.mass              += (gathered - left) * 1.0e-6f;
                    micro_set(t, left, true);
                }
            }
        }

        bool drop_spawn(uint32_t texel, uint8_t face, const Vector3& p, float mass)
        {
            if (occupant[texel] >= 0)
            {
                drops[occupant[texel]].mass += mass;
                return true;
            }

            uint32_t index;
            if (!drops_free.empty())
            {
                index = drops_free.back();
                drops_free.pop_back();
            }
            else if (drops.size() < CarRain::drops_max)
            {
                index = static_cast<uint32_t>(drops.size());
                drops.emplace_back();
            }
            else
            {
                return false;
            }

            drop& d    = drops[index];
            d          = drop();
            d.position = p;
            d.normal   = info_normal(texel_info[texel]);
            d.mass     = mass;
            d.seed     = random01();
            d.texel    = texel;
            d.face     = face;
            occupant[texel] = static_cast<int32_t>(index);
            return true;
        }

        void impact(uint32_t texel, uint8_t face, float mass)
        {
            const Vector3 p = texel_position(texel, face, random01(), random01());

            // landing on a drop it joins it
            const int32_t hit = drop_touching(texel, face, p, radius_of(mass), -1);
            if (hit >= 0)
            {
                drops[hit].mass += mass;
                return;
            }

            // small impacts stay micro droplets until a texel holds enough of them to coalesce
            const float total = micro_now(texel) * 1.0e-6f + mass;
            if (total >= drop_mass_nucleate && drop_spawn(texel, face, p, total))
            {
                micro_set(texel, 0.0f, false);
                return;
            }
            micro_set(texel, total * 1.0e6f, false);
        }

        void rain(const CarRain::Conditions& c, float dt)
        {
            if (c.rain <= 0.0f || c.exposure <= 0.0f || face_texels.empty())
                return;

            // kg of water per m^3 of air, what falls at the terminal speed to give the rain rate
            const float rate          = c.rain * rain_rate_max;
            const float rain_density  = rate / 3.6e6f * water_density / rain_fall_speed;
            // in the car's frame the rain comes at it, faces looking forward catch more the faster it goes
            const Vector3 fall        = c.gravity.Normalized() * rain_fall_speed + c.wind;
            const Vector3 relative    = fall - c.velocity;
            const float diameter      = rain_drop_diameter * (0.7f + 0.3f * c.rain);
            // mean retained mass of an exponential size spectrum, the mean of d^3 is 6 d0^3
            const float mass_mean     = water_density * pi * diameter * diameter * diameter * rain_retained;
            const float share         = static_cast<float>(face_texels.size()) / static_cast<float>(rain_samples);
            const float texel_area    = texel_size * texel_size;

            for (uint32_t i = 0; i < rain_samples; i++)
            {
                const uint32_t texel = face_texels[static_cast<uint32_t>(random01() * static_cast<float>(face_texels.size())) % face_texels.size()];
                const Vector3 n      = info_normal(texel_info[texel]);
                const float approach = -n.Dot(relative);
                if (approach <= 0.0f)
                    continue;

                const uint8_t face     = face_at(texel);
                const float area       = texel_area / max(fabsf(get(n, face / 2)), 0.3f);
                const float expected   = rain_density * approach * area * dt * share * c.exposure / mass_mean;
                const uint32_t count   = static_cast<uint32_t>(expected + random01());
                for (uint32_t k = 0; k < count; k++)
                {
                    const float d = min(0.005f, -logf(max(random01(), 1e-6f)) * diameter);
                    impact(texel, face, water_density * pi / 6.0f * d * d * d * rain_retained);
                }
            }
        }

        float ventilation(float speed, float radius)
        {
            return 1.0f + 0.3f * sqrtf(2.0f * radius * speed / air_viscosity);
        }

        // what the paint, the car's motion and the air do to one drop, touches nothing but the drop
        void drop_force(drop& d, const Vector3& specific, const Vector3& air, const Vector3& air_dir, float dryness, float dt)
        {
            const float radius = radius_of(d.mass);
            const Vector3& n   = d.normal;

            // the air skimming the paint, slowed inside the boundary layer and nearly still in the wake behind the car
            Vector3 flow       = air - n * air.Dot(n);
            flow              *= boundary_layer * (air_dir.Dot(n) > 0.2f ? wake_shelter : 1.0f);
            const float flow_speed = flow.Length();
            const float area   = 0.5f * pi * radius * radius;

            const Vector3 force = specific * d.mass + flow * (0.5f * air_density * drag_coefficient * area * flow_speed);

            // pulled off the paint harder than surface tension holds it, it drips or is thrown off
            const float normal_force = force.Dot(n);
            if (normal_force > pi * radius * surface_tension)
            {
                d.state = state_detached;
                return;
            }

            Vector3 along   = force - n * normal_force;
            const float pin = pinning(d.texel) * radius;

            // the contact line advances where it snags the least, so a running drop veers towards wet paint and
            // away from specks, which is how it finds and keeps to the paths earlier drops wetted
            const float speed = d.velocity.Length();
            if (speed > 1e-4f)
            {
                const Vector3 ahead = d.velocity / speed;
                const Vector3 side  = n.Cross(ahead).Normalized();
                uint32_t texel_left, texel_right;
                if (locate(d.face, d.position + (ahead + side) * radius, texel_left) && locate(d.face, d.position + (ahead - side) * radius, texel_right))
                {
                    along += side * ((pinning(texel_right) - pinning(texel_left)) * radius * 0.5f);
                }
            }

            // held until the push beats the pinning, then viscous dissipation in the contact region sets the pace
            const float friction  = contact_friction * radius;
            const Vector3 momentum = d.velocity * d.mass + along * dt;
            const float held      = pin * dt;
            const float p         = momentum.Length();
            d.velocity            = p <= held ? Vector3::Zero : momentum * ((p - held) / p / (d.mass + friction * dt));

            // the cap deforms by how close the push comes to tearing the contact line loose
            d.lean = along / max(pin, 1e-9f);
            const float lean_length = d.lean.Length();
            if (lean_length > 2.0f)
            {
                d.lean *= 2.0f / lean_length;
            }

            // diffusion limited evaporation, a sessile drop loses mass in proportion to its radius
            d.mass -= 2.0f * pi * radius * vapour_diffusivity * vapour_saturation * dryness * ventilation(flow_speed, radius) * dt;
            if (d.mass < drop_mass_min)
            {
                d.state = state_evaporated;
            }
        }

        void drop_move(uint32_t index, float dt)
        {
            drop& d = drops[index];
            if (d.state == state_detached)
            {
                drop_remove(index);
                return;
            }
            if (d.state == state_evaporated)
            {
                micro_set(d.texel, micro_now(d.texel) + max(d.mass, 0.0f) * 1.0e6f, false);
                drop_remove(index);
                return;
            }

            const float speed = d.velocity.Length();
            if (speed <= 0.0f)
                return;

            const uint32_t steps = min(steps_max, max(1u, static_cast<uint32_t>(ceilf(speed * dt / (step_texels * texel_size)))));
            const float step     = speed * dt / static_cast<float>(steps);
            for (uint32_t s = 0; s < steps; s++)
            {
                Vector3 p    = d.position + d.velocity * (step / speed);
                uint8_t face = d.face;
                uint32_t texel;
                // off the paint, it runs over an edge into the air and drips away
                if (!project(face, p, texel))
                {
                    drop_remove(index);
                    return;
                }

                d.position = p;
                d.face     = face;
                d.normal   = info_normal(texel_info[texel]);
                Vector3 v  = d.velocity - d.normal * d.velocity.Dot(d.normal);
                const float v_length = v.Length();
                d.velocity = v_length > 1e-6f ? v * (speed / v_length) : Vector3::Zero;
                if (d.velocity == Vector3::Zero)
                    break;

                if (texel == d.texel)
                    continue;

                if (occupant[d.texel] == static_cast<int32_t>(index))
                {
                    occupant[d.texel] = -1;
                }
                d.texel = texel;
                drop_sweep(d);
                if (d.mass < drop_mass_min)
                {
                    drop_remove(index);
                    return;
                }

                for (uint32_t m = 0; m < merges_max; m++)
                {
                    const int32_t other = drop_touching(d.texel, d.face, d.position, radius_of(d.mass), static_cast<int32_t>(index));
                    if (other < 0)
                        break;
                    drop_merge(index, static_cast<uint32_t>(other));
                }

                // a drop already sits there, it swallows it too
                if (occupant[d.texel] >= 0 && occupant[d.texel] != static_cast<int32_t>(index))
                {
                    drop_merge(index, static_cast<uint32_t>(occupant[d.texel]));
                }
                occupant[d.texel] = static_cast<int32_t>(index);
            }
        }

        void step(const CarRain::Conditions& c, float dt)
        {
            // the air saturates in the rain and dries out over minutes after it
            const bool raining   = c.rain > 0.0f && c.exposure > 0.0f;
            const float target   = raining ? humidity_rain : humidity_dry;
            humidity            += (target - humidity) * min(1.0f, dt / (raining ? 30.0f : humidity_time));
            const float dryness  = max(0.0f, 1.0f - humidity);

            const Vector3 air     = c.wind - c.velocity;
            const float air_speed = air.Length();
            const Vector3 air_dir = air_speed > 0.01f ? air / air_speed : Vector3::Zero;
            clock += dt * (dryness / (1.0f - humidity_rain)) * ventilation(air_speed * boundary_layer, micro_radius);

            // in the car's frame everything on it feels gravity minus the car's own acceleration
            const Vector3 specific = c.gravity - c.acceleration;

            SP_PROFILE_CPU_START("car_rain_impacts");
            rain(c, dt);
            SP_PROFILE_CPU_END();

            SP_PROFILE_CPU_START("car_rain_forces");
            const uint32_t count = static_cast<uint32_t>(drops.size());
            auto forces = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                {
                    drop& d = drops[i];
                    if (d.mass > 0.0f)
                    {
                        d.state = state_held;
                        drop_force(d, specific, air, air_dir, dryness, dt);
                    }
                }
            };
            if (count > 0)
            {
                ThreadPool::ParallelLoop(forces, count);
            }
            SP_PROFILE_CPU_END();

            // moving drops merge and write the paint, one at a time
            SP_PROFILE_CPU_START("car_rain_moves");
            for (uint32_t i = 0; i < count; i++)
            {
                if (drops[i].mass > 0.0f)
                {
                    drop_move(i, dt);
                }
            }
            SP_PROFILE_CPU_END();
        }

        bool is_wheel_part(Entity* entity, Entity* root)
        {
            for (Entity* e = entity; e && e != root; e = e->GetParent())
            {
                if (e->HasTag("wheel"))
                    return true;
            }
            return false;
        }

        bool bake(Entity* root)
        {
            SP_PROFILE_CPU();

            // every part of the car but the wheels, in the car's frame, positions and rotation only so metres stay metres
            const Vector3 root_position   = root->GetPosition();
            const Quaternion root_inverse = root->GetRotation().Inverse();
            vector<Vector3> positions;
            vector<Vector3> normals;
            vector<uint32_t> indices;
            vector<uint32_t> mesh_indices;
            vector<RHI_Vertex_PosTexNorTan> mesh_vertices;
            for (Entity* entity : World::GetEntitiesWithRender())
            {
                if (!entity || !entity->GetActive() || (entity != root && !entity->IsDescendantOf(root)) || is_wheel_part(entity, root))
                    continue;

                Render* render = entity->GetComponent<Render>();
                if (!render || !render->GetMesh() || render->HasInstancing())
                    continue;

                render->GetMesh()->GetGeometry(render->GetSubMeshIndex(), &mesh_indices, &mesh_vertices);
                const Matrix& world  = entity->GetMatrix();
                const Vector3 origin = Vector3::Zero * world;
                const uint32_t base  = static_cast<uint32_t>(positions.size());
                for (const RHI_Vertex_PosTexNorTan& vertex : mesh_vertices)
                {
                    positions.push_back(root_inverse * (vertex.get_position() * world - root_position));
                    normals.push_back(root_inverse * (vertex.get_normal() * world - origin).Normalized());
                }
                for (uint32_t index : mesh_indices)
                {
                    indices.push_back(index < mesh_vertices.size() ? base + index : base);
                }
            }
            if (indices.size() < 3)
                return false;

            Vector3 box_max = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            box_min         = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
            for (const Vector3& p : positions)
            {
                box_min = Vector3(min(box_min.x, p.x), min(box_min.y, p.y), min(box_min.z, p.z));
                box_max = Vector3(max(box_max.x, p.x), max(box_max.y, p.y), max(box_max.z, p.z));
            }
            box_min -= Vector3(0.01f, 0.01f, 0.01f);
            box_max += Vector3(0.01f, 0.01f, 0.01f);
            const Vector3 size = box_max - box_min;

            // the six maps in three rows, +x -x, +y -y, +z -z, the texel grows until the atlas fits
            texel_size = texel_size_min;
            for (;;)
            {
                for (uint32_t f = 0; f < 6; f++)
                {
                    const uint32_t axis = f / 2;
                    faces[f].w = static_cast<uint32_t>(ceilf(get(size, axis_u(axis)) / texel_size)) + 2;
                    faces[f].h = static_cast<uint32_t>(ceilf(get(size, axis_v(axis)) / texel_size)) + 2;
                }
                atlas_w = max(faces[0].w * 2, max(faces[2].w * 2, faces[4].w * 2));
                atlas_h = faces[0].h + faces[2].h + faces[4].h;
                if (atlas_w <= atlas_max && atlas_h <= atlas_max && atlas_w * atlas_h <= texels_cap)
                    break;
                texel_size *= 1.1f;
            }
            uint32_t row = 0;
            for (uint32_t f = 0; f < 6; f += 2)
            {
                faces[f].x     = 0;
                faces[f].y     = row;
                faces[f + 1].x = faces[f].w;
                faces[f + 1].y = row;
                row           += faces[f].h;
            }

            // software raster, each map keeps the paint furthest out along its axis and side
            const uint32_t texel_count = atlas_w * atlas_h;
            vector<float> best(texel_count, -FLT_MAX);
            vector<Vector3> best_normal(texel_count, Vector3::Zero);
            height.assign(texel_count, surface_none);
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const uint32_t i0 = indices[i];
                const uint32_t i1 = indices[i + 1];
                const uint32_t i2 = indices[i + 2];
                const Vector3& p0 = positions[i0];
                const Vector3& p1 = positions[i1];
                const Vector3& p2 = positions[i2];
                Vector3 geometric = (p1 - p0).Cross(p2 - p0);
                const float length = geometric.Length();
                if (length < 1e-12f)
                    continue;
                geometric /= length;
                if (geometric.Dot(normals[i0] + normals[i1] + normals[i2]) < 0.0f)
                {
                    geometric = -geometric;
                }

                for (uint32_t f = 0; f < 6; f++)
                {
                    const uint32_t axis = f / 2;
                    const float sign    = (f & 1) ? -1.0f : 1.0f;
                    if (get(geometric, axis) * sign <= 0.02f)
                        continue;

                    const uint32_t u   = axis_u(axis);
                    const uint32_t v   = axis_v(axis);
                    const face_rect& r = faces[f];
                    const float x0 = (get(p0, u) - get(box_min, u)) / texel_size + 1.0f;
                    const float y0 = (get(p0, v) - get(box_min, v)) / texel_size + 1.0f;
                    const float x1 = (get(p1, u) - get(box_min, u)) / texel_size + 1.0f;
                    const float y1 = (get(p1, v) - get(box_min, v)) / texel_size + 1.0f;
                    const float x2 = (get(p2, u) - get(box_min, u)) / texel_size + 1.0f;
                    const float y2 = (get(p2, v) - get(box_min, v)) / texel_size + 1.0f;
                    const float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
                    if (fabsf(area) < 1e-9f)
                        continue;

                    auto write = [&](uint32_t tx, uint32_t ty, float w0, float w1, float w2)
                    {
                        const uint32_t texel = (r.y + ty) * atlas_w + r.x + tx;
                        const float key      = sign * (get(p0, axis) * w0 + get(p1, axis) * w1 + get(p2, axis) * w2);
                        if (key <= best[texel])
                            return;
                        best[texel]   = key;
                        height[texel] = key * sign;
                        Vector3 n     = (normals[i0] * w0 + normals[i1] * w1 + normals[i2] * w2).Normalized();
                        best_normal[texel] = n.Dot(geometric) > 0.3f ? n : geometric;
                    };

                    const int32_t min_x = max(0, static_cast<int32_t>(floorf(min(x0, min(x1, x2)))));
                    const int32_t min_y = max(0, static_cast<int32_t>(floorf(min(y0, min(y1, y2)))));
                    const int32_t max_x = min(static_cast<int32_t>(r.w) - 1, static_cast<int32_t>(floorf(max(x0, max(x1, x2)))));
                    const int32_t max_y = min(static_cast<int32_t>(r.h) - 1, static_cast<int32_t>(floorf(max(y0, max(y1, y2)))));
                    bool covered = false;
                    for (int32_t ty = min_y; ty <= max_y; ty++)
                    {
                        for (int32_t tx = min_x; tx <= max_x; tx++)
                        {
                            const float px = static_cast<float>(tx) + 0.5f;
                            const float py = static_cast<float>(ty) + 0.5f;
                            const float w0 = ((x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)) / area;
                            const float w1 = ((x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)) / area;
                            const float w2 = 1.0f - w0 - w1;
                            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                                continue;
                            covered = true;
                            write(static_cast<uint32_t>(tx), static_cast<uint32_t>(ty), w0, w1, w2);
                        }
                    }

                    // a sliver smaller than a texel still leaves its mark
                    if (!covered)
                    {
                        const float cx = (x0 + x1 + x2) / 3.0f;
                        const float cy = (y0 + y1 + y2) / 3.0f;
                        if (cx >= 0.0f && cy >= 0.0f && cx < static_cast<float>(r.w) && cy < static_cast<float>(r.h))
                        {
                            write(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy), 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);
                        }
                    }
                }
            }

            // each texel belongs to the map its normal looks along, the rest is handed over to that map
            surface.assign(texel_count, surface_none);
            texel_info.assign(texel_count, 0);
            face_texels.clear();
            for (uint32_t f = 0; f < 6; f++)
            {
                const uint32_t axis = f / 2;
                const float sign    = (f & 1) ? -1.0f : 1.0f;
                const face_rect& r  = faces[f];
                for (uint32_t ty = 0; ty < r.h; ty++)
                {
                    for (uint32_t tx = 0; tx < r.w; tx++)
                    {
                        const uint32_t texel = (r.y + ty) * atlas_w + r.x + tx;
                        if (best[texel] == -FLT_MAX)
                            continue;

                        const Vector3& n  = best_normal[texel];
                        const bool own    = face_of(n) == f && get(n, axis) * sign > 0.0f;
                        texel_info[texel] = pack_info(n, own ? kind_face : kind_other);
                        if (own)
                        {
                            surface[texel] = height[texel];
                            face_texels.push_back(texel);
                        }
                    }
                }
            }

            micro.assign(static_cast<size_t>(texel_count) * 2, 0.0f);
            occupant.assign(texel_count, -1);
            pending_slot.assign(texel_count, -1);
            drops.clear();
            drops_free.clear();
            texels_pending.clear();

            SP_LOG_INFO("car rain, %zu triangles baked into a %ux%u atlas of %.1f mm texels, %zu of them paint", indices.size() / 3, atlas_w, atlas_h, texel_size * 1000.0f, face_texels.size());
            return true;
        }

        // the car has been standing in this weather a while before anyone got in
        void prewarm(const CarRain::Conditions& conditions)
        {
            if (conditions.rain <= 0.0f)
                return;

            CarRain::Conditions parked = conditions;
            parked.velocity            = Vector3::Zero;
            parked.acceleration        = Vector3::Zero;
            humidity                   = humidity_rain;
            for (float t = 0.0f; t < prewarm_seconds; t += prewarm_step)
            {
                step(parked, prewarm_step);
            }

            // the gpu takes the whole state from the cpu copy
            for (const CarRainTexelGpu& change : texels_pending)
            {
                pending_slot[change.texel] = -1;
            }
            texels_pending.clear();
        }

        void build_gpu()
        {
            SP_PROFILE_CPU();

            drops_gpu.resize(drops.size());
            auto pack = [&](uint32_t start, uint32_t end)
            {
                for (uint32_t i = start; i < end; i++)
                {
                    const drop& d     = drops[i];
                    CarRainDropGpu& g = drops_gpu[i];
                    if (d.mass <= 0.0f)
                    {
                        g.radius = 0.0f;
                        continue;
                    }

                    const uint32_t axis = d.face / 2;
                    const face_rect& r  = faces[d.face];
                    g.u      = static_cast<float>(r.x) + (get(d.position, axis_u(axis)) - get(box_min, axis_u(axis))) / texel_size + 1.0f;
                    g.v      = static_cast<float>(r.y) + (get(d.position, axis_v(axis)) - get(box_min, axis_v(axis))) / texel_size + 1.0f;
                    g.radius = radius_of(d.mass);
                    g.seed   = d.seed;
                    g.lean_x = d.lean.x;
                    g.lean_y = d.lean.y;
                    g.lean_z = d.lean.z;
                    g.speed  = d.velocity.Length();
                }
            };
            if (!drops.empty())
            {
                ThreadPool::ParallelLoop(pack, static_cast<uint32_t>(drops.size()));
            }

            // hand this frame's micro water changes over, what does not fit waits for the next frame
            texels_frame.clear();
            const size_t count = min(texels_pending.size(), static_cast<size_t>(CarRain::texels_max));
            texels_frame.assign(texels_pending.begin(), texels_pending.begin() + count);
            for (const CarRainTexelGpu& change : texels_frame)
            {
                pending_slot[change.texel] = -1;
            }
            texels_pending.erase(texels_pending.begin(), texels_pending.begin() + count);
            for (size_t i = 0; i < texels_pending.size(); i++)
            {
                pending_slot[texels_pending[i].texel] = static_cast<int32_t>(i);
            }
        }
    }

    void CarRain::Tick(Entity* root, const Conditions& conditions, float delta_time)
    {
        SP_PROFILE_CPU();

        texels_frame.clear();
        if (!root)
        {
            active = false;
            return;
        }

        if (root != baked_root)
        {
            baked_root = root;
            if (!bake(root))
            {
                active = false;
                return;
            }
            prewarm(conditions);
            state_version++;
        }

        active = true;
        if (delta_time > 0.0f)
        {
            const uint32_t steps = max(1u, static_cast<uint32_t>(ceilf(delta_time / step_max)));
            for (uint32_t i = 0; i < steps; i++)
            {
                step(conditions, delta_time / static_cast<float>(steps));
            }
        }
        build_gpu();
    }

    void CarRain::Clear()
    {
        baked_root = nullptr;
        active     = false;
        drops.clear();
        drops_free.clear();
        drops_gpu.clear();
        texels_pending.clear();
        texels_frame.clear();
        surface.clear();
        height.clear();
        texel_info.clear();
        face_texels.clear();
        micro.clear();
        occupant.clear();
        pending_slot.clear();
        atlas_w = 0;
        atlas_h = 0;
    }

    bool CarRain::IsActive()
    {
        return active && atlas_w > 0;
    }

    uint32_t CarRain::GetVersion()
    {
        return state_version;
    }

    uint32_t CarRain::GetAtlasWidth()
    {
        return atlas_w;
    }

    uint32_t CarRain::GetAtlasHeight()
    {
        return atlas_h;
    }

    float CarRain::GetTexelSize()
    {
        return texel_size;
    }

    Vector3 CarRain::GetBoxMin()
    {
        return box_min;
    }

    Vector4 CarRain::GetFaceRect(uint32_t face)
    {
        const face_rect& r = faces[min(face, 5u)];
        return Vector4(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w), static_cast<float>(r.h));
    }

    const vector<float>& CarRain::GetSurface()
    {
        return surface;
    }

    const vector<float>& CarRain::GetMicro()
    {
        return micro;
    }

    float CarRain::GetClock()
    {
        return clock;
    }

    float CarRain::GetMicroLife()
    {
        return micro_life;
    }

    float CarRain::GetResidueMass()
    {
        return residue_mg();
    }

    const vector<CarRainDropGpu>& CarRain::GetDrops()
    {
        return drops_gpu;
    }

    const vector<CarRainTexelGpu>& CarRain::GetTexels()
    {
        return texels_frame;
    }
}
