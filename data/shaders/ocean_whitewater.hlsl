/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_OCEAN_WHITEWATER
#define SPARTAN_OCEAN_WHITEWATER

// whitewater where the sea works against a pile, a hull or a rock
// the search walks a disc on the water around the pixel, not a disc on the screen, so the foam keeps its width in
// metres at any view angle, each tap asks what opaque geometry the camera sees there and keeps it when it stands at
// the waterline within reach of the pixel, measured sideways and in height on their own, one 3d distance cannot tell
// a pile half a metre away from a wall half a metre down
float ocean_contact_foam(float3 water_position, float footprint, float wave_activity)
{
    // Only a crest running up a solid face leaves a contact fringe. A body entering the
    // water already generates impact foam through the displacement field.
    float crest    = max(water_position.y - buffer_frame.ocean_sea_level, 0.0f);
    float activity = smoothstep(0.04f, 0.25f, wave_activity) * smoothstep(0.25f, 1.25f, crest / max(wave_activity, 0.01f));
    if (activity <= 0.0f)
        return 0.0f;

    float reach    = min(crest, 0.35f);
    float resolved = smoothstep(0.75f, 2.0f, reach / max(footprint, 1e-4f));
    if (resolved <= 0.0f)
    {
        return 0.0f;
    }

    float2 wind_axis = ocean_foam_axis();
    float nearest    = reach;
    [loop] for (uint i = 0; i < 20u; ++i)
    {
        // golden angle spiral, even in area so a thin pile cannot hide between the taps
        float angle   = (float)i * 2.39996323f;
        float radius  = reach * sqrt(((float)i + 0.5f) / (float)20u);
        float3 probe  = water_position + float3(cos(angle), 0.0f, sin(angle)) * radius;
        float2 tap_uv = world_to_uv(probe);
        if (!is_valid_uv(tap_uv))
        {
            continue;
        }
        float depth = tex4.SampleLevel(samplers[sampler_point_clamp], tap_uv, 0.0f).r;
        if (depth <= 1e-5f)
        {
            continue;
        }

        float3 contact = get_position(depth, tap_uv);
        float2 away    = contact.xz - water_position.xz;
        float beside   = length(away);
        float above    = contact.y - water_position.y;
        // the shaft above the water and the shaft seen through it both mark where the object stands in the surface
        float at_surface = above >= 0.0f ? 1.0f - smoothstep(0.0f, reach, above) : 1.0f - smoothstep(0.0f, reach, -above);
        if (at_surface <= 0.0f || beside >= nearest)
        {
            continue;
        }

        // the terrain already owns a world space wash, a second camera dependent rim does not belong on the same beach
        float bed_valid;
        float bed = sample_ocean_terrain_height(contact.xz, bed_valid);
        if (bed_valid > 0.5f && abs(contact.y - bed) < 0.15f)
        {
            continue;
        }

        // the side the wind drives the sea against throws its wash further than the lee,
        // geometry that only brushes the waterline band counts as further away
        float windward = saturate(0.5f + 0.5f * dot(away / max(beside, 1e-3f), wind_axis));
        nearest        = min(nearest, beside / (at_surface * lerp(0.8f, 1.0f, windward)));
    }
    if (nearest >= reach)
    {
        return 0.0f;
    }

    float2 drift = buffer_frame.wind.xz * (float)buffer_frame.time * 0.015f;
    float coverage = saturate(1.0f - nearest / reach);
    return ocean_foam_raft(0.45f * coverage * coverage * activity * resolved, water_position.xz - drift, wind_axis, footprint);
}

void ocean_whitewater(Surface surface, out float foam, out float foam_churn)
{
    foam = 0.0f;
    foam_churn = 0.0f;

    if (surface.is_water() && buffer_frame.ocean_enabled > 0.5f)
    {
        // recover the undisplaced fft grid so foam sits on the crest that produced it
        float2 grid_xz = get_ocean_grid_xz(surface.position.xz);
        float view_distance = length(surface.position - get_camera_position());
        float wave_activity;
        foam = get_ocean_foam(grid_xz, surface.position, view_distance, wave_activity, foam_churn);
        float contact = ocean_contact_foam(surface.position, ocean_foam_footprint(view_distance, surface.position), wave_activity);
        foam = max(foam, contact);
        foam_churn = max(foam_churn, contact);
    }
}

float ocean_shore_coverage(Surface surface, float2 edge_uv)
{
    float  bed_raw  = tex4.SampleLevel(samplers[sampler_point_clamp], edge_uv, 0.0f).r;
    float  column   = bed_raw > 0.0f ? surface.position.y - get_position(bed_raw, edge_uv).y : 10.0f;
    float  lobes    = ocean_foam_noise(surface.position.xz * 0.45f) * 0.65f + ocean_foam_noise(surface.position.xz * 1.7f) * 0.35f;
    return smoothstep(0.0f, 0.006f + 0.04f * lobes, column);
}
#endif
