// Temporary chassis contact, independent of persistent wheel pressure.
// Matches GrassInteraction::Body: planes are in the rendered chassis frame.
#define GRASS_BODY_HULLS 6
#define GRASS_BODY_PLANES 48
// Float4 elements keep the SRV stride within both APIs' structured-buffer limits.
StructuredBuffer<float4> grass_bodies : register(t61);
struct GrassBody
{
    float4 center;
    float4 right;
    float4 up;
    float4 forward;
    uint offset;
};

GrassBody grass_body_load(uint frame)
{
    GrassBody body;
    body.offset = frame * (4 + GRASS_BODY_HULLS + GRASS_BODY_HULLS * GRASS_BODY_PLANES);
    body.center = grass_bodies[body.offset];
    body.right = grass_bodies[body.offset + 1];
    body.up = grass_bodies[body.offset + 2];
    body.forward = grass_bodies[body.offset + 3];
    return body;
}

float3 grass_body_local(float3 p, GrassBody body)
{
    return float3(dot(p, body.right.xyz), dot(p, body.up.xyz), dot(p, body.forward.xyz));
}

float3 grass_body_world(float3 p, GrassBody body)
{
    return body.right.xyz * p.x + body.up.xyz * p.y + body.forward.xyz * p.z;
}

float3 grass_body_rotate(float3 v, float3 from, float3 to)
{
    float3 axis = cross(from, to);
    return v + cross(axis, v) + cross(axis, cross(axis, v)) / max(1.0 + dot(from, to), 0.0001);
}

void bend_grass_around_body(GrassBody body, float3 root, float blade_reach, inout float3 position,
                            inout float3 normal, inout float3 tangent)
{
    if (body.center.w == 0.0)
        return;

    float3 local_root = grass_body_local(root - body.center.xyz, body);
    float3 outside = max(abs(local_root) - float3(body.right.w, body.up.w, body.forward.w), 0.0);
    if (dot(outside, outside) > (blade_reach + 0.04) * (blade_reach + 0.04))
        return;

    float3 p = grass_body_local(position - body.center.xyz, body);
    float3 original = p - local_root;
    float radius = length(original);
    if (radius < 0.001)
        return;

    // Each root keeps a separating plane against the fitted hull. A blade that
    // stays outside any such face is untouched, including empty bumper corners.
    [loop] for (uint pass = 0; pass < 2; ++pass)
    {
        [loop] for (uint hull = 0; hull < (uint)body.center.w; ++hull)
        {
            float4 contact_plane = 0.0;
            float plane_weight = 0.0;
            bool root_outside = false;
            bool miss = false;
            float4 descriptor = grass_bodies[body.offset + 4 + hull];
            uint first = (uint)descriptor.x;
            uint count = (uint)descriptor.y;
            [loop] for (uint face = 0; face < count; ++face)
            {
                float4 plane = grass_bodies[body.offset + 4 + GRASS_BODY_HULLS + first + face];
                // A two-centimetre skin covers the ribbon without clearing a halo.
                float skin = min(0.02, max(0.0, dot(plane.xyz, local_root) + plane.w) * 0.5);
                plane.w -= skin;
                float start = dot(plane.xyz, local_root) + plane.w;
                float end = dot(plane.xyz, p) + plane.w;
                root_outside = root_outside || start > 0.0;
                if (start > 0.0 && end >= 0.0) { miss = true; break; }
                if (start > 0.0)
                {
                    // Blend the separating faces instead of switching abruptly
                    // between a sill and the floor as a blade grows past an edge.
                    // Positive plane combinations still separate the convex hull.
                    float ratio = start / max(-end, 0.0001);
                    float weight = ratio * ratio;
                    contact_plane += plane * weight;
                    plane_weight += weight;
                }
            }
            if (miss || !root_outside || plane_weight < 0.000001)
                continue;

            contact_plane /= plane_weight;
            contact_plane /= max(length(contact_plane.xyz), 0.0001);
            float3 n = contact_plane.xyz;
            float clearance = max(0.0, dot(n, local_root) + contact_plane.w);
            float3 offset = p - local_root;
            float along = dot(offset, n);
            float target = max(along, -clearance);
            float3 lateral = offset - n * along;
            float3 outward = float3(local_root.x, 0.0, local_root.z);
            outward /= max(length(outward), 0.01);
            float3 guide = float3(0, 2, 0) + outward;
            guide -= n * dot(guide, n);
            guide /= max(length(guide), 0.0001);
            // Stabilize the bow when the incoming blade points into the surface
            // normal. A tiny lateral component must not choose a random side.
            lateral += guide * (target - along);
            float lateral_length = length(lateral);
            if (lateral_length < 0.0001)
            {
                // A blade directly below a flat floor still bows sideways.
                lateral = float3(local_root.x, 0.0, local_root.z);
                lateral -= n * dot(lateral, n);
                if (dot(lateral, lateral) < 0.0001)
                    lateral = abs(n.y) < 0.9 ? cross(n, float3(0, 1, 0)) : cross(n, float3(1, 0, 0));
                lateral_length = length(lateral);
            }
            // Rotate the blade around its planted root. Preserve its radial length
            // instead of projecting all vertices into a thin, disappearing sheet.
            p = local_root + n * target + lateral / lateral_length * sqrt(max(0.0, radius * radius - target * target));
        }
    }
    float3 bent = p - local_root;
    float3 from = grass_body_world(original / radius, body);
    float3 to = grass_body_world(bent / radius, body);
    normal = normalize(grass_body_rotate(normal, from, to));
    tangent = normalize(grass_body_rotate(tangent, from, to));
    position = root + grass_body_world(bent, body);
}
