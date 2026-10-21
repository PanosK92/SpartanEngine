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
#include "../commands/console/ConsoleCommands.h"
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

        // shape, an open panel in the airflow only keeps a film, a crevice the body closes over fills up and an edge is wiped
        constexpr float hold_splat          = 0.6f;   // share of a texel slurry can cake on an open panel
        constexpr float hold_dust           = 0.22f;  // share of a texel dust can cover on an open panel
        constexpr float hold_edge_loss      = 0.45f;  // of the capacity a sharp edge loses
        constexpr uint32_t erode_texels     = 24000;  // paint texels the air and the rain work on per frame, a sweep over the whole body
        constexpr float boundary_layer      = 0.6f;   // share of the free stream the deposit feels, as for the water on the car
        constexpr float wake_shelter        = 0.25f;  // faces looking downstream sit in separated, slow air
        constexpr float scour_threshold     = 5.0f;   // m/s of air skimming the paint before dry dust starts to lift
        constexpr float scour_rate          = 0.0012f; // per second per m/s over the threshold, for loose powder on an open panel
        constexpr float rain_fall_speed     = 7.0f;   // m/s
        constexpr float wash_rate           = 0.0004f; // per second per m/s of full rain striking an open panel
        constexpr float rain_wet_seconds    = 60.0f;  // rain soaks what it lands on for this long on the dirt clock

        // running drops, a drop lifts this share of the loose dirt of every texel it sweeps and carries it until it settles
        constexpr float drop_pickup         = 0.6f;
        constexpr float drop_load_max       = 3.0f;   // texels worth of dirt a drop can hold, a loaded drop slides over the rest
        constexpr float tide_mark_min       = 0.02f;  // texels worth of carried dirt a settling drop needs to leave a mark

        // soiling in place, a parked car driven through spray and dust at this speed until it is as dirty as asked
        constexpr float soil_speed          = 15.0f;  // m/s
        constexpr uint32_t soil_passes_max  = 400;
        constexpr uint32_t soil_grains      = 200000; // per pass
        constexpr uint32_t soil_splats      = 300;    // per pass

        // paint damage, a cut goes through the clearcoat, the basecoat and the primer down to the steel
        constexpr float metal_depth         = 0.85f;  // cut depth where the steel shows
        constexpr float groove_hold         = 0.8f;   // capacity a fully cut texel adds, dirt packs into the grooves
        constexpr float groove_grip         = 0.6f;   // of the airflow, the rain and the drops a groove keeps off the dirt in it
        constexpr float groove_silt         = 0.12f;  // share of a passing drop's load a fully cut texel catches
        constexpr float grit_cut            = 1.2f;   // extra cut caked grit under a scrape adds, it works like sandpaper
        constexpr float plough_share        = 0.7f;   // of the dirt in its path a scrape pushes along and to the sides
        constexpr float rust_seconds        = 240.0f; // wet seconds bare steel takes to rust through
        constexpr float scrape_tolerance    = 0.03f;  // metres the scraping point may sit off the paint as it follows the curvature
        constexpr float scrape_gap_max      = 0.4f;   // metres of hollow or gap a scrape bridges before it has left the car

        float soil_target   = -1.0f; // the coverage car.dirt asked for, applied once the atlas exists
        bool soil_requested = false;
        float scrape_amount     = 0.0f;
        bool scrape_requested   = false;

        TConsoleVar<float> cvar_dirt("car.dirt", 0.0f, "soils the occupied car in place as if driven off road, 0 washes it, 1 a long session, -1 shows the shelter and -2 the edges the dirt reads",
            [](const CVarVariant& value)
            {
                soil_target    = std::get<float>(value);
                soil_requested = true;
            });
        TConsoleVar<float> cvar_dirt_moisture("car.dirt_moisture", 0.5f, "what car.dirt soils with, 0 dry dust to 1 wet slurry");
        TConsoleVar<float> cvar_scratch("car.scratch", 0.0f, "sideswipes the right flank of the occupied car in place, 0 repairs the paint, 1 a hard scrape along a wall with a gouge to the steel",
            [](const CVarVariant& value)
            {
                scrape_amount    = std::get<float>(value);
                scrape_requested = true;
            });

        struct carried
        {
            float mass    = 0.0f;          // texels worth of coverage
            Vector3 color = Vector3::Zero; // linear albedo times mass
            float coarse  = 0.0f;          // coarseness times mass
        };
        vector<carried> carry; // per rain drop slot

        uint32_t rain_version = ~0u;
        uint32_t dirt_version = 0;
        Entity* owner         = nullptr;
        float clock           = 0.0f;
        bool active           = false;
        bool dirty            = false;
        float dust_remainder  = 0.0f;
        uint32_t erode_cursor = 0;
        uint64_t rng_state    = 0x853c49e6748fea9bull;

        vector<uint32_t> color;        // per texel, rgb sqrt encoded albedo and coverage, 8 bits each
        vector<uint32_t> state;        // per texel, wet until in 1/16 s of the clock << 8 | coarseness
        vector<uint32_t> wear;         // per texel, cut depth | groove direction << 8 | strands << 16 | rust << 24
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
                wear.assign(texel_count, 0u);
                pending_slot.assign(texel_count, -1);
                carry.assign(CarRain::drops_max, carried());
                pending.clear();
                frame.clear();
                dirty = false;
                dirt_version++;
            }
            return texel_count > 0;
        }

        float cut_of(uint32_t w)
        {
            return static_cast<float>(w & 0xff) / 255.0f;
        }

        float rust_of(uint32_t w)
        {
            return static_cast<float>(w >> 24) / 255.0f;
        }

        // dirt and cut paint wet out instead of beading, the drops of the rain catch on them
        void snag(uint32_t texel)
        {
            const float covered = static_cast<float>(color[texel] >> 24) / 255.0f;
            CarRain::SetTexelSnag(texel, covered * 0.9f + cut_of(wear[texel]) * 0.8f + rust_of(wear[texel]) * 0.4f);
        }

        void publish(uint32_t texel)
        {
            dirty = true;
            snag(texel);
            SurfaceDirtTexel change;
            change.texel = texel;
            change.color = color[texel];
            change.state = state[texel];
            change.wear  = wear[texel];
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

        // the gpu takes the whole atlas from the cpu copy, after the dirt or the paint was rewritten everywhere at once
        void reupload()
        {
            for (const SurfaceDirtTexel& change : pending)
            {
                pending_slot[change.texel] = -1;
            }
            pending.clear();
            frame.clear();
            for (uint32_t texel : CarRain::GetPaintTexels())
            {
                snag(texel);
            }
            dirt_version++;
        }

        // the share of the texel its shape lets a deposit cover, open is what an exposed flat panel holds, grooves fill up too
        float capacity(uint32_t texel, float open)
        {
            const float shelter = CarRain::GetTexelShelter(texel);
            const float edge    = CarRain::GetTexelConvexity(texel);
            return min(1.0f, (open + (1.0f - open) * sqrtf(shelter)) * (1.0f - hold_edge_loss * edge) + groove_hold * cut_of(wear[texel]));
        }

        void deposit(uint32_t texel, const Vector3& albedo, float amount, float moisture, float coarseness, float open)
        {
            if (texel >= color.size() || amount <= 0.0f)
                return;

            // blend in linear by how much of the texel the new material makes up
            const uint32_t c    = color[texel];
            const float r       = static_cast<float>(c & 0xff) / 255.0f;
            const float g       = static_cast<float>((c >> 8) & 0xff) / 255.0f;
            const float b       = static_cast<float>((c >> 16) & 0xff) / 255.0f;
            const float covered = static_cast<float>(c >> 24) / 255.0f;

            // what lands past the capacity does not stay, it is shed by the airflow and the vibration
            const float hold = max(capacity(texel, open), 0.05f);
            amount          *= clamp((hold - covered) / hold, 0.0f, 1.0f);
            if (amount <= 0.0f)
                return;

            const float share   = amount / max(covered + amount, 1e-4f);
            const Vector3 mixed = Vector3(r * r, g * g, b * b) * (1.0f - share) + albedo * share;
            color[texel] = quantize(sqrtf(max(mixed.x, 0.0f))) | (quantize(sqrtf(max(mixed.y, 0.0f))) << 8) | (quantize(sqrtf(max(mixed.z, 0.0f))) << 16) | (quantize(min(1.0f, covered + amount)) << 24);

            // fresh slurry wets what it lands on again, a dry grain never dries what is still wet
            const uint32_t s        = state[texel];
            const float coarse      = static_cast<float>(s & 0xff) / 255.0f;
            const uint32_t wet_old  = s >> 8;
            const uint32_t wet_new  = static_cast<uint32_t>(min((clock + moisture * wet_seconds) * 16.0f, 16777215.0f));
            state[texel] = (max(wet_old, moisture > 0.0f ? wet_new : 0u) << 8) | quantize(coarse + (coarseness - coarse) * share);
            publish(texel);
        }

        // one speck, an ellipse on the paint stretched along t, its rim broken so no two specks share an outline
        void stamp(const Vector3& centre, const Vector3& n, const Vector3& t, const Vector3& b, float radius, float elongation, float amount, const CarDirt::Deposit& d, float open)
        {
            const float s = CarRain::GetTexelSize();
            uint32_t texel;
            if (radius < s * 0.6f)
            {
                // smaller than a texel, it hides its share of it
                if (CarRain::FindTexel(centre, n, find_tolerance, texel))
                {
                    const float share = min(1.0f, (radius * radius * elongation) / (s * s * 0.36f));
                    deposit(texel, d.color, amount * share, d.moisture, d.coarseness, open);
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
                        deposit(texel, d.color, amount * (1.0f - 0.45f * e * e), d.moisture, d.coarseness, open);
                    }
                }
            }
        }

        // the air and the rain work on the deposits a slice of the body per frame, so what stays is what the shape protects
        // the free stream skims the open panels where the flow stays attached and lifts loose dry powder, a crust holds on
        // and wet slurry sticks until it dries, the rain strikes the faces it falls onto, soaks the dirt and rinses it away,
        // the hollows, the sills under the shoulder, the arches and the faces in the wake keep theirs
        void erode(const CarRain::Conditions& c, float dt)
        {
            const vector<uint32_t>& paint = CarRain::GetPaintTexels();
            if (paint.empty() || dt <= 0.0f)
                return;

            const Vector3 air     = c.wind - c.velocity;
            const float air_speed = air.Length();
            const Vector3 air_dir = air_speed > 0.01f ? air / air_speed : Vector3::Zero;
            const float rain      = c.rain * c.exposure;
            const Vector3 falling = c.gravity.Normalized() * rain_fall_speed + c.wind - c.velocity;

            const uint32_t count = min(static_cast<uint32_t>(paint.size()), erode_texels);
            const float span     = dt * static_cast<float>(paint.size()) / static_cast<float>(count);
            const uint32_t wet_rain = static_cast<uint32_t>(min((clock + rain_wet_seconds) * 16.0f, 16777215.0f));
            for (uint32_t k = 0; k < count; k++)
            {
                erode_cursor         = erode_cursor + 1 < paint.size() ? erode_cursor + 1 : 0;
                const uint32_t texel = paint[erode_cursor];
                const uint32_t c_old = color[texel];
                const uint32_t w_old = wear[texel];
                const float cut      = cut_of(w_old);
                const bool steel     = cut >= metal_depth && rust_of(w_old) < 1.0f;
                if ((c_old >> 24) == 0 && !steel)
                    continue;

                const Vector3 n      = CarRain::GetTexelNormal(texel);
                const float open     = (1.0f - CarRain::GetTexelShelter(texel)) * (1.0f - groove_grip * cut);
                const float edge     = CarRain::GetTexelConvexity(texel);
                const uint32_t s_old = state[texel];
                const float coarse   = static_cast<float>(s_old & 0xff) / 255.0f;
                const bool wet       = static_cast<float>(s_old >> 8) / 16.0f > clock;

                float rate = 0.0f;
                if (!wet)
                {
                    const Vector3 flow = air - n * air.Dot(n);
                    const float shear  = flow.Length() * boundary_layer * (air_dir.Dot(n) > 0.2f ? wake_shelter : 1.0f);
                    rate += scour_rate * max(0.0f, shear - scour_threshold) * (1.0f - 0.75f * coarse) * open * open * (1.0f + edge);
                }

                const float strike = rain > 0.0f ? max(0.0f, -n.Dot(falling)) : 0.0f;
                if (strike > 0.0f)
                {
                    rate += wash_rate * rain * strike * powf(open, 1.5f) * (wet ? 1.5f : 1.0f) * (1.0f - 0.5f * coarse) * (1.0f + edge);
                }

                // bare steel rusts while water or wet mud sits on it, faster under the mud that keeps it damp
                if (steel && (wet || strike > 0.0f))
                {
                    const float covered = static_cast<float>(c_old >> 24) / 255.0f;
                    const float rust    = min(1.0f, rust_of(w_old) + span / rust_seconds * (1.0f + covered));
                    wear[texel]         = (w_old & 0x00ffffffu) | (quantize(rust) << 24);
                }
                if ((c_old >> 24) != 0 && rate > 0.0f)
                {
                    const float covered = static_cast<float>(c_old >> 24) / 255.0f * expf(-rate * span);
                    color[texel]        = (c_old & 0x00ffffffu) | (quantize(covered) << 24);
                }
                if (strike > 0.0f)
                {
                    state[texel] = (max(s_old >> 8, wet_rain) << 8) | (s_old & 0xffu);
                }
                if (color[texel] != c_old || state[texel] != s_old || wear[texel] != w_old)
                {
                    publish(texel);
                }
            }
        }
        // car local, the impact of one clump of spray
        void splat_local(const Vector3& p, const Vector3& normal, const Vector3& v, const CarDirt::Deposit& d, float mass)
        {
            const Vector3 n   = normal.Normalized();
            Vector3 t         = v - n * v.Dot(n);
            const float slide = t.Length();
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
            stamp(p, n, t, b, core, 1.0f + min(2.5f, slide * 0.07f) * (0.4f + 0.6f * wet), (0.45f + 0.5f * wet), d, hold_splat);

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
                stamp(p + t * along + b * across, n, t, b, radius, 1.0f + 1.5f * wet * min(1.0f, slide * 0.1f), amount, d, hold_splat);
            }

            // the dry part of a clump puffs into powder that settles in a halo around the impact
            const uint32_t powder = static_cast<uint32_t>(28.0f * size * (1.0f - wet) + random01());
            for (uint32_t i = 0; i < powder; i++)
            {
                const Vector3 offset = t * (random_gauss() * 0.025f + 0.01f) + b * (random_gauss() * 0.02f);
                uint32_t texel;
                if (CarRain::FindTexel(p + offset, n, find_tolerance, texel))
                {
                    deposit(texel, d.color, 0.04f + 0.05f * random01(), 0.0f, 0.0f, hold_dust);
                }
            }
        }

        // how likely a grain of the cloud the tires raise settles on a texel, car local
        float dust_weight(uint32_t texel, const Vector3* contacts, uint32_t contact_count, const Vector3& travel, float bottom, float& wake)
        {
            const Vector3 nrm = CarRain::GetTexelNormal(texel);
            const Vector3 pt  = CarRain::GetTexelPoint(texel);

            // the cloud hugs the ground and the wake behind the car curls it back onto the rear faces
            const float height = pt.y - bottom;
            const float low    = expf(-height / 0.35f);
            wake               = max(0.0f, -nrm.Dot(travel)) * (0.15f + 0.85f * low);
            float wheel        = 0.0f;
            for (uint32_t i = 0; i < contact_count; i++)
            {
                const Vector3 offset = pt - contacts[i];
                const float distance = sqrtf(offset.x * offset.x + offset.z * offset.z);
                const float behind   = offset.Dot(travel) < 0.0f ? 1.0f : 0.35f;
                wheel = max(wheel, expf(-distance / 0.45f - height / 0.5f) * behind);
            }

            // the air slows in the hollows the body closes over and drops its dust there, an edge sheds it to the stream
            const float shelter = CarRain::GetTexelShelter(texel);
            const float edge    = CarRain::GetTexelConvexity(texel);
            const float settle  = max(0.0f, nrm.y) * 0.06f;
            return (0.02f + 0.9f * low + 0.6f * wake + 1.2f * wheel + settle) * (0.35f + 1.3f * shelter) * (1.0f - 0.6f * edge);
        }

        // how likely spray thrown off the tread reaches a texel, car local, it flies low and backwards from the contact patches
        // and only reaches what faces it, the sills, the arches, the doors behind the front wheels and the rear valance
        float spray_weight(uint32_t texel, const Vector3* contacts, uint32_t contact_count, const Vector3& travel, float bottom)
        {
            const Vector3 nrm  = CarRain::GetTexelNormal(texel);
            const Vector3 pt   = CarRain::GetTexelPoint(texel);
            const float height = pt.y - bottom;
            float wheel        = 0.0f;
            for (uint32_t i = 0; i < contact_count; i++)
            {
                const Vector3 offset = pt - contacts[i];
                const float along    = -offset.Dot(travel);
                const float distance = sqrtf(offset.x * offset.x + offset.z * offset.z);
                wheel = max(wheel, expf(-distance / (along > 0.0f ? 0.9f : 0.35f) - height / 0.45f));
            }
            const float low    = expf(-height / 0.3f);
            const float facing = max(0.0f, -nrm.Dot(travel)) * 0.6f + fabsf(nrm.x) * 0.4f + max(0.0f, -nrm.y) * 0.5f;
            return (1.6f * wheel + 0.5f * low) * (0.25f + facing);
        }

        void dust_local(const Vector3* contacts, uint32_t contact_count, const Vector3& travel, const CarDirt::Deposit& d, uint32_t count)
        {
            const vector<uint32_t>& paint = CarRain::GetPaintTexels();
            const float bottom            = CarRain::GetBoxMin().y;
            for (uint32_t k = 0; k < count; k++)
            {
                const uint32_t texel = paint[min(static_cast<uint32_t>(random01() * static_cast<float>(paint.size())), static_cast<uint32_t>(paint.size()) - 1)];
                float wake           = 0.0f;
                if (random01() * 2.4f > dust_weight(texel, contacts, contact_count, travel, bottom, wake))
                    continue;

                // the wake keeps the dust on the rear faces, so they cake even where they are flat
                const Vector3 tint = d.color * (0.88f + 0.24f * random01());
                deposit(texel, tint, 0.022f * (0.5f + random01()), d.moisture, d.coarseness, min(1.0f, hold_dust + 0.6f * wake));
            }
        }

        // the direction of a groove in the projection plane of the texel's own map, 0 to 255 over half a turn, the gpu draws
        // the strands along it in the same plane so a stroke stays one continuous line from texel to texel
        uint32_t groove_direction(uint32_t texel, const Vector3& t)
        {
            const uint32_t width = CarRain::GetAtlasWidth();
            const float x        = static_cast<float>(texel % width);
            const float y        = static_cast<float>(texel / width);
            const Vector3 n      = CarRain::GetTexelNormal(texel);
            uint32_t axis        = fabsf(n.x) > fabsf(n.y) ? (fabsf(n.x) > fabsf(n.z) ? 0u : 2u) : (fabsf(n.y) > fabsf(n.z) ? 1u : 2u);
            for (uint32_t face = 0; face < 6; face++)
            {
                const Vector4 rect = CarRain::GetFaceRect(face);
                if (x >= rect.x && x < rect.x + rect.z && y >= rect.y && y < rect.y + rect.w)
                {
                    axis = face / 2;
                    break;
                }
            }
            const float u = axis == 0 ? t.y : t.x;
            const float v = axis == 2 ? t.y : t.z;
            float angle   = atan2f(v, u);
            if (angle < 0.0f)
            {
                angle += 3.14159265f;
            }
            return static_cast<uint32_t>(angle / 3.14159265f * 256.0f) & 0xffu;
        }

        // cuts deeper into a texel, every pass through the same paint goes further, the latest strong stroke sets the grain
        void cut(uint32_t texel, float amount, const Vector3& t, float strands)
        {
            if (texel >= wear.size() || amount <= 0.0f)
                return;

            const uint32_t w      = wear[texel];
            const float old       = cut_of(w);
            const float now       = 1.0f - (1.0f - old) * (1.0f - min(amount, 1.0f));
            const uint32_t grain  = amount >= old * 0.5f ? groove_direction(texel, t) : ((w >> 8) & 0xffu);
            const uint32_t strand = max((w >> 16) & 0xffu, quantize(strands));
            wear[texel] = max(w & 0xffu, quantize(now)) | (grain << 8) | (strand << 16) | (w & 0xff000000u);
            publish(texel);
        }

        // the scraper pushes the dirt in its path along, what it lifts is carried in pile
        void plough(uint32_t texel, carried& pile)
        {
            const uint32_t c_old = color[texel];
            const float covered  = static_cast<float>(c_old >> 24) / 255.0f;
            if (covered <= 0.0f)
                return;

            const float take = covered * plough_share;
            const float r    = static_cast<float>(c_old & 0xff) / 255.0f;
            const float g    = static_cast<float>((c_old >> 8) & 0xff) / 255.0f;
            const float b    = static_cast<float>((c_old >> 16) & 0xff) / 255.0f;
            pile.mass       += take;
            pile.color      += Vector3(r * r, g * g, b * b) * take;
            pile.coarse     += static_cast<float>(state[texel] & 0xff) / 255.0f * take;
            color[texel]     = (c_old & 0x00ffffffu) | (quantize(covered - take) << 24);
            publish(texel);
        }

        CarDirt::Deposit pile_deposit(const carried& pile)
        {
            CarDirt::Deposit d;
            d.color      = pile.color / max(pile.mass, 1e-4f);
            d.coarseness = pile.coarse / max(pile.mass, 1e-4f);
            d.moisture   = 0.3f;
            return d;
        }

        // the paint under a point, through the map the normal looks along and then the other maps facing the same side, a
        // curved flank tucks under and the map its normal picks can see another panel further out
        bool find_paint(const Vector3& p, const Vector3& n, uint32_t& texel)
        {
            if (CarRain::FindTexel(p, n, scrape_tolerance, texel))
                return true;

            const Vector3 axes[6] = { Vector3::Right, Vector3::Left, Vector3::Up, Vector3::Down, Vector3::Forward, Vector3::Backward };
            float best_dot[2]     = { 0.0f, 0.0f };
            int32_t best[2]       = { -1, -1 };
            for (int32_t i = 0; i < 6; i++)
            {
                const float d = axes[i].Dot(n);
                if (d > best_dot[0])
                {
                    best_dot[1] = best_dot[0];
                    best[1]     = best[0];
                    best_dot[0] = d;
                    best[0]     = i;
                }
                else if (d > best_dot[1])
                {
                    best_dot[1] = d;
                    best[1]     = i;
                }
            }
            for (int32_t i = 0; i < 2; i++)
            {
                if (best[i] >= 0 && best_dot[i] > 0.15f && CarRain::FindTexel(p, axes[best[i]], scrape_tolerance, texel))
                    return true;
            }
            return false;
        }

        // car local, something hard drags over the paint from p along heading, the stroke follows the curvature of the body
        // and keeps its heading the way a wall or a post does, it bites in, chatters and trails off, grit under it cuts deeper,
        // the dirt it meets is ploughed into berms on both sides and a smear where it lets go
        float scrape_local(Vector3 p, const Vector3& normal, const Vector3& heading, float length, float width, float depth, float strands)
        {
            const float s = CarRain::GetTexelSize();
            Vector3 n     = normal.Normalized();
            Vector3 t     = heading - n * heading.Dot(n);
            Vector3 b     = n.Cross(t.Normalized());
            uint32_t texel;
            if (t.LengthSquared() < 1e-6f || !find_paint(p, n, texel))
                return 0.0f;

            carried pile;
            const float phase   = random01() * 6.2831853f;
            const float chatter = 40.0f + 60.0f * random01();
            const float step    = s * 0.5f;
            const int32_t reach = static_cast<int32_t>(ceilf(width / s));
            // a hollow or a panel gap the straight edge passes over is skipped, it bites again where the paint comes back
            float walked = 0.0f;
            float gap    = 0.0f;
            for (; walked <= length; walked += step)
            {
                if (!find_paint(p, n, texel))
                {
                    gap += step;
                    if (gap > scrape_gap_max)
                        break;
                    p += t.Normalized() * step;
                    continue;
                }
                gap = 0.0f;

                // back onto the paint and into its tangent plane
                n  = CarRain::GetTexelNormal(texel);
                p -= n * n.Dot(p - CarRain::GetTexelPoint(texel));
                t  = heading - n * heading.Dot(n);
                if (t.LengthSquared() < 1e-6f)
                    break;
                t.Normalize();
                b = n.Cross(t).Normalized();

                const float e       = walked / max(length, 1e-4f);
                const float profile = min(1.0f, e / 0.05f) * min(1.0f, (1.0f - e) / 0.25f + 0.2f) * (0.75f + 0.25f * sinf(walked * chatter + phase));
                const float covered = static_cast<float>(color[texel] >> 24) / 255.0f;
                const float coarse  = static_cast<float>(state[texel] & 0xff) / 255.0f;
                const float grit    = 1.0f + grit_cut * covered * (0.4f + coarse);
                for (int32_t i = -reach; i <= reach; i++)
                {
                    const float o = static_cast<float>(i) * s;
                    const float r = reach > 0 ? o / max(width, s) : 0.0f;
                    uint32_t across;
                    if (fabsf(r) > 1.0f || !find_paint(p + b * o, n, across))
                        continue;

                    plough(across, pile);
                    cut(across, depth * profile * grit * sqrtf(1.0f - r * r), t, strands);
                }

                // the berms, a little of what is pushed along spills off both edges of the contact
                if (pile.mass > 0.05f)
                {
                    const CarDirt::Deposit d = pile_deposit(pile);
                    for (float side = -1.0f; side <= 1.0f; side += 2.0f)
                    {
                        uint32_t berm;
                        if (find_paint(p + b * (side * (width + s * 1.5f)), n, berm))
                        {
                            const float spill = pile.mass * 0.06f;
                            deposit(berm, d.color, spill, d.moisture, d.coarseness, 1.0f);
                            pile.mass   -= spill;
                            pile.color  -= d.color * spill;
                            pile.coarse -= d.coarseness * spill;
                        }
                    }
                }
                p += t * step;
            }

            // where it lets go the rest of the pile stays as a smear
            if (pile.mass >= tide_mark_min)
            {
                stamp(p, n, t, b, s * sqrtf(pile.mass / (0.6f * 3.14159265f)) * 1.2f, 1.8f, 0.8f, pile_deposit(pile), 1.0f);
            }
            return min(walked, length);
        }

        // the right flank against a wall in place, amount 1 is a hard scrape the length of the car with a gouge to the steel
        void sideswipe(float amount)
        {
            if (CarRain::GetPaintTexels().empty())
                return;

            if (amount <= 0.0f)
            {
                fill(wear.begin(), wear.end(), 0u);
                reupload();
                SP_LOG_INFO("car paint, repaired");
                return;
            }

            const Vector3 lo         = CarRain::GetBoxMin();
            const Vector3 hi         = CarRain::GetBoxMax();
            const float height       = hi.y - lo.y;
            const float span         = hi.z - lo.z;
            const float step         = CarRain::GetTexelSize() * 2.0f;
            const uint32_t strokes   = 2 + static_cast<uint32_t>(min(amount, 2.0f) * 5.0f);
            uint32_t texel;
            float asked  = 0.0f;
            float walked = 0.0f;
            vector<Vector3> points;
            vector<Vector3> normals;
            for (uint32_t k = 0; k < strokes; k++)
            {
                // the wall slides back along the car at one height, the driver wavers a little
                const float y0     = lo.y + (0.22f + 0.38f * random01()) * height;
                const float rise   = (random01() - 0.5f) * 0.1f;
                const float z0     = hi.z - random01() * 0.35f * span;
                const float length = min(z0 - lo.z, (0.35f + 0.4f * min(amount, 1.5f)) * span * (0.6f + 0.4f * random01()));
                const float width  = 0.003f + 0.035f * amount * random01() * random01();
                const float depth  = 0.15f + 0.6f * amount * random01();
                const Vector3 heading = Vector3(0.0f, rise, -1.0f).Normalized();

                // the flank under the wall, outermost paint first
                points.clear();
                normals.clear();
                float widest = -FLT_MAX;
                for (float d = 0.0f; d <= length; d += step)
                {
                    const Vector3 probe(hi.x + 0.05f, y0 + rise * d, z0 - d);
                    const bool found = CarRain::FindTexel(probe, Vector3::Right, hi.x - lo.x, texel);
                    points.push_back(found ? CarRain::GetTexelPoint(texel) : Vector3(-FLT_MAX, 0.0f, 0.0f));
                    normals.push_back(found ? CarRain::GetTexelNormal(texel) : Vector3::Right);
                    widest = max(widest, points.back().x);
                }
                if (widest == -FLT_MAX)
                    continue;

                // a flat wall only meets the bulges of the arches and the doors, a harder hit pushes it further into the recesses between them
                const float wall = widest - (0.008f + 0.025f * min(amount, 2.0f)) * (0.6f + 0.8f * random01());
                asked += length;
                for (size_t i = 0; i < points.size();)
                {
                    if (points[i].x < wall)
                    {
                        i++;
                        continue;
                    }
                    const size_t first = i;
                    while (i < points.size() && points[i].x >= wall)
                    {
                        i++;
                    }
                    walked += scrape_local(points[first], normals[first], heading, static_cast<float>(i - first) * step, width, depth, 0.2f + width * 18.0f);
                }
            }

            // a post caught the front corner, short and to the steel
            if (amount >= 0.5f && CarRain::FindTexel(Vector3(hi.x + 0.05f, lo.y + 0.45f * height, hi.z - 0.45f), Vector3::Right, hi.x - lo.x, texel))
            {
                asked  += 0.35f;
                walked += scrape_local(CarRain::GetTexelPoint(texel), CarRain::GetTexelNormal(texel), Vector3(0.0f, -0.2f, -1.0f).Normalized(), 0.35f, 0.012f, 1.5f * amount, 0.7f);
            }

            SP_LOG_INFO("car paint, %u scrapes walked %.2f of %.2f m and left %.4f of the paint cut and %.4f to the steel", strokes, walked, asked, CarDirt::GetDamage(0.0f), CarDirt::GetDamage(metal_depth));
        }

        // the car as if driven through spray and dust, in place, amount 1 is a long session off road
        // the same deposition as on the move, so the dirt lands where the shape puts it, only faster
        void soil(float target, float moisture)
        {
            const vector<uint32_t>& paint = CarRain::GetPaintTexels();
            if (paint.empty())
                return;

            fill(color.begin(), color.end(), 0u);
            fill(state.begin(), state.end(), 0u);
            for (carried& c : carry)
            {
                c = carried();
            }
            // negative targets paint the shape the dirt reads instead, -1 the shelter and -2 the convexity, as pale dust
            if (target < 0.0f)
            {
                const bool edges = target < -1.5f;
                for (uint32_t texel : paint)
                {
                    const float value = edges ? CarRain::GetTexelConvexity(texel) : CarRain::GetTexelShelter(texel);
                    color[texel]      = 0x00b0c8e0u | (static_cast<uint32_t>(value * 255.0f + 0.5f) << 24);
                }
            }

            uint32_t passes = 0;
            if (target > 0.0f)
            {
                // the wheels sit a fifth of the length in from the ends, a hand span inside the sides, where the tires throw from
                const Vector3 lo = CarRain::GetBoxMin();
                const Vector3 hi = CarRain::GetBoxMax();
                const float axle = (hi.z - lo.z) * 0.3f;
                const float side = (hi.x - lo.x) * 0.5f - 0.15f;
                const Vector3 centre((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);
                const Vector3 contacts[4] =
                {
                    centre + Vector3(-side, 0.0f, axle), centre + Vector3(side, 0.0f, axle),
                    centre + Vector3(-side, 0.0f, -axle), centre + Vector3(side, 0.0f, -axle)
                };
                const Vector3 travel = Vector3::Forward;
                const float bottom   = lo.y;

                CarDirt::Deposit slurry;
                slurry.moisture   = clamp(0.3f + 0.7f * moisture, 0.0f, 1.0f);
                slurry.coarseness = 0.5f;
                CarDirt::Deposit powder;
                powder.color      = slurry.color * 1.6f;
                powder.moisture   = 0.0f;
                powder.coarseness = 0.0f;

                passes = static_cast<uint32_t>(min(target, 1.0f) * static_cast<float>(soil_passes_max) + 0.5f);
                for (uint32_t pass = 0; pass < passes; pass++)
                {
                    dust_local(contacts, 4, travel, powder, static_cast<uint32_t>(soil_grains * (1.0f - moisture)));

                    // spray thrown off the tread, landing on the texels the tires can reach, hitting them along the travel
                    for (uint32_t i = 0; i < static_cast<uint32_t>(soil_splats * (0.3f + moisture)); i++)
                    {
                        const uint32_t texel = paint[min(static_cast<uint32_t>(random01() * static_cast<float>(paint.size())), static_cast<uint32_t>(paint.size()) - 1)];
                        if (random01() * 2.0f > spray_weight(texel, contacts, 4, travel, bottom))
                            continue;
                        const Vector3 n = CarRain::GetTexelNormal(texel);
                        const Vector3 v = -travel * soil_speed * (0.3f + 0.4f * random01()) - n * (2.0f + 6.0f * random01());
                        splat_local(CarRain::GetTexelPoint(texel), n, v, slurry, 0.5f + random01());
                    }
                }
            }

            SP_LOG_INFO("car dirt, %u passes of spray and dust left %.3f mean coverage", passes, CarDirt::GetCoverage());
            dirty = true;
            reupload();
        }

        // the running drops of the rain lift the dirt off the paths they take and leave it where they dry
        void wash()
        {
            const vector<CarRain::Wash>& washes = CarRain::GetWashes();
            if (washes.empty())
                return;

            const uint32_t wet_rain = static_cast<uint32_t>(min((clock + rain_wet_seconds) * 16.0f, 16777215.0f));
            for (const CarRain::Wash& w : washes)
            {
                if (w.drop >= carry.size() || w.other >= carry.size())
                    continue;

                carried& c = carry[w.drop];
                switch (w.kind)
                {
                    case CarRain::WashKind::Sweep:
                    {
                        if (w.texel >= color.size())
                            break;

                        // a groove across the path catches some of the silt the drop carries, scratches fill with mud lines
                        const float cut = cut_of(wear[w.texel]);
                        if (cut > 0.1f && c.mass > 0.01f)
                        {
                            const float silt  = c.mass * groove_silt * cut;
                            const Vector3 tint = c.color / c.mass;
                            const float grain = c.coarse / c.mass;
                            deposit(w.texel, tint, silt, 1.0f, grain, 1.0f);
                            c.mass   -= silt;
                            c.color  -= tint * silt;
                            c.coarse -= grain * silt;
                        }

                        const uint32_t c_old = color[w.texel];
                        const uint32_t s_old = state[w.texel];
                        const float covered  = static_cast<float>(c_old >> 24) / 255.0f;
                        if (covered <= 0.0f)
                        {
                            state[w.texel] = (max(s_old >> 8, wet_rain) << 8) | (s_old & 0xffu);
                            break;
                        }

                        // wet slurry runs with the water, a dry crust only gives up its loose grains, what sits in a groove stays
                        const float coarse = static_cast<float>(s_old & 0xff) / 255.0f;
                        const bool wet     = static_cast<float>(s_old >> 8) / 16.0f > clock;
                        const float room   = max(0.0f, 1.0f - c.mass / drop_load_max);
                        const float take   = covered * min(1.0f, drop_pickup * room * (wet ? 1.4f : 1.0f) * (1.0f - 0.5f * coarse) * (1.0f - groove_grip * cut));
                        if (take <= 0.0f)
                            break;
                        const float r      = static_cast<float>(c_old & 0xff) / 255.0f;
                        const float g      = static_cast<float>((c_old >> 8) & 0xff) / 255.0f;
                        const float b      = static_cast<float>((c_old >> 16) & 0xff) / 255.0f;
                        c.mass            += take;
                        c.color           += Vector3(r * r, g * g, b * b) * take;
                        c.coarse          += coarse * take;

                        color[w.texel] = (c_old & 0x00ffffffu) | (quantize(covered - take) << 24);
                        state[w.texel] = (max(s_old >> 8, wet_rain) << 8) | (s_old & 0xffu);
                        publish(w.texel);
                        break;
                    }
                    case CarRain::WashKind::Merge:
                    {
                        carried& o = carry[w.other];
                        c.mass    += o.mass;
                        c.color   += o.color;
                        c.coarse  += o.coarse;
                        o          = carried();
                        break;
                    }
                    case CarRain::WashKind::Settle:
                    {
                        // a tide mark, the mud gathers at the rim of the drop as it dries
                        if (c.mass >= tide_mark_min)
                        {
                            CarDirt::Deposit d;
                            d.color      = c.color / c.mass;
                            d.coarseness = c.coarse / c.mass;
                            d.moisture   = 1.0f;
                            const Vector3 n = CarRain::GetTexelNormal(w.texel);
                            const Vector3 t = n.Cross(fabsf(n.y) < 0.9f ? Vector3::Up : Vector3::Right).Normalized();
                            const float s   = CarRain::GetTexelSize();
                            stamp(CarRain::GetTexelPoint(w.texel), n, t, n.Cross(t), s * sqrtf(c.mass / (0.6f * 3.14159265f)), 1.0f, 0.6f, d, 1.0f);
                        }
                        c = carried();
                        break;
                    }
                    case CarRain::WashKind::Lost:
                    {
                        c = carried();
                        break;
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
        splat_local(to_car * (position - root->GetPosition()), to_car * normal, to_car * velocity, d, mass);
    }

    void CarDirt::Scrape(Entity* root, const Vector3& position, const Vector3& normal, const Vector3& slide, float severity, float length)
    {
        if (!sync(root) || severity <= 0.0f || length <= 0.0f)
            return;

        const Quaternion to_car = root->GetRotation().Inverse();
        const Vector3 n         = to_car * normal;
        Vector3 heading         = to_car * slide;
        if (heading.LengthSquared() < 1e-6f)
        {
            heading = n.Cross(fabsf(n.y) < 0.9f ? Vector3::Up : Vector3::Right);
        }
        const float width = 0.003f + 0.03f * severity;
        scrape_local(to_car * (position - root->GetPosition()), n, heading.Normalized(), length, width, 0.2f + 0.9f * severity, 0.2f + width * 18.0f);
    }

    void CarDirt::Dust(Entity* root, const Vector3* wheels, uint32_t wheel_count, const Vector3& velocity, const Deposit& d, float grains)
    {
        if (!sync(root) || grains <= 0.0f)
            return;

        if (CarRain::GetPaintTexels().empty())
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

        dust_remainder += grains;
        const uint32_t count = min(static_cast<uint32_t>(dust_remainder), dust_grains_max);
        dust_remainder       = min(dust_remainder - static_cast<float>(count), 1.0f);
        dust_local(contacts, wheel_count, travel, d, count);
    }

    void CarDirt::Tick(float delta_time, const CarRain::Conditions& conditions)
    {
        frame.clear();

        Entity* root = CarRain::GetBakedRoot();
        active       = root && CarRain::IsActive() && sync(root);
        if (!active)
            return;

        clock += max(delta_time, 0.0f);
        if (soil_requested)
        {
            soil_requested = false;
            soil(soil_target, clamp(cvar_dirt_moisture.GetValue(), 0.0f, 1.0f));
        }
        if (scrape_requested)
        {
            scrape_requested = false;
            sideswipe(scrape_amount);
        }
        wash();
        erode(conditions, delta_time);

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
        fill(wear.begin(), wear.end(), 0u);
        fill(carry.begin(), carry.end(), carried());
        reupload();
        dust_remainder = 0.0f;
        dirty          = false;
    }

    void CarDirt::Wash()
    {
        if (color.empty())
            return;

        fill(color.begin(), color.end(), 0u);
        fill(state.begin(), state.end(), 0u);
        fill(carry.begin(), carry.end(), carried());
        reupload();
        dust_remainder = 0.0f;
        SP_LOG_INFO("car paint, washed");
    }

    void CarDirt::Release()
    {
        color.clear();
        state.clear();
        wear.clear();
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

    float CarDirt::GetDamage(float depth)
    {
        const vector<uint32_t>& paint = CarRain::GetPaintTexels();
        if (paint.empty() || wear.empty())
            return 0.0f;

        uint32_t count = 0;
        for (uint32_t texel : paint)
        {
            count += texel < wear.size() && (wear[texel] & 0xffu) > 0 && cut_of(wear[texel]) >= depth ? 1 : 0;
        }
        return static_cast<float>(count) / static_cast<float>(paint.size());
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

    const vector<uint32_t>& CarDirt::GetWear()
    {
        return wear;
    }

    const vector<SurfaceDirtTexel>& CarDirt::GetTexels()
    {
        return frame;
    }
}
