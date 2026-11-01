/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_RESTIR_RESERVOIR
#define SPARTAN_RESTIR_RESERVOIR
#include "common_ray_hit.hlsl"
#include "rt_visibility.hlsl"
#include "restir_environment.hlsl"
#include "restir_material_factors.hlsl"

[[vk::image_format("unknown")]] RWTexture2D<float4> restir_specular_output : register(u68);

// ReSTIR PT Enhanced (2026).
static const uint RESTIR_MAX_PATH_LENGTH = 5;
// Section 6.1: each 8x8 tracing block draws from one presampled light tile.
static const uint RESTIR_LIGHT_TILE_COUNT = 128u;
static const uint RESTIR_LIGHT_TILE_SIZE = 1024u;
static const uint RESTIR_LIGHT_KIND_ENVIRONMENT = 0u;
static const uint RESTIR_LIGHT_KIND_EMISSIVE = 1u;
static const uint RESTIR_LIGHT_KIND_ANALYTIC = 2u;

// Equal default weights for the available light kinds, as in the reference.
// Use the actual integer tile allocation as the mixture PMF after rounding.
uint3 restir_light_kind_counts()
{
    uint emissive = uint(buffer_frame.restir_pt_emissive_tri_count > 0.5f);
    uint analytic = uint(buffer_frame.restir_pt_light_count > 0.5f);
    uint count = RESTIR_LIGHT_TILE_SIZE / (1u + emissive + analytic);
    return uint3(RESTIR_LIGHT_TILE_SIZE - count * (emissive + analytic), count * emissive, count * analytic);
}
float restir_light_kind_pmf(uint kind)
{
    return float(restir_light_kind_counts()[kind]) / float(RESTIR_LIGHT_TILE_SIZE);
}
uint restir_nee_candidate_count(uint vertex) { return max(32u / (vertex * vertex), 1u); }
float restir_light_kind_pmf_at_vertex(uint kind, uint vertex)
{
    uint count = restir_nee_candidate_count(vertex);
    uint stride = (RESTIR_LIGHT_TILE_SIZE + count - 1u) / count;
    uint draws = count * stride;
    // A ceil stride wraps the first few entries (two at depth three).
    // Those entries belong to the environment partition. Account for them
    // in the mixture density averaged over the full candidate set.
    uint extra = kind == RESTIR_LIGHT_KIND_ENVIRONMENT ? draws - RESTIR_LIGHT_TILE_SIZE : 0u;
    return float(restir_light_kind_counts()[kind] + extra) / float(draws);
}
float restir_environment_source_pdf(float3 direction, float3 normal, uint vertex)
{
    return restir_light_kind_pmf_at_vertex(RESTIR_LIGHT_KIND_ENVIRONMENT, vertex) * sky_nee_pdf_at(direction, normal);
}
struct RestirLightTileEntry
{
    // Exactly one light kind per entry. x valid: analytic; z valid: emissive;
    // both invalid: environment. y stores the full discrete selection PMF.
    uint4 lights;
    uint analytic_position;
    float3 environment_direction;
    float environment_pdf;
};
RWStructuredBuffer<RestirLightTileEntry> restir_light_tiles : register(u69);
float2 restir_analytic_sample_position(RestirLightTileEntry entry)
{
    return float2(entry.analytic_position & 65535u, entry.analytic_position >> 16u) / 65536.0f;
}
static uint restir_light_tile_index = 0u;
// LightTileRIS::GenerateSample uses one random offset and a fixed stride.
// Prefix replay consumes the same offset draw without evaluating candidates.
RestirLightTileEntry restir_light_tile_entry(float xi, uint proposal, uint count, inout uint offset)
{
    uint stride = (RESTIR_LIGHT_TILE_SIZE + count - 1u) / count;
    if (proposal == 0u)
        offset = min(uint(xi * float(stride)), stride - 1u);
    uint entry = (offset + proposal * stride) % RESTIR_LIGHT_TILE_SIZE;
    return restir_light_tiles[restir_light_tile_index * RESTIR_LIGHT_TILE_SIZE + entry];
}

// Every pass must sample all primary guides at this full-resolution texel centre.
// Reduced-resolution centres can land on full-resolution texel boundaries, where
// point sampling and integer G-buffer loads can select different surfaces.
// A filtered depth at a quarter-resolution texel center can place the ray inside
// a wall, causing self-intersection and repeated history rejection.
float2 restir_primary_uv(float2 uv)
{
    uint2 size;
    tex_depth.GetDimensions(size.x, size.y);
    uint2 pixel = min(uint2(uv * float2(size)), size - 1u);
    return (float2(pixel) + 0.5f) / float2(size);
}

float2 restir_primary_uv(uint2 pixel, uint2 sample_size)
{
    uint2 size;
    tex_depth.GetDimensions(size.x, size.y);
    return (float2(restir_guide_pixel(pixel, sample_size, size)) + 0.5f) / float2(size);
}

float3 restir_primary_position(float2 uv)
{
    float2 sample_uv = restir_primary_uv(uv);
    return get_position(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), sample_uv, 0).r, render_uv_to_screen_uv(sample_uv));
}

static const float RESTIR_C_CAP_DEFAULT      = 20.0f;
static const float RESTIR_C_CAP_DUPLICATED   = 1.0f;

// paired spatial reuse, lin 2026 3, three tileable self inverting gaussian pairing tables
// sizes are near coprime so the tiling periods never align within a screen
static const uint RESTIR_PAIRING_COUNT    = 3;
static const uint RESTIR_PAIRING_SIZES[3] = { 254u, 230u, 210u };
static const uint RESTIR_PAIRING_BASES[3] = { 0u, 64516u, 117416u };

// Section 5: decorrelation intentionally introduces a small, localized bias.
static const float RESTIR_DUPLICATION_ALPHA = 0.1f;
float get_restir_m_cap() { return RESTIR_C_CAP_DEFAULT; }
// duplication is the fraction of the 17x17 window carrying this pixel's replay seed
float get_restir_m_cap_decorrelated(float duplication)
{
    float d = pow(saturate(duplication), RESTIR_DUPLICATION_ALPHA);
    return lerp(get_restir_m_cap(), RESTIR_C_CAP_DUPLICATED, d);
}
uint  get_restir_max_path_length()     { return RESTIR_MAX_PATH_LENGTH; }
// One BSDF path tree per pixel; its light-ending leaves share the initial RIS.
uint  get_restir_initial_candidates()  { return 1u; }
// Reference isValidGeometry with useSymmetricCondition for paired spatial reuse.
static const float RESTIR_DEPTH_THRESHOLD    = 0.1f;
static const float RESTIR_NORMAL_THRESHOLD   = 0.5f;
static const float RESTIR_TEMPORAL_DECAY     = 1.0f;
// Scatter and guide rays already use restir_ray_origin. Match the reference's
// zero TMin; another world-space epsilon would skip nearby surfaces.
static const float RESTIR_RAY_T_MIN          = 0.0f;
// Match the reference's kRayTMax for scattering, replay and sky visibility.
// A finite scene-scale cutoff can turn a distant surface into a sky miss.
static const float RESTIR_RAY_T_MAX          = 1e30f;
// Denoising guide value for a miss, independent of the traversal interval.
static const float RESTIR_MISS_HIT_DISTANCE  = 10000.0f;

// emissive nits calibration, must match light_composition or emitters glow on screen but bounce no light
static const float RESTIR_EMISSIVE_NITS_FROM_ALBEDO = lighting_emissive_nits_from_albedo;
static const float RESTIR_EMISSIVE_NITS_TEXTURE     = lighting_emissive_nits_texture;

static const float RESTIR_SKY_DISTANCE              = 1e10f;

// Section 4 replaces the fixed world-space distance gate with dual footprints.
// Reject only degenerate distances whose geometry term cannot be represented.
bool restir_connection_distance_valid(float distance_sq)
{
    return distance_sq > 0.0f && isfinite(distance_sq) && isfinite(rcp(distance_sq));
}

bool restir_dual_footprint_allowed(float distance_sq, float forward_pdf_cos, float inverse_pdf_cos, float threshold)
{
    // Equation 5: min(d^2 / a, d^2 / b) >= threshold is equivalent to
    // d^2 >= threshold * max(a, b). No PDF floor or finite stand-in for an
    // infinite footprint is needed; zero also disables the inverse endpoint test.
    if (!all(isfinite(float3(forward_pdf_cos, inverse_pdf_cos, threshold))) ||
        forward_pdf_cos < 0.0f || inverse_pdf_cos < 0.0f || threshold < 0.0f)
        return false;
    return distance_sq >= threshold * max(forward_pdf_cos, inverse_pdf_cos);
}
// Keep the entire front hemisphere: grazing glossy paths can carry substantial energy.
static const float RESTIR_RC_COS_FRONT       = 0.0f;
// footprint threshold scale, c / 100 with c = 0.02 from the paper, larger is more conservative
static const float RESTIR_RC_FOOTPRINT_C     = 0.0002f;
// Reference getDiffusePseudoPdf: a BSDF-hit emitter has a diffuse inverse
// footprint even though no outgoing scattering event is sampled there.
static const float RESTIR_RC_EMITTER_PDF     = 1.0f / (2.0f * PI);
// Supplemental section 4: reciprocal squared PDF replaces lobe roughness for
// a black-box mixture BSDF. Rough glossy mixtures are not diffuse lobes.
static const float RESTIR_RC_MIN_PDF_ROUGHNESS = 0.2f;
// A probability density has no scene-independent positive lower bound.
// Reject undefined techniques, not valid low-probability paths.
bool restir_pdf_valid(float pdf)
{
    return pdf > 0.0f && isfinite(pdf);
}

// nee
static const float MIN_AREA_LIGHT_SOLID_ANGLE = 1e-4f;

// smooth range window matching compute_attenuation_range in common_structs.hlsl, keeps restir falloff consistent with raster
float restir_range_window(float dist, float range)
{
    if (range <= 0.0f)
    {
        return 0.0f;
    }
    float ratio  = dist / range;
    float ratio2 = ratio * ratio;
    float window = saturate(1.0f - ratio2 * ratio2);
    return window * window;
}

// spot cone shared by the primary nee candidate and the reuse re-evaluation
float restir_spot_factor(LightParameters light, float3 dir_to_light)
{
    if ((light.flags & (1u << 2)) == 0u)
    {
        return 1.0f;
    }
    return light_spot_factor(light.flags, -dir_to_light, light.direction, light.direction_right, light.angle);
}

// incident radiance of a point or spot light at pos, the dirac endpoint has no area measure so
// reuse re-evaluates it at the destination with a unit jacobian instead of reconnecting a surface
bool restir_dirac_light_radiance(uint light_index, float3 pos, out float3 radiance)
{
    radiance = 0.0f;
    LightParameters light = light_parameters[light_index];
    if ((light.flags & ((1u << 1) | (1u << 2))) == 0u || light.intensity <= 0.0f)
    {
        return false;
    }

    float3 to = light.position - pos;
    float distance_sq = dot(to, to);
    // Initial NEE accepts every representable nonzero distance. Reuse must
    // retain those paths rather than imposing a separate near-light cutoff.
    if (!restir_connection_distance_valid(distance_sq))
        return true;
    float dist = sqrt(distance_sq);
    if (light.range <= 0.0f || dist >= light.range)
    {
        return true;
    }

    float attenuation = restir_range_window(dist, light.range) / (dist * dist + 0.0001f);
    attenuation      *= restir_spot_factor(light, to / dist);
    radiance          = light.color.rgb * light.intensity * attenuation;
    return true;
}

// urena, fajardo, king 2013 spherical rectangle solid angle sampling
// rect_origin is the min corner, ex / ey the edge vectors, returns the sampled point and solid angle
void sample_spherical_rectangle(
    float3 origin,
    float3 rect_origin,
    float3 ex,
    float3 ey,
    float2 xi,
    out float3 out_pos,
    out float  out_solid_angle)
{
    float3 d   = rect_origin - origin;
    float  exl = max(length(ex), 1e-6f);
    float  eyl = max(length(ey), 1e-6f);
    float3 x   = ex / exl;
    float3 y   = ey / eyl;
    float3 z   = cross(x, y);

    float x0 = dot(d, x);
    float y0 = dot(d, y);
    float z0 = dot(d, z);
    if (abs(z0) < 1e-6f)
    {
        // Coplanar receivers see zero projected area; edge normals are undefined.
        out_pos = rect_origin + xi.x * ex + xi.y * ey;
        out_solid_angle = 0.0f;
        return;
    }
    if (z0 > 0.0f)
    {
        z0 = -z0;
        z  = -z;
    }

    float x1 = x0 + exl;
    float y1 = y0 + eyl;

    float3 v00 = float3(x0, y0, z0);
    float3 v10 = float3(x1, y0, z0);
    float3 v11 = float3(x1, y1, z0);
    float3 v01 = float3(x0, y1, z0);

    float3 n0 = normalize(cross(v00, v10));
    float3 n1 = normalize(cross(v10, v11));
    float3 n2 = normalize(cross(v11, v01));
    float3 n3 = normalize(cross(v01, v00));

    float g0 = atan2(length(cross(n0, n1)), -dot(n0, n1));
    float g1 = atan2(length(cross(n1, n2)), -dot(n1, n2));
    float g2 = atan2(length(cross(n2, n3)), -dot(n2, n3));
    float g3 = atan2(length(cross(n3, n0)), -dot(n3, n0));

    float b0 = n0.z;
    float b1 = n2.z;
    float k  = 2.0f * PI - g2 - g3;

    out_solid_angle = max(g0 + g1 - k, 0.0f);

    float au   = xi.x * out_solid_angle + k;
    // au crosses pi: preserving the sign of sin(au) is essential to sample
    // both halves of the rectangle. A positive-only epsilon biases the light.
    float sin_au = sin(au);
    float denominator = (sin_au < 0.0f ? -1.0f : 1.0f) * max(abs(sin_au), 1e-7f);
    float fu   = (cos(au) * b0 - b1) / denominator;
    float sgn  = fu > 0.0f ? 1.0f : -1.0f;
    float cu   = clamp(sgn / sqrt(fu * fu + b0 * b0), -1.0f, 1.0f);

    float xu_denom = max(sqrt(max(1.0f - cu * cu, 0.0f)), 1e-7f);
    float xu       = clamp(-(cu * z0) / xu_denom, x0, x1);

    float dsq = xu * xu + z0 * z0;
    float h0  = y0 / sqrt(dsq + y0 * y0);
    float h1  = y1 / sqrt(dsq + y1 * y1);
    float hv  = h0 + xi.y * (h1 - h0);
    float hv2 = hv * hv;
    float yv  = (hv2 < 1.0f - 1e-6f) ? (hv * sqrt(dsq) / sqrt(1.0f - hv2)) : y1;

    out_pos = origin + xu * x + yv * y + z0 * z;
}

// Conditional point density in solid angle, before the triangle selection PMF.
// Cached tile points are uniform in area and independent of the receiver.
// Convert the same area density for NEE, BSDF-hit MIS and reconnection.
float restir_triangle_point_pdf(float3 origin, float3 v0, float3 v1, float3 v2, float3 endpoint)
{
    float3 delta = endpoint - origin;
    float distance_sq = dot(delta, delta);
    if (!restir_connection_distance_valid(distance_sq))
        return 0.0f;
    float projected_area = 0.5f * abs(dot(cross(v1 - v0, v2 - v0), -delta * rsqrt(distance_sq)));
    return projected_area > 0.0f ? distance_sq / projected_area : 0.0f;
}

// The pool and BLAS both use LOD 0. Reconstruct its proposal density from
// the hit triangle; no search through the emissive pool is needed.
bool restir_hit_emitter_triangle(uint2 identity, out EmissiveTriangle tri)
{
    tri = (EmissiveTriangle)0;
    uint count, stride;
    geometry_infos.GetDimensions(count, stride);
    if (identity.x >= count)
        return false;
    GeometryInfo geo = geometry_infos[identity.x];
    MaterialParameters mat = material_parameters[geo.material_index];
    if (!mat.is_pooled_emitter())
        return false;
    uint base = geo.index_offset + identity.y * 3u;
    geometry_indices.GetDimensions(count, stride);
    if (base + 2u >= count)
        return false;
    float3x3 transform = float3x3(geo.object_to_world_0.xyz, geo.object_to_world_1.xyz, geo.object_to_world_2.xyz);
    float3 translation = float3(geo.object_to_world_0.w, geo.object_to_world_1.w, geo.object_to_world_2.w);
    tri.v0 = mul(geometry_vertices[geo.vertex_offset + geometry_indices[base]].position, transform) + translation;
    tri.v1 = mul(geometry_vertices[geo.vertex_offset + geometry_indices[base + 1u]].position, transform) + translation;
    tri.v2 = mul(geometry_vertices[geo.vertex_offset + geometry_indices[base + 2u]].position, transform) + translation;
    float3 area_normal = cross(tri.v1 - tri.v0, tri.v2 - tri.v0);
    float twice_area = length(area_normal);
    if (twice_area <= 1e-12f)
        return false;
    tri.area = 0.5f * twice_area;
    tri.normal = area_normal / twice_area;
    tri.emission = saturate(mat.color.rgb) * mat.emissive_strength * photometric_to_radiometric(RESTIR_EMISSIVE_NITS_FROM_ALBEDO);
    tri.weight = tri.area * dot(tri.emission, float3(0.299f, 0.587f, 0.114f));
    return tri.weight > 0.0f;
}

float restir_emitter_source_pdf(EmissiveTriangle tri, float3 origin, float3 normal, float3 endpoint, uint vertex)
{
    uint count = (uint)buffer_frame.restir_pt_emissive_tri_count;
    if (count == 0u || tri.weight <= 0.0f)
        return 0.0f;
    float total = emissive_triangles[count - 1u].cdf;
    float3 delta = endpoint - origin;
    float distance_sq = dot(delta, delta);
    // Match NEE's support even near the emitter. Dropping only this PDF
    // gives BSDF hits full MIS weight while NEE still contributes there.
    if (total <= 0.0f || !restir_connection_distance_valid(distance_sq))
        return 0.0f;
    float distance = sqrt(distance_sq);
    float3 direction = delta / distance;
    // Light tiles sample the full sphere. Receiver-side support belongs to the
    // BSDF; a dielectric can transmit toward a light below its shading normal.
    if (dot(tri.normal, -direction) <= 0.0f)
        return 0.0f;
    float pdf = (tri.weight / total) * restir_light_kind_pmf_at_vertex(RESTIR_LIGHT_KIND_EMISSIVE, vertex) *
        restir_triangle_point_pdf(origin, tri.v0, tri.v1, tri.v2, endpoint);
    return pdf > 0.0f && isfinite(pdf) ? pdf : 0.0f;
}

// Terminal type is derived from the endpoint strategy and connection length
// when unpacking. This leaves two packed bits for the scattering events on
// either side of RC without sacrificing the five-bit NEE proposal identity.
static const uint PATH_FLAG_SKY      = 1 << 0;  // rc is the sky dome, rc_pos stores a unit direction
static const uint PATH_FLAG_HAS_RC   = 1 << 1;  // reconnection vertex is valid for the reconnection shift
static const uint PATH_FLAG_RC_EMIT  = 1 << 2;  // terminal BSDF-hit emitter
static const uint PATH_FLAG_NEE      = 1 << 3;  // candidate came from the light nee strategy
static const uint PATH_FLAG_TRANSMISSION_BEFORE_RC = 1 << 4;
static const uint PATH_FLAG_TRANSMISSION_AFTER_RC  = 1 << 5;

// endpoint_light values that are not analytic light indices, fixed so a sample keeps its technique
// when the light count changes under it, light indices stay below them (16 bit packed field)
static const uint RESTIR_ENDPOINT_EMTRI = 65533u; // emissive triangle pool
static const uint RESTIR_ENDPOINT_SKY   = 65534u; // environment nee
static const uint RESTIR_ENDPOINT_BSDF  = 65535u; // bsdf sampled hit
// Analytic terminal endpoints reserve this field for their persistent type.
static const uint RESTIR_ENDPOINT_DIRAC_TAG = 7u;
static const uint RESTIR_ENDPOINT_AREA_TAG = 6u;

// suffix of a path starting at the primary hit, lin 2022 5 split of the radiance leaving rc
//   L_at_rc = L_nee + f_rc(in_at_rc, rc_outgoing_dir) * L_post
// L_nee stores terminal emission only. Continuing paths store one selected suffix
// in L_post; the full RC BRDF is re-evaluated for the destination incoming direction.
// rc_pos is a world space vertex (HAS_RC) or a unit sky direction (SKY)
// rc_normal is the shading normal at rc, the same one the suffix trace sampled and shaded with,
// storing the geometric normal instead makes f_rc reject its own rc_outgoing_dir on normal mapped
// surfaces and silently drops L_post
// src_* captures the source pixel primary surface so reuse passes avoid sampling a reprojected g-buffer
struct PathSample
{
    float3 F; // Exact RGB integrand at the source primary, paired with W.
    float rc_nee_pdf; // Source light PDF for the endpoint immediately after RC.
    uint2 endpoint_geometry; // Terminal pooled emitter: instance/primitive or pool index.
    bool endpoint_pooled;
    bool endpoint_environment; // Sky endpoint, distinct from an emissive BSDF hit.
    bool rc_nee_reflection_disabled; // RC was inside a volume; only transmission-side NEE has support.
    float rc_pdf; // Source incoming geometry factor (times rectangle area for local-UV endpoints).
    float first_hit_distance; // Transient denoiser guide, never the postponed RC distance.
    float initial_weight; // Transient initial RIS/RR weight, folded into reservoir W.
    uint endpoint_light; // 0xffff = BSDF hit, otherwise the fixed NEE technique.
    uint endpoint_emtri_draw;
    uint3 rc_hit; // TLAS instance/primitive or pool index/zero, then UNORM16 barycentrics.
    float3 rc_pos;
    float3 rc_normal;
    uint rc_geometric_normal_packed; // Surface RC: two 12-bit octahedral components.
    float3 rc_outgoing_dir;
    float3 rc_L_post;
    float3 rc_L_nee;
    float3 rc_albedo;
    float  rc_roughness;
    float  rc_metallic;
    // Transient interface data, reconstructed from rc_hit after unpacking.
    uint   rc_material_index;
    bool   rc_front_facing;
    uint src_normal_roughness; // Exact G-buffer normal (24 bits) and roughness (8 bits).
    float3 src_pos;
    float3 src_normal;
    float3 src_albedo;
    float  src_roughness;
    float  src_metallic;
    uint   seed_path;
    uint   path_length;
    uint   rc_length;
    uint   flags;
};

// The reference retains triangle hits rather than a frozen world-space point.
// BSDF paths retain TLAS hits; pooled NEE retains pool triangle hits.
// Analytic endpoints still use their explicit endpoint representation.
bool is_triangle_reconnection(PathSample s)
{
    return (s.flags & PATH_FLAG_HAS_RC) != 0u && (s.flags & PATH_FLAG_SKY) == 0u &&
        ((s.flags & PATH_FLAG_NEE) == 0u || s.endpoint_light == RESTIR_ENDPOINT_EMTRI);
}

// Pool topology changes invalidate history on the CPU. A pool index plus
// barycentrics therefore identifies the same emitter point throughout reuse.
bool restir_pool_hit_position(uint3 hit, out float3 position)
{
    position = 0.0f;
    if (hit.x >= (uint)buffer_frame.restir_pt_emissive_tri_count) return false;
    EmissiveTriangle tri = emissive_triangles[hit.x];
    float3 bary = unpack_hit_barycentrics(hit.z);
    position = tri.v0 * bary.x + tri.v1 * bary.y + tri.v2 * bary.z;
    return tri.area > 0.0f && all(isfinite(position));
}

bool restir_rc_hit_position(uint3 hit, out float3 position)
{
    position = 0.0f;
    uint count, stride;
    geometry_infos.GetDimensions(count, stride);
    if (hit.x >= count) return false;
    GeometryInfo geo = geometry_infos[hit.x];
    uint index_count;
    geometry_indices.GetDimensions(index_count, stride);
    uint base = geo.index_offset + hit.y * 3u;
    if (base >= index_count || index_count - base < 3u) return false;
    float3 bary = unpack_hit_barycentrics(hit.z);
    float3 p0 = geometry_vertices[geo.vertex_offset + geometry_indices[base]].position;
    float3 p1 = geometry_vertices[geo.vertex_offset + geometry_indices[base + 1u]].position;
    float3 p2 = geometry_vertices[geo.vertex_offset + geometry_indices[base + 2u]].position;
    float3x3 transform = float3x3(geo.object_to_world_0.xyz, geo.object_to_world_1.xyz, geo.object_to_world_2.xyz);
    position = mul(p0 * bary.x + p1 * bary.y + p2 * bary.z, transform) +
        float3(geo.object_to_world_0.w, geo.object_to_world_1.w, geo.object_to_world_2.w);
    return all(isfinite(position));
}

bool restir_reconstruct_rc(inout PathSample s, float3 previous_position);
float3 restir_rc_bsdf_evaluate(PathSample s, float3 view, float3 direction, out float pdf);

// weight_sum is a pass local accumulator, it is never read back after a store because W and
// target_pdf carry everything the next pass needs, so it stays out of the packed layout
struct Reservoir
{
    PathSample sample;
    float      weight_sum;
    float      M;
    float      W;
    float      target_pdf;
};

float2 octahedral_encode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0f)
    {
        float2 sign_not_zero = float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
        n.xy = (1.0f - abs(n.yx)) * sign_not_zero;
    }
    return n.xy;
}

float3 octahedral_decode(float2 e)
{
    float3 n = float3(e.xy, 1.0f - abs(e.x) - abs(e.y));
    if (n.z < 0.0f)
    {
        float2 sign_not_zero = float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
        n.xy = (1.0f - abs(n.yx)) * sign_not_zero;
    }
    return normalize(n);
}

uint pack_path_info(PathSample s)
{
    // BSDF leaves do not use the NEE draw field. Retain their endpoint kind
    // there so suffix MIS distinguishes sky misses from unpooled emitters.
    uint endpoint_tag = s.endpoint_light == RESTIR_ENDPOINT_BSDF ? (uint(s.endpoint_environment) | (uint(s.rc_nee_reflection_disabled) << 1u)) : s.endpoint_emtri_draw;
    uint stored_flags = (s.flags & (PATH_FLAG_SKY | PATH_FLAG_HAS_RC)) |
        ((s.flags & (PATH_FLAG_TRANSMISSION_BEFORE_RC | PATH_FLAG_TRANSMISSION_AFTER_RC)) >> 2u);
    return (s.rc_length & 7u) | (stored_flags << 3u) | ((s.path_length & 7u) << 7u)
        | ((s.endpoint_light & 65535u) << 10u) | ((endpoint_tag & 31u) << 26u) | (uint(s.endpoint_pooled) << 31u);
}

uint pack_rc_geometric_normal(float3 normal)
{
    float2 oct = octahedral_encode(normal);
    uint2 encoded = uint2(saturate(oct * 0.5f + 0.5f) * 4095.0f + 0.5f);
    return encoded.x | (encoded.y << 12u);
}

float3 unpack_rc_geometric_normal(uint packed)
{
    return octahedral_decode(float2(packed & 4095u, (packed >> 12u) & 4095u) * (2.0f / 4095.0f) - 1.0f);
}

void unpack_path_info(uint packed, inout PathSample s)
{
    s.rc_length = packed & 7u;
    uint stored_flags = (packed >> 3u) & 15u;
    s.flags = (stored_flags & 3u) | ((stored_flags & 12u) << 2u);
    s.path_length = (packed >> 7u) & 7u;
    s.endpoint_light = (packed >> 10u) & 65535u;
    s.endpoint_emtri_draw = (packed >> 26u) & 31u;
    s.endpoint_environment = s.endpoint_light == RESTIR_ENDPOINT_SKY ||
        (s.endpoint_light == RESTIR_ENDPOINT_BSDF && (s.endpoint_emtri_draw & 1u) != 0u);
    s.rc_nee_reflection_disabled = s.endpoint_light == RESTIR_ENDPOINT_BSDF && (s.endpoint_emtri_draw & 2u) != 0u;
    if (s.endpoint_light == RESTIR_ENDPOINT_BSDF) s.endpoint_emtri_draw = 0u;
    s.endpoint_pooled = (packed >> 31u) != 0u;
    if ((s.flags & PATH_FLAG_HAS_RC) != 0u && s.rc_length == s.path_length)
        s.flags |= s.endpoint_light == RESTIR_ENDPOINT_BSDF ? PATH_FLAG_RC_EMIT : PATH_FLAG_NEE;
}

// Four RGBA32F textures (64 bytes). Primary surface history is shared per pixel;
// RC material state is reconstructed from retained geometry before a shift.
// t0: triangle hit / explicit endpoint / analytic identity + rectangle UV, outgoing direction or terminal geometric normal
// t1: suffix or terminal radiance, source incoming geometry factor
// t2: cached RGB integrand paired with W, W
// t3: initial seed, path descriptor, endpoint NEE PDF, confidence
// The reference's compressed PathReservoir::reconnectionWi uses octahedral SNORM16x2.
// Half-float coordinates waste precision near the octahedron's edges, where
// direction errors can change a narrow glossy lobe on every reuse.
uint pack_rc_outgoing_direction(float3 direction)
{
    float2 oct = octahedral_encode(direction);
    oct = float2(isnan(oct.x) ? 0.0f : clamp(oct.x, -1.0f, 1.0f),
                 isnan(oct.y) ? 0.0f : clamp(oct.y, -1.0f, 1.0f));
    int2 encoded = int2(oct * 32767.0f + float2(oct.x >= 0.0f ? 0.5f : -0.5f,
                                              oct.y >= 0.0f ? 0.5f : -0.5f));
    return (asuint(encoded.x) & 65535u) | ((asuint(encoded.y) & 65535u) << 16u);
}

float3 unpack_rc_outgoing_direction(uint packed)
{
    int2 encoded = int2(int(packed << 16u) >> 16, int(packed) >> 16);
    return octahedral_decode(max(float2(encoded) / 32767.0f, -1.0f));
}

float pack_uint8x4_to_float(float r, float g, float b, float a)
{
    uint pr = uint(saturate(r) * 255.0f + 0.5f);
    uint pg = uint(saturate(g) * 255.0f + 0.5f);
    uint pb = uint(saturate(b) * 255.0f + 0.5f);
    uint pa = uint(saturate(a) * 255.0f + 0.5f);
    return asfloat(pr | (pg << 8u) | (pb << 16u) | (pa << 24u));
}

float4 unpack_float_to_uint8x4(float p)
{
    uint packed = asuint(p);
    return float4(
        float((packed      ) & 0xFFu) / 255.0f,
        float((packed >>  8u) & 0xFFu) / 255.0f,
        float((packed >> 16u) & 0xFFu) / 255.0f,
        float((packed >> 24u) & 0xFFu) / 255.0f
    );
}

// Supplemental Algorithm 1: pHat = luminance(F). Keep black at zero;
// Match PathRISState::toScalar in NVIDIA's ReSTIR PT Enhanced reference;
// display luminance uses different coefficients and floors black to FLT_MIN.
// Section 6.3 still uses the full RGB integrand for shading weights.
float target_scalar(float3 f)
{
    return max(dot(f, float3(0.299f, 0.587f, 0.114f)), 0.0f);
}

bool is_dirac_light_sample(PathSample s);
bool is_area_light_sample(PathSample s);
bool restir_resolve_area_position(inout PathSample s);
bool restir_resolve_dirac_light(PathSample s, out uint light_index);

float4 restir_primary_surface(float2 uv)
{
    uint normal_roughness = (tex_normal[gbuffer_texel(uv)].x & 0x00ffffffu) |
        ((tex_material[gbuffer_texel(uv)] & 255u) << 24u);
    return float4(restir_primary_position(uv), asfloat(normal_roughness));
}

uint restir_primary_material(float2 uv)
{
    float3 albedo = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    return asuint(pack_uint8x4_to_float(albedo.r, albedo.g, albedo.b, get_material(uv).g));
}

void pack_reservoir(Reservoir r, out float4 tex0, out float4 tex1, out float4 tex2, out float4 tex3)
{
    bool terminal = (r.sample.flags & (PATH_FLAG_SKY | PATH_FLAG_RC_EMIT | PATH_FLAG_NEE)) != 0u;
    float3 endpoint = is_triangle_reconnection(r.sample) ? asfloat(r.sample.rc_hit) : r.sample.rc_pos;
    if (is_dirac_light_sample(r.sample)) endpoint = float3(asfloat(r.sample.endpoint_geometry), 0.0f);
    if (is_area_light_sample(r.sample)) endpoint = asfloat(uint3(r.sample.endpoint_geometry, r.sample.rc_hit.z));
    tex0 = float4(endpoint, asfloat(terminal ? r.sample.rc_geometric_normal_packed : pack_rc_outgoing_direction(r.sample.rc_outgoing_dir)));
    tex1 = float4(terminal ? r.sample.rc_L_nee : r.sample.rc_L_post, r.sample.rc_pdf);
    tex2 = float4(r.sample.F, r.W);
    // Reference PathReservoir::M is floating point. Duplication-driven caps
    // produce fractional confidence; retain it through every reuse pass.
    tex3 = float4(asfloat(r.sample.seed_path), asfloat(pack_path_info(r.sample)), r.sample.rc_nee_pdf, r.M);
}

Reservoir unpack_reservoir(float4 tex0, float4 tex1, float4 tex2, float4 tex3,
    float4 primary_surface, uint primary_material)
{
    Reservoir r = (Reservoir)0;
    r.sample.F = tex2.rgb;
    r.W = tex2.a;
    r.sample.rc_pdf = tex1.w;
    r.sample.rc_nee_pdf = tex3.z;
    r.sample.seed_path = asuint(tex3.x);
    r.sample.initial_weight = 1.0f;
    unpack_path_info(asuint(tex3.y), r.sample);
    bool terminal = (r.sample.flags & (PATH_FLAG_SKY | PATH_FLAG_RC_EMIT | PATH_FLAG_NEE)) != 0u;
    r.sample.rc_hit = asuint(tex0.xyz);
    r.sample.rc_pos = tex0.xyz;
    r.sample.rc_normal = float3(0, 1, 0);
    r.sample.rc_roughness = 1.0f;
    if (terminal)
    {
        r.sample.rc_L_nee = tex1.rgb;
        r.sample.rc_geometric_normal_packed = asuint(tex0.w);
        r.sample.rc_normal = unpack_rc_geometric_normal(r.sample.rc_geometric_normal_packed);
        r.sample.rc_outgoing_dir = r.sample.rc_normal;
    }
    else
    {
        r.sample.rc_L_post = tex1.rgb;
        r.sample.rc_outgoing_dir = unpack_rc_outgoing_direction(asuint(tex0.w));
    }
    if (is_triangle_reconnection(r.sample))
    {
        bool pool_hit = r.sample.endpoint_light == RESTIR_ENDPOINT_EMTRI && (r.sample.flags & PATH_FLAG_NEE) != 0u;
        if (pool_hit) restir_pool_hit_position(r.sample.rc_hit, r.sample.rc_pos);
        else restir_rc_hit_position(r.sample.rc_hit, r.sample.rc_pos);
        if (terminal && r.sample.endpoint_pooled)
            r.sample.endpoint_geometry = pool_hit ? uint2(r.sample.rc_hit.x, 0u) : r.sample.rc_hit.xy;
    }
    else if (is_area_light_sample(r.sample))
    {
        r.sample.endpoint_geometry = asuint(tex0.xy);
        if (!restir_resolve_area_position(r.sample)) r.sample.rc_pos = 0.0f;
    }
    else if (is_dirac_light_sample(r.sample))
    {
        r.sample.endpoint_geometry = asuint(tex0.xy);
        uint light_index;
        r.sample.rc_pos = restir_resolve_dirac_light(r.sample, light_index) ? light_parameters[light_index].position : 0.0f;
    }
    r.sample.src_pos = primary_surface.xyz;
    r.sample.src_normal_roughness = asuint(primary_surface.w);
    r.sample.src_normal = gbuffer_unpack_normal(r.sample.src_normal_roughness);
    r.sample.src_roughness = max(float(r.sample.src_normal_roughness >> 24u) / 255.0f, 0.04f);
    float4 material = unpack_float_to_uint8x4(asfloat(primary_material));
    r.sample.src_albedo = material.rgb;
    r.sample.src_metallic = material.a;
    r.M = tex3.w;
    r.target_pdf = target_scalar(r.sample.F);
    return r;
}

bool is_sky_sample(PathSample s)     { return (s.flags & PATH_FLAG_SKY)     != 0; }
bool has_reconnection(PathSample s)  { return (s.flags & PATH_FLAG_HAS_RC)  != 0; }
bool is_rc_emissive(PathSample s)    { return (s.flags & PATH_FLAG_RC_EMIT) != 0; }
bool is_nee_sample(PathSample s)     { return (s.flags & PATH_FLAG_NEE)     != 0; }
bool is_dirac_light_sample(PathSample s)
{
    return is_nee_sample(s) && !is_sky_sample(s) && s.rc_length == s.path_length && s.endpoint_light < RESTIR_ENDPOINT_EMTRI &&
        s.endpoint_emtri_draw == RESTIR_ENDPOINT_DIRAC_TAG;
}

bool is_area_light_sample(PathSample s)
{
    return is_nee_sample(s) && !is_sky_sample(s) && s.rc_length == s.path_length && s.endpoint_light < RESTIR_ENDPOINT_EMTRI &&
        s.endpoint_emtri_draw == RESTIR_ENDPOINT_AREA_TAG;
}

// Buffer indices can change as lights are culled, added or removed. Resolve
// the persistent light identity; position is not an identity, even nearby.
bool restir_resolve_analytic_light(PathSample s, uint type_flags, out uint light_index)
{
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    light_index = s.endpoint_light;
    if (!any(s.endpoint_geometry != 0u))
        return false;
    if (light_index < light_count)
    {
        LightParameters light = light_parameters[light_index];
        if ((light.flags & type_flags) != 0u &&
            all(uint2(light.identity_low, light.identity_high) == s.endpoint_geometry))
            return true;
    }
    for (uint i = 0u; i < light_count; ++i)
    {
        LightParameters light = light_parameters[i];
        if ((light.flags & type_flags) != 0u &&
            all(uint2(light.identity_low, light.identity_high) == s.endpoint_geometry))
        {
            light_index = i;
            return true;
        }
    }
    return false;
}

bool restir_resolve_dirac_light(PathSample s, out uint light_index)
{
    return restir_resolve_analytic_light(s, (1u << 1) | (1u << 2), light_index);
}

// Rectangle samples retain the original UNORM16 tile coordinates, not a
// frozen world-space point. The stable identity survives light-buffer changes.
bool restir_resolve_area_position(inout PathSample s)
{
    uint index;
    if (!restir_resolve_analytic_light(s, 1u << 6, index)) return false;
    LightParameters light = light_parameters[index];
    if (light.area_width <= 0.0f || light.area_height <= 0.0f) return false;
    float2 position = float2(s.rc_hit.z & 65535u, s.rc_hit.z >> 16u) / 65536.0f - 0.5f;
    float3 right = normalize(light.direction_right);
    float3 up = normalize(cross(light.direction, right));
    s.endpoint_light = index;
    s.rc_pos = light.position + position.x * light.area_width * right + position.y * light.area_height * up;
    return all(isfinite(s.rc_pos));
}

bool restir_reconstruct_area_endpoint(inout PathSample s, float3 origin)
{
    if (!restir_resolve_area_position(s)) return false;
    LightParameters light = light_parameters[s.endpoint_light];
    s.rc_normal = light.direction;
    s.rc_geometric_normal_packed = pack_rc_geometric_normal(light.direction);
    s.rc_L_nee = light.color.rgb * light.intensity * restir_range_window(length(s.rc_pos - origin), light.range);
    return all(isfinite(s.rc_L_nee));
}

float restir_endpoint_area_measure(PathSample s)
{
    if (!is_area_light_sample(s)) return 1.0f;
    LightParameters light = light_parameters[s.endpoint_light];
    return light.area_width * light.area_height;
}

bool is_reservoir_valid(Reservoir r)
{
    if (any(isnan(r.sample.F)) || any(isinf(r.sample.F))) return false;
    if (any(isnan(r.sample.rc_pos))    || any(isinf(r.sample.rc_pos)))    return false;
    if (any(isnan(r.sample.rc_L_post)) || any(isinf(r.sample.rc_L_post))) return false;
    if (any(isnan(r.sample.rc_L_nee))  || any(isinf(r.sample.rc_L_nee)))  return false;
    if (any(isnan(r.sample.rc_normal)) || any(isinf(r.sample.rc_normal))) return false;
    if (isnan(r.W) || isinf(r.W) || r.W < 0)                              return false;
    if (!isfinite(r.M) || r.M < 0)                                       return false;
    // target_pdf is the own domain density in the pairwise mis shares, reject corrupt values
    if (isnan(r.target_pdf) || isinf(r.target_pdf) || r.target_pdf < 0)   return false;
    if (r.sample.rc_length > RESTIR_MAX_PATH_LENGTH)                      return false;
    return true;
}

Reservoir create_empty_reservoir()
{
    Reservoir r;
    r.sample.rc_hit          = 0u;
    r.sample.rc_pos          = float3(0, 0, 0);
    r.sample.rc_normal       = float3(0, 1, 0);
    r.sample.rc_geometric_normal_packed = 0u;
    r.sample.rc_outgoing_dir = float3(0, 1, 0);
    r.sample.rc_L_post       = float3(0, 0, 0);
    r.sample.rc_L_nee        = float3(0, 0, 0);
    r.sample.rc_albedo       = float3(0, 0, 0);
    r.sample.rc_roughness    = 1.0f;
    r.sample.rc_metallic     = 0.0f;
    r.sample.rc_material_index = 0u;
    r.sample.rc_front_facing = false;
    r.sample.src_normal_roughness = gbuffer_pack_normal(float3(0, 1, 0), 0u) | 0xff000000u;
    r.sample.src_pos         = float3(0, 0, 0);
    r.sample.src_normal      = float3(0, 1, 0);
    r.sample.src_albedo      = float3(0, 0, 0);
    r.sample.src_roughness   = 1.0f;
    r.sample.src_metallic    = 0.0f;
    r.sample.seed_path       = 0;
    r.sample.F               = 0.0f;
    r.sample.rc_pdf          = 0.0f;
    r.sample.rc_nee_pdf       = 0.0f;
    r.sample.endpoint_geometry = 0u;
    r.sample.endpoint_pooled = false;
    r.sample.endpoint_environment = false;
    r.sample.rc_nee_reflection_disabled = false;
    r.sample.initial_weight  = 1.0f;
    r.sample.first_hit_distance = 0.0f;
    r.sample.endpoint_light  = 65535u;
    r.sample.endpoint_emtri_draw = 0u;
    r.sample.path_length     = 0;
    r.sample.rc_length       = 0;
    r.sample.flags           = 0;
    r.weight_sum             = 0;
    r.M                      = 0;
    r.W                      = 0;
    r.target_pdf             = 0;
    return r;
}

// Reference PathReservoir::merge rejects a non-finite contribution before it
// enters the accumulated weight. Finite inputs can still overflow in products.
float restir_sanitize_resampling_weight(float weight)
{
    return isfinite(weight) ? max(weight, 0.0f) : 0.0f;
}

// streaming ris sample insert, lin 2022 algorithm 1
// M here is a pass local candidate count, the initial pass overwrites it with a confidence of one
// before the store so the mis denominators never see how many candidates happened to score
bool update_reservoir(inout Reservoir reservoir, PathSample new_sample, float weight, float random_value)
{
    if (weight <= 0.0f || isnan(weight) || isinf(weight))
    {
        return false;
    }

    reservoir.weight_sum += weight;
    reservoir.M          += 1.0f;

    if (random_value * reservoir.weight_sum < weight)
    {
        reservoir.sample = new_sample;
        return true;
    }
    return false;
}

// caps M without touching weight_sum or W, only affects future stream combine ratios
void clamp_reservoir_M(inout Reservoir reservoir, float max_M)
{
    if (reservoir.M > max_M)
        reservoir.M = max_M;
}

// rng
uint pcg_hash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint xxhash32(uint seed)
{
    const uint PRIME1 = 2654435761u;
    const uint PRIME2 = 2246822519u;
    const uint PRIME3 = 3266489917u;

    uint h = seed + PRIME3;
    h = (h ^ (h >> 15)) * PRIME2;
    h = (h ^ (h >> 13)) * PRIME3;
    return h ^ (h >> 16);
}

float random_float(inout uint seed)
{
    seed = pcg_hash(seed);
    // Keep the upper 24 bits so FP32 conversion cannot round up to 1.
    // Reservoir selection and inverse-CDF sampling require [0, 1).
    return float(seed >> 8u) * (1.0f / 16777216.0f);
}

float2 random_float2(inout uint seed) { return float2(random_float(seed), random_float(seed)); }
float3 random_float3(inout uint seed) { return float3(random_float(seed), random_float(seed), random_float(seed)); }

uint create_seed_for_pass(uint2 pixel, uint frame, uint pass_id)
{
    const uint GOLDEN_RATIO = 0x9E3779B9u;
    const uint PASS_PRIMES[4] = { 0x85EBCA77u, 0xC2B2AE3Du, 0x27D4EB2Fu, 0x165667B1u };

    uint pass_salt = (pass_id < 4) ? PASS_PRIMES[pass_id] : xxhash32(pass_id * GOLDEN_RATIO);

    uint h = xxhash32(pixel.x ^ pass_salt);
    h = pcg_hash(h ^ xxhash32(pixel.y));
    h = pcg_hash(h ^ xxhash32(frame ^ (pass_salt >> 16)));
    return h;
}

// paired partner lookup, lin 2026 3.2, the table is self inverting so when p resolves q then
// q resolves p, both spatial passes call this with identical inputs and agree on every pair
// a per frame random flip, transpose and offset decorrelates consecutive frames, deltas
// transform through the inverse of the linear part so pairs stay mutual under any transform
int2 restir_pairing_partner(uint2 pixel, uint table)
{
    uint size      = RESTIR_PAIRING_SIZES[table];
    uint h         = xxhash32(buffer_frame.frame * 3u + table + 1u);
    uint2 c        = (pixel + uint2(h & 0xFFu, (h >> 8) & 0xFFu)) % size;
    bool flip_x    = (h & 0x10000u) != 0u;
    bool flip_y    = (h & 0x20000u) != 0u;
    bool transpose = (h & 0x40000u) != 0u;
    if (transpose)
    {
        c = c.yx;
    }
    if (flip_x)
    {
        c.x = size - 1u - c.x;
    }
    if (flip_y)
    {
        c.y = size - 1u - c.y;
    }
    uint packed = restir_pairing[RESTIR_PAIRING_BASES[table] + c.y * size + c.x];
    int2 d      = int2(int((packed >> 8) & 0xFFu) - 128, int(packed & 0xFFu) - 128);
    if (flip_x)
    {
        d.x = -d.x;
    }
    if (flip_y)
    {
        d.y = -d.y;
    }
    if (transpose)
    {
        d = d.yx;
    }
    return int2(pixel) + d;
}

// g-buffer similarity gate used before attempting a shift on a paired neighbor
bool is_neighbor_gbuffer_compatible(
    uint2 center_pixel,
    int2 neighbor_pixel,
    float2 resolution)
{
    float2 resolution_active = floor(resolution * get_render_uv_scale());
    if (neighbor_pixel.x < 0 || neighbor_pixel.x >= (int)resolution_active.x ||
        neighbor_pixel.y < 0 || neighbor_pixel.y >= (int)resolution_active.y)
        return false;

    if (any(center_pixel >= uint2(resolution_active)))
        return false;

    // Both lanes must evaluate the same ordered pair. Algebraically symmetric
    // expressions with caller-supplied guides can round differently on the GPU
    // and disagree at the threshold, breaking paired reuse's reciprocal gate.
    int2 first = int2(center_pixel);
    int2 second = neighbor_pixel;
    if (second.y < first.y || (second.y == first.y && second.x < first.x))
    {
        int2 swap_pixel = first;
        first = second;
        second = swap_pixel;
    }
    float2 center_uv = restir_primary_uv(uint2(first), uint2(resolution));
    float center_depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), center_uv, 0).r;
    if (center_depth <= 0.0f)
        return false;
    float3 center_pos = restir_primary_position(center_uv);
    float3 center_normal = get_normal(center_uv);
    float2 neighbor_uv   = restir_primary_uv(uint2(second), uint2(resolution));
    float neighbor_depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), neighbor_uv, 0).r;

    if (neighbor_depth <= 0.0f)
        return false;

    float3 neighbor_normal = get_normal(neighbor_uv);
    float normal_similarity = dot(center_normal, neighbor_normal);
    if (normal_similarity < RESTIR_NORMAL_THRESHOLD)
        return false;

    // Match the reference's symmetric camera-distance gate. Plane separation
    // admits distant points on grazing floors while rejecting different pairs
    // on curved surfaces, changing the paper's spatial reuse neighborhood.
    float3 neighbor_pos = restir_primary_position(neighbor_uv);
    float center_distance = length(center_pos - get_camera_position());
    float neighbor_distance = length(neighbor_pos - get_camera_position());
    return abs(center_distance - neighbor_distance) <
        RESTIR_DEPTH_THRESHOLD * max(center_distance, neighbor_distance);
}

// sampling helpers
float3 sample_cosine_hemisphere(float2 xi, out float pdf)
{
    float phi       = 2.0f * PI * xi.x;
    float cos_theta = sqrt(xi.y);
    float sin_theta = sqrt(1.0f - xi.y);

    pdf = cos_theta / PI;
    return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

void build_orthonormal_basis_fast(float3 n, out float3 t, out float3 b)
{
    // Duff et al., Building an Orthonormal Basis, Revisited (2017), also
    // used by Falcor's branchlessONB. The denominator stays away from zero;
    // 1 / (1 + n.z) loses orthogonality near the negative Z pole in FP32.
    float sign_z = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (sign_z + n.z);
    float d = n.x * n.y * a;
    t = float3(1.0f + sign_z * n.x * n.x * a, sign_z * d, -sign_z * n.x);
    b = float3(d, sign_z + n.y * n.y * a, -n.y);
}

float3 local_to_world(float3 local_dir, float3 n)
{
    float3 t, b;
    build_orthonormal_basis_fast(n, t, b);
    return normalize(t * local_dir.x + b * local_dir.y + n * local_dir.z);
}

float3 world_to_local(float3 world_dir, float3 n)
{
    float3 t, b;
    build_orthonormal_basis_fast(n, t, b);
    return float3(dot(world_dir, t), dot(world_dir, b), dot(world_dir, n));
}

// visible ggx normal sampling, heitz 2018, ve is the view direction in local tangent space
// alpha must match the brdf model via restir_d_ggx_alpha or the pdf and integrand densities diverge
float3 sample_ggx_vndf(float3 ve, float2 xi, float alpha)
{
    // Keep the sampled distribution consistent with the alpha used by the PDF.
    // Delta callers handle alpha == 0 before reaching this continuous sampler.
    float a = alpha;

    float3 vh = normalize(float3(a * ve.x, a * ve.y, ve.z));

    float  lensq = vh.x * vh.x + vh.y * vh.y;
    float3 t1    = (lensq > 0.0f) ? (float3(-vh.y, vh.x, 0.0f) * rsqrt(lensq)) : float3(1.0f, 0.0f, 0.0f);
    float3 t2    = cross(vh, t1);

    float r        = sqrt(xi.x);
    float phi      = 2.0f * PI * xi.y;
    float t1_coeff = r * cos(phi);
    float t2_coeff = r * sin(phi);
    float s        = 0.5f * (1.0f + vh.z);
    t2_coeff       = (1.0f - s) * sqrt(1.0f - t1_coeff * t1_coeff) + s * t2_coeff;

    float3 nh = t1_coeff * t1 + t2_coeff * t2 + sqrt(max(0.0f, 1.0f - t1_coeff * t1_coeff - t2_coeff * t2_coeff)) * vh;

    return normalize(float3(a * nh.x, a * nh.y, max(0.0f, nh.z)));
}

// Supplemental Sections 1 and 5: balance MIS using each strategy's
// sample-count-weighted density. The cosine environment proposal has M = 1.
float restir_balance_heuristic(float pdf_a, float pdf_b)
{
    // Rescaling preserves the partition of unity even for very small PDFs,
    // and avoids overflow when a narrow lobe has a very large density.
    float scale = max(pdf_a, pdf_b);
    if (scale <= 0.0f)
        return 0.0f;
    float a = pdf_a / scale;
    float b = pdf_b / scale;
    return a / (a + b);
}

// ray offset for self intersection avoidance, scales with position magnitude and camera distance
// since float precision degrades with magnitude, wachter and binder simplified form
// the magnitude term covers rounding in the world position itself and the distance term covers
// depth reconstruction error, both grow without bound so a fixed ceiling here cancels the whole
// point of the scaling and lets distant surfaces self hit, which reads as black gi far away
float compute_ray_offset(float3 pos_ws)
{
    float p_mag = max(abs(pos_ws.x), max(abs(pos_ws.y), abs(pos_ws.z)));
    float dist  = length(pos_ws - get_camera_position());
    return max(max(p_mag * 1e-5f, dist * 1e-4f), 2e-4f);
}

// Falcor GeometryHelpers::computeRayOrigin: component-wise floating-point
// offset. The same origin must define tracing, connection PDFs and shifts.
float restir_offset_ray_component(float position, float normal)
{
    int offset = int(normal * (3.0f * 256.0f));
    float displaced = asfloat(asint(position) + (position < 0.0f ? -offset : offset));
    return abs(position) < (1.0f / 16.0f) ? position + normal * (3.0f / 65536.0f) : displaced;
}
float3 restir_ray_origin(float3 position, float3 geometric_normal, bool transmission = false)
{
    geometric_normal = transmission ? -geometric_normal : geometric_normal;
    return float3(restir_offset_ray_component(position.x, geometric_normal.x),
        restir_offset_ray_component(position.y, geometric_normal.y),
        restir_offset_ray_component(position.z, geometric_normal.z));
}

float3 restir_reconnection_ray_origin(PathSample sample, float3 position, float3 geometric_normal)
{
    // Reference Shift::shiftPathReconnection offsets using the retained event,
    // not a newly inferred direction. Reuse must keep the same interface side.
    bool transmission = (sample.flags & PATH_FLAG_TRANSMISSION_BEFORE_RC) != 0u;
    return restir_ray_origin(position, geometric_normal, transmission);
}

// diffuse vs specular selection probability for the importance sampled brdf, from lobe energy
float compute_spec_probability(float3 albedo, float roughness, float metallic, float n_dot_v)
{
    float albedo_lum = max(luminance(albedo), 1e-3f);
    float f0_scalar  = lerp(0.04f, albedo_lum, metallic);
    float fresnel    = f0_scalar + (1.0f - f0_scalar) * pow(1.0f - n_dot_v, 5.0f);
    // smooth surfaces favor specular sampling, rough surfaces favor diffuse
    float gloss_bias = lerp(0.85f, 1.15f, 1.0f - roughness);
    float spec_w     = fresnel * gloss_bias;
    float diff_w     = albedo_lum * (1.0f - metallic) * (1.0f - fresnel);
    float total      = spec_w + diff_w;
    float spec_prob  = total > 0.0f ? spec_w / total : 0.5f;
    return clamp(spec_prob, 0.05f, 0.95f);
}

// Shared path-tracing BRDF helpers for initial sampling, replay and reconnection.
float restir_pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float restir_d_ggx_alpha(float roughness)
{
    float gloss = 1.0f - roughness;
    return sqrt(2.0f / (1.0f + pow(2.0f, 18.0f * gloss)));
}

float3 restir_compute_f90(float3 f0)
{
    return saturate(50.0f * dot(f0, 1.0f / 3.0f));
}

float3 restir_f_schlick(float3 f0, float3 f90, float v_dot_h)
{
    return f0 + (f90 - f0) * restir_pow5(1.0f - v_dot_h);
}

float restir_f_schlick_scalar(float f0, float f90, float v_dot_h)
{
    return f0 + (f90 - f0) * restir_pow5(1.0f - v_dot_h);
}

float restir_v_smithggx(float n_dot_v, float n_dot_l, float a2)
{
    float ggxv = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0f - a2) + a2);
    float ggxl = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0f - a2) + a2);
    return 0.5f / max(ggxv + ggxl, 1e-6f);
}

float restir_d_ggx(float n_dot_h, float a2)
{
    // Preserve the normalized GGX distribution sampled by the VNDF sampler.
    // The expanded form avoids cancellation at N.H ~= 1 for smooth surfaces.
    float cos2 = saturate(n_dot_h * n_dot_h);
    float d = (1.0f - cos2) + a2 * cos2;
    return a2 / (PI * d * d);
}

// G1(V) / (4 * N.V), with N.V cancelled analytically. This remains finite
// at grazing angles without changing the visible-normal sampling density.
float restir_vndf_reflection_pdf(float d, float n_dot_v, float a2)
{
    return d / (2.0f * (n_dot_v + sqrt(a2 + (1.0f - a2) * n_dot_v * n_dot_v)));
}

// Reference StandardBSDF uses Frostbite diffuse with roughness-dependent grazing
// bias and energy normalization.
float3 restir_diffuse_frostbite(float3 diffuse_color, float roughness, float n_dot_v, float n_dot_l, float v_dot_h)
{
    float f90           = roughness * (0.5f + 2.0f * v_dot_h * v_dot_h);
    float energy_factor = lerp(1.0f, 1.0f / 1.51f, roughness);
    float light_scatter = restir_f_schlick_scalar(1.0f, f90, n_dot_l);
    float view_scatter  = restir_f_schlick_scalar(1.0f, f90, n_dot_v);
    return diffuse_color * (light_scatter * view_scatter * energy_factor * (1.0f / PI));
}

float3 restir_compute_multiscatter_energy(float3 f0, float n_dot_v, float roughness)
{
    float  dhr        = lerp(1.0f - roughness * 0.7f, 1.0f, restir_pow5(1.0f - n_dot_v));
    float3 energy_loss = (1.0f - dhr) * (1.0f - f0);
    return 1.0f + f0 * energy_loss / max(1.0f - energy_loss, 1e-6f);
}

// GGX + Frostbite diffuse BRDF returning f_r * cos and the matching mixture PDF.
// specular_blend [0,1] fades the specular lobe, 0 is pure diffuse, 1 is the full mixture
float3 evaluate_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float3 l, out float pdf, float specular_blend)
{
    float3 h_unnorm = v + l;
    float h_len_sq  = dot(h_unnorm, h_unnorm);

    if (h_len_sq < 1e-6f)
    {
        pdf = 0.0f;
        return float3(0, 0, 0);
    }

    float3 h      = h_unnorm * rsqrt(h_len_sq);
    float n_dot_l = max(dot(n, l), 0.0f);
    float n_dot_v = dot(n, v);
    float n_dot_h = max(dot(n, h), 0.0f);
    float v_dot_h = max(dot(v, h), 0.0f);

    if (n_dot_l <= 0.0f)
    {
        pdf = 0.0f;
        return float3(0, 0, 0);
    }

    // Reject only the back hemisphere. A grazing-angle cutoff also removes
    // diffuse light, drawing a dark ring at about 20 times the camera height.
    if (n_dot_v <= 0.0f)
    {
        pdf = 0.0f;
        return float3(0, 0, 0);
    }

    float3 f0     = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 f90    = restir_compute_f90(f0);
    float3 f_term = restir_f_schlick(f0, f90, v_dot_h);

    float3 diffuse_color = albedo * (1.0f - metallic);
    float3 diffuse       = restir_diffuse_frostbite(diffuse_color, roughness, n_dot_v, n_dot_l, v_dot_h);
    float3 diffuse_cos   = diffuse * (1.0f - f_term) * n_dot_l;

    if (specular_blend <= 0.0f)
    {
        // diffuse only, pdf is the cosine hemisphere pdf to match sample_brdf
        pdf = n_dot_l / PI;
        return diffuse_cos;
    }

    // ggx specular, the same alpha feeds the d term, the pdf and the vndf sampler so densities match
    float  a       = restir_d_ggx_alpha(roughness);
    float  a2      = a * a;
    float  d_term  = restir_d_ggx(n_dot_h, a2);
    float  v_term  = restir_v_smithggx(n_dot_v, n_dot_l, a2);
    float3 fr      = d_term * v_term * f_term;
    fr            *= restir_compute_multiscatter_energy(f0, n_dot_v, roughness);
    float3 specular_cos = fr * n_dot_l;

    // vndf pdf, heitz 2018 eq 17, the effective spec pick fades with specular_blend
    float diffuse_pdf = n_dot_l / PI;
    float spec_pdf    = restir_vndf_reflection_pdf(d_term, n_dot_v, a2);
    float spec_prob_full = compute_spec_probability(albedo, roughness, metallic, n_dot_v);
    float spec_prob_eff  = saturate(spec_prob_full * specular_blend);
    pdf = (1.0f - spec_prob_eff) * diffuse_pdf + spec_prob_eff * spec_pdf;

    return diffuse_cos + specular_cos * specular_blend;
}

// samples a direction from the diffuse + blend*specular mixture, returns direction and mixture pdf
// specular_blend matches evaluate_brdf so the two stay numerically consistent
float3 sample_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float2 xi, out float pdf, float specular_blend)
{
    float n_dot_v = dot(n, v);
    if (n_dot_v <= 0.0f)
    {
        pdf = 0.0f;
        return n;
    }
    if (specular_blend <= 0.0f)
    {
        float  pdf_diffuse;
        float3 local_dir = sample_cosine_hemisphere(xi, pdf_diffuse);
        pdf = pdf_diffuse;
        return local_to_world(local_dir, n);
    }

    float spec_prob_full = compute_spec_probability(albedo, roughness, metallic, n_dot_v);
    float spec_prob      = saturate(spec_prob_full * specular_blend);
    float prob_diffuse   = 1.0f - spec_prob;

    float3 l;
    if (xi.x < prob_diffuse)
    {
        xi.x = xi.x / max(prob_diffuse, 1e-6f);
        float pdf_diffuse;
        float3 local_dir = sample_cosine_hemisphere(xi, pdf_diffuse);
        l = local_to_world(local_dir, n);
    }
    else
    {
        xi.x = (xi.x - prob_diffuse) / max(1.0f - prob_diffuse, 1e-6f);
        float3 v_local = world_to_local(v, n);
        v_local.z      = max(v_local.z, 0.0f);
        float3 h_local = sample_ggx_vndf(v_local, xi, restir_d_ggx_alpha(roughness));
        float3 h_world = local_to_world(h_local, n);
        l = reflect(-v, h_world);
    }

    float n_dot_l     = max(dot(n, l), 0.0f);
    if (n_dot_l <= 0.0f)
    {
        pdf = 0.0f;
        return l;
    }
    float diffuse_pdf = n_dot_l / PI;

    float3 h_unnorm = v + l;
    float h_len_sq  = dot(h_unnorm, h_unnorm);

    if (h_len_sq < 1e-6f)
    {
        pdf = diffuse_pdf * prob_diffuse;
        return l;
    }

    float3 h       = h_unnorm * rsqrt(h_len_sq);
    float n_dot_h  = max(dot(n, h), 0.0f);
    // vndf pdf for the reflection direction, heitz 2018 eq 17
    float a           = restir_d_ggx_alpha(roughness);
    float a2          = a * a;
    float d           = restir_d_ggx(n_dot_h, a2);
    float spec_pdf    = restir_vndf_reflection_pdf(d, n_dot_v, a2);

    pdf = prob_diffuse * diffuse_pdf + spec_prob * spec_pdf;
    return l;
}

// The reference material rejects reflection across the geometric surface.
// A tilted shading normal alone cannot turn reflection into transmission.
bool restir_reflection_hemisphere(float3 geometric_normal, float3 view, float3 direction)
{
    return (dot(geometric_normal, view) >= 0.0f) == (dot(geometric_normal, direction) >= 0.0f);
}

float3 evaluate_reflection_brdf(float3 albedo, float roughness, float metallic,
    float3 normal, float3 geometric_normal, float3 view, float3 direction, out float pdf, float specular_blend)
{
    if (!restir_reflection_hemisphere(geometric_normal, view, direction))
    {
        pdf = 0.0f;
        return 0.0f;
    }
    return evaluate_brdf(albedo, roughness, metallic, normal, view, direction, pdf, specular_blend);
}

float3 sample_reflection_brdf(float3 albedo, float roughness, float metallic,
    float3 normal, float3 geometric_normal, float3 view, float2 xi, out float pdf, float specular_blend)
{
    float3 direction = sample_brdf(albedo, roughness, metallic, normal, view, xi, pdf, specular_blend);
    // Invalid directions remain zero draws; resampling would change the PDF.
    if (!restir_reflection_hemisphere(geometric_normal, view, direction)) pdf = 0.0f;
    return direction;
}

float3 eval_surface_reflection_brdf_cos(float3 albedo, float roughness, float metallic,
    float3 normal, float3 geometric_normal, float3 view, float3 direction, float specular_blend)
{
    float pdf;
    return evaluate_reflection_brdf(albedo, roughness, metallic, normal, geometric_normal, view, direction, pdf, specular_blend);
}

// Full BRDF*cos helper retained for the standalone path tracer.
float3 eval_surface_brdf_cos(float3 albedo, float roughness, float metallic, float3 normal, float3 view_dir, float3 dir, float specular_blend)
{
    float n_dot_l = dot(normal, dir);
    if (n_dot_l <= 0.0f)
        return float3(0, 0, 0);

    float unused_pdf;
    return evaluate_brdf(albedo, roughness, metallic, normal, view_dir, dir, unused_pdf, specular_blend);
}

// A single reservoir owns diffuse and glossy primary transport.
float restir_primary_specular_blend(float roughness)
{
    return 1.0f;
}

// primary ray footprint squared radius, lin 2026 eq 5 rhs without the constant scale,
// r_pri^2 = |x0 - x1|^2 / (cos / 4pi) following muller 2021, the paper's c = 0.02 is tuned
// against this normalization, scaling by the pixel cone instead left the threshold four orders
// of magnitude below any reconnection footprint and the gate never fired
float restir_primary_footprint_sq(float3 primary_pos, float3 primary_normal, float3 camera_position)
{
    float3 to_cam  = camera_position - primary_pos;
    float  dist_sq = dot(to_cam, to_cam);
    // Equation 5 uses the actual projected cosine. Flooring it shrinks the
    // primary footprint at grazing angles and admits overly early reconnection.
    // A degenerate or back-facing primary has no usable finite footprint;
    // the dual-footprint test rejects this threshold and keeps random replay.
    if (!restir_connection_distance_valid(dist_sq))
        return asfloat(0x7f800000u);
    float cos_cam = dot(primary_normal, to_cam * rsqrt(dist_sq));
    if (!(cos_cam > 0.0f) || !isfinite(cos_cam))
        return asfloat(0x7f800000u);

    return dist_sq / (cos_cam / (4.0f * PI));
}

float restir_primary_footprint_sq(float3 primary_pos, float3 primary_normal)
{
    return restir_primary_footprint_sq(primary_pos, primary_normal, get_camera_position());
}

struct ShiftResult
{
    float3 f_dst;
    float  jacobian;
    bool   ok;
    PathSample sample;
};

ShiftResult try_random_replay_shift(PathSample src, float3 src_pos, float3 dst_pos,
    float3 dst_normal, float3 dst_view, float3 dst_albedo, float roughness, float metallic, float3 src_view,
    float3 camera_position, float3 dst_geometric_normal);

ShiftResult restir_shift_prefix(PathSample src, float3 pos, float3 normal, float3 view,
    float3 albedo, float roughness, float metallic, float3 camera_position, float3 primary_geometric_normal);


// Supplemental Section 5: the RIS candidate count scales the full source
// density, including the light kind's probability in the shared tile.
float restir_pooled_endpoint_mis(uint path_length, uint endpoint_light, float nee_pdf, float bsdf_pdf)
{
    uint vertex = path_length - 1u;
    float nee_count = float(max(32u / (vertex * vertex), 1u));
    float nee_density = nee_count * nee_pdf;
    return endpoint_light == RESTIR_ENDPOINT_EMTRI ? restir_balance_heuristic(nee_density, bsdf_pdf)
                                                  : restir_balance_heuristic(bsdf_pdf, nee_density);
}

float restir_terminal_emitter_pdf(PathSample src, float3 origin, float3 normal)
{
    EmissiveTriangle tri;
    if (src.endpoint_light == RESTIR_ENDPOINT_EMTRI)
    {
        if (src.endpoint_geometry.x >= (uint)buffer_frame.restir_pt_emissive_tri_count)
            return 0.0f;
        tri = emissive_triangles[src.endpoint_geometry.x];
    }
    else if (!restir_hit_emitter_triangle(src.endpoint_geometry, tri))
        return 0.0f;
    return restir_emitter_source_pdf(tri, origin, normal, src.rc_pos, src.path_length - 1u);
}

float restir_environment_endpoint_mis(uint path_length, uint endpoint_light,
    float3 normal, float3 direction, float bsdf_pdf)
{
    uint vertex = path_length - 1u;
    float nee_density = float(max(32u / (vertex * vertex), 1u)) * restir_environment_source_pdf(direction, normal, vertex);
    return endpoint_light == RESTIR_ENDPOINT_SKY ? restir_balance_heuristic(nee_density, bsdf_pdf)
                                                 : restir_balance_heuristic(bsdf_pdf, nee_density);
}

float restir_primary_endpoint_mis(PathSample src, float3 origin, float3 normal, float3 geometric_normal, float3 view,
    float3 albedo, float roughness, float metallic, float3 direction)
{
    if (src.rc_length != src.path_length || (!src.endpoint_pooled && !src.endpoint_environment)) return 1.0f;
    float bsdf_pdf;
    evaluate_reflection_brdf(albedo, roughness, metallic, normal, geometric_normal, view, direction, bsdf_pdf, restir_primary_specular_blend(roughness));
    return src.endpoint_pooled ? restir_pooled_endpoint_mis(src.path_length, src.endpoint_light,
        restir_terminal_emitter_pdf(src, origin, normal), bsdf_pdf) :
        restir_environment_endpoint_mis(src.path_length, src.endpoint_light, normal, direction, bsdf_pdf);
}

// Only the endpoint directly after RC has view-dependent MIS. The proposal
// count depends on its actual path depth, not its distance from RC.
float restir_rc_endpoint_mis(uint rc_endpoint_length, uint path_length, uint endpoint_light,
    bool endpoint_environment, float3 normal, float3 direction, float brdf_pdf)
{
    if (rc_endpoint_length != 3u || !endpoint_environment) return 1.0f;
    return restir_environment_endpoint_mis(path_length, endpoint_light, normal, direction, brdf_pdf);
}

// radiance leaving rc toward dst primary, L_at_rc = L_nee + f_rc(dst_view, rc_outgoing_dir) * L_post
// dir_primary_to_rc is the unit direction from dst primary to rc, incoming at rc is its negative
float3 rc_outgoing_radiance(PathSample src, float3 dir_primary_to_rc)
{
    // sky and area light nee samples carry no continuation past rc, short circuit
    if (dot(src.rc_L_post, src.rc_L_post) <= 0.0f)
    {
        return src.rc_L_nee;
    }

    float3 view_at_rc = -dir_primary_to_rc;
    // rc is a suffix vertex so the full brdf is active
    float pdf;
    float3 f_rc = restir_rc_bsdf_evaluate(src, view_at_rc, src.rc_outgoing_dir, pdf);
    float mis = src.endpoint_pooled && src.path_length == src.rc_length + 1u
        ? restir_pooled_endpoint_mis(src.path_length, src.endpoint_light, src.rc_nee_pdf, pdf)
        : restir_rc_endpoint_mis(src.path_length - src.rc_length + 2u, src.path_length, src.endpoint_light,
            src.endpoint_environment, src.rc_normal, src.rc_outgoing_dir, pdf);
    if (src.endpoint_light == RESTIR_ENDPOINT_BSDF && src.path_length == src.rc_length + 1u &&
        src.rc_nee_reflection_disabled && (src.flags & PATH_FLAG_TRANSMISSION_AFTER_RC) == 0u)
        mis = 1.0f;
    return src.rc_L_nee + f_rc * src.rc_L_post * mis;
}

float3 sample_sky(float3 dir);

// Reference TraceTemporalUpdate refreshes environment endpoints in the current
// scene even when light-tile RIS disables general suffix replay. Analytic sun
// endpoints belong to a separate light strategy and must retain that identity.
void restir_refresh_environment_endpoint(inout PathSample s)
{
    if (is_sky_sample(s) &&
        (s.endpoint_light == RESTIR_ENDPOINT_BSDF || s.endpoint_light == RESTIR_ENDPOINT_SKY))
    {
        s.rc_L_nee = sample_sky(s.rc_pos);
        s.rc_L_post = 0.0f;
    }
}

// reconnection shift from source primary to destination, ok=false on degenerate geometry
// visibility is checked separately so non visibility critical passes can skip the ray cast
// Non-reconnectable paths use shared-random-number replay and carry its updated descriptor.
ShiftResult try_reconnection_shift(
    PathSample src,
    float3 src_primary_pos,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic,
    float3 src_view_dir,
    float3 dst_geometric_normal,
    bool destination_previous_frame = false)
{
    ShiftResult result;
    result.sample = src;
    result.f_dst    = float3(0, 0, 0);
    result.jacobian = 0.0f;
    result.ok       = false;

    float3 destination_camera = destination_previous_frame ? buffer_frame.camera_position_previous : get_camera_position();
    if (has_reconnection(src) && src.rc_length > 2u)
    {
        // Even an unchanged primary can have moving occluders along its prefix.
        // trace_shift_visibility relies on replay to validate these segments.
        return restir_shift_prefix(src, dst_pos, dst_normal, dst_view_dir, dst_albedo,
            dst_roughness, dst_metallic, destination_camera, dst_geometric_normal);
    }

    if (is_sky_sample(src))
    {
        // BSDF-sampled glossy environment paths must follow the reflected
        // direction under motion instead of reconnecting to a fixed sky texel.
        // NEE sun samples retain their light-sampling parameterization.
        if (!is_nee_sample(src) && !has_reconnection(src))
            return try_random_replay_shift(src, src_primary_pos, dst_pos, dst_normal,
                dst_view_dir, dst_albedo, dst_roughness, dst_metallic, src_view_dir, destination_camera, dst_geometric_normal);
        restir_refresh_environment_endpoint(src);
        result.sample = src;
        float3 dir = src.rc_pos;
        if (!is_nee_sample(src))
        {
            float pdf_dst;
            evaluate_reflection_brdf(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_geometric_normal,
                dst_view_dir, dir, pdf_dst, restir_primary_specular_blend(dst_roughness));
            if (pdf_dst * pdf_dst * RESTIR_RC_MIN_PDF_ROUGHNESS > 1.0f)
                return result;
        }
        if (dot(dst_normal, dir) <= RESTIR_RC_COS_FRONT)
            return result;

        float3 brdf_cos = eval_surface_reflection_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_geometric_normal, dst_view_dir, dir, restir_primary_specular_blend(dst_roughness));
        if (all(brdf_cos <= 0.0f))
            return result;

        result.f_dst    = brdf_cos * rc_outgoing_radiance(src, dir) *
            restir_primary_endpoint_mis(src, dst_pos, dst_normal, dst_geometric_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic, dir);
        result.jacobian = 1.0f;
        result.ok       = true;
        result.sample.F = result.f_dst;
        return result;
    }

    // Complete replay paths also own their visibility checks. Do not let an
    // unchanged primary reuse cached lighting without tracing the current scene.
    if (!has_reconnection(src))
        return try_random_replay_shift(src, src_primary_pos, dst_pos, dst_normal, dst_view_dir,
            dst_albedo, dst_roughness, dst_metallic, src_view_dir, destination_camera, dst_geometric_normal);

    float3 connection_origin = restir_reconnection_ray_origin(src, dst_pos, dst_geometric_normal);
    if (is_triangle_reconnection(src) || is_area_light_sample(src))
    {
        if (!restir_reconstruct_rc(src, connection_origin)) return result;
        result.sample = src;
    }

    // Direct reconnections still receive a separate visibility test.
    bool is_identity = all(dst_pos == src_primary_pos) && all(dst_view_dir == src_view_dir);
    // A stationary primary does not imply a stationary/unchanged light.
    // Analytic endpoints must resolve and re-evaluate current light parameters.
    if (is_identity && !is_dirac_light_sample(src) && !is_area_light_sample(src) && !is_triangle_reconnection(src))
    {
        // Primary normals and roughness retain their original G-buffer bits.
        // Compare the actual source BSDF, without a second quantization step.
        float4 stored_material = unpack_float_to_uint8x4(pack_uint8x4_to_float(
            dst_albedo.r, dst_albedo.g, dst_albedo.b, dst_metallic));
        bool same_shading = all(src.src_normal == dst_normal) &&
            all(src.src_albedo == stored_material.rgb) && src.src_metallic == stored_material.a &&
            src.src_roughness == dst_roughness;
        if (same_shading && restir_reflection_hemisphere(dst_geometric_normal,
            dst_view_dir, src.rc_pos - connection_origin))
        {
            result.f_dst = src.F;
            result.sample.F = src.F;
            result.jacobian = 1.0f;
            result.ok = all(isfinite(src.F)) && any(src.F > 0.0f);
            return result;
        }
    }

    // primary nee on a point or spot light, the endpoint is the light itself so there is no rc
    // surface whose area density could change, falloff and cone are re-evaluated at dst
    if (is_dirac_light_sample(src))
    {
        uint light_index;
        float3 dirac_radiance;
        if (!restir_resolve_dirac_light(src, light_index) || !restir_dirac_light_radiance(light_index, connection_origin, dirac_radiance))
            return result;

        result.sample.endpoint_light = light_index;
        result.sample.rc_pos         = light_parameters[light_index].position;

        float3 to_light = result.sample.rc_pos - connection_origin;
        float distance_sq = dot(to_light, to_light);
        if (!restir_connection_distance_valid(distance_sq) || all(dirac_radiance <= 0.0f))
            return result;

        float dist = sqrt(distance_sq);
        float3 dir = to_light / dist;
        if (dot(dst_normal, dir) <= RESTIR_RC_COS_FRONT)
            return result;

        float3 brdf_cos = eval_surface_reflection_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_geometric_normal, dst_view_dir, dir, restir_primary_specular_blend(dst_roughness));
        if (all(brdf_cos <= 0.0f))
            return result;

        result.f_dst            = brdf_cos * dirac_radiance;
        result.jacobian         = 1.0f;
        result.ok               = true;
        result.sample.F         = result.f_dst;
        result.sample.rc_L_nee  = dirac_radiance;
        result.sample.rc_normal = -dir;
        return result;
    }

    float3 rc_from_dst = src.rc_pos - connection_origin;
    float  dist_dst_sq = dot(rc_from_dst, rc_from_dst);
    if (!restir_connection_distance_valid(dist_dst_sq))
        return result;

    float dist_dst = sqrt(dist_dst_sq);
    float3 dir_dst = rc_from_dst / dist_dst;

    if (dot(dst_normal, dir_dst) <= RESTIR_RC_COS_FRONT)
        return result;

    float cos_rc_dst = dot(src.rc_normal, -dir_dst);

    // The source edge belongs to the frame in which the sample was drawn.
    // Its geometry factor is retained in rc_pdf; connecting the current RC
    // to the old primary can be degenerate after motion and is not that edge.
    if (cos_rc_dst <= RESTIR_RC_COS_FRONT)
        return result;

    float3 rc_geometric_normal = unpack_rc_geometric_normal(src.rc_geometric_normal_packed);
    float geometry_cos_dst = abs(dot(rc_geometric_normal, -dir_dst));

    // dst side forward footprint test mirrors the source reconnection criteria, lin 2026 4
    // Use the actual mixture PDF on both sides; the supplemental black-box
    // criterion also rejects a sharply peaked preceding scattering event.
    if ((!is_identity || is_triangle_reconnection(src)) && !is_nee_sample(src))
    {
        float pdf_dst;
        evaluate_reflection_brdf(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_geometric_normal,
            dst_view_dir, dir_dst, pdf_dst, restir_primary_specular_blend(dst_roughness));
        if (pdf_dst * pdf_dst * RESTIR_RC_MIN_PDF_ROUGHNESS > 1.0f)
            return result;
        float forward_pdf_cos = pdf_dst * geometry_cos_dst;
        float inverse_pdf_cos = is_rc_emissive(src)
            ? RESTIR_RC_EMITTER_PDF * abs(dot(dst_geometric_normal, dir_dst)) : 0.0f;
        if (!is_rc_emissive(src) && any(src.rc_L_post > 0.0f))
        {
            float pdf_rc;
            restir_rc_bsdf_evaluate(src, -dir_dst, src.rc_outgoing_dir, pdf_rc);
            inverse_pdf_cos = pdf_rc * abs(dot(dst_geometric_normal, dir_dst));
        }
        if (!restir_dual_footprint_allowed(dist_dst_sq, forward_pdf_cos, inverse_pdf_cos,
            RESTIR_RC_FOOTPRINT_C * restir_primary_footprint_sq(dst_pos, dst_geometric_normal, destination_camera)))
            return result;
    }

    float3 brdf_cos = eval_surface_reflection_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_geometric_normal, dst_view_dir, dir_dst, restir_primary_specular_blend(dst_roughness));
    if (all(brdf_cos <= 0.0f))
        return result;

    // Reference shiftPathReconnection: preserve the source geometry factor
    // across stores, then replace it with the destination factor for the next
    // shift. Recomputing the denominator from a reconstructed/moving RC loses
    // the geometry of the sample that actually produced the cached integrand.
    // Rectangle endpoints use fixed local UVs, so their area measure is part
    // of the Jacobian when the emitter is resized between source and destination.
    float geometry_dst = geometry_cos_dst / dist_dst_sq * restir_endpoint_area_measure(src);
    if (!(src.rc_pdf > 0.0f) || !(geometry_dst > 0.0f))
        return result;
    float jacobian = geometry_dst / src.rc_pdf;
    if (!(jacobian > 0.0f) || isnan(jacobian) || isinf(jacobian))
        return result;

    // RC outgoing direction is represented in solid angle, so reconnecting
    // preserves that coordinate. Its initial sampling PDF is already in W.
    result.f_dst    = brdf_cos * rc_outgoing_radiance(src, dir_dst) *
        restir_primary_endpoint_mis(src, connection_origin, dst_normal, dst_geometric_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic, dir_dst);
    result.jacobian = jacobian;
    result.sample.rc_pdf = geometry_dst;
    result.ok       = true;
    result.sample.F = result.f_dst;
    return result;
}

// forward declarations, the helpers are defined later in this header
bool trace_shift_visibility(PathSample src, float3 dst_pos, float3 dst_normal);

// shared path tracing parameters used by the initial trace suffix, a constant continuation
// probability keeps the roulette decision dependent only on the seed draw
static const uint  RESTIR_RR_START        = 1;
static const float RESTIR_RR_CONTINUATION = 0.75f;
static const float SKY_MIP_LEVEL             = 2.0f;
// sun cone half angle, matches the real sun angular radius
static const float SUN_CONE_HALF_ANGLE       = 0.0047f;

// Use one finite sun emitter for primary and secondary NEE. Its radiance is
// authored directional intensity times this solid-angle PDF at every bounce.
float3 sample_restir_sun_cone(float3 direction, float2 xi, out float pdf)
{
    float cos_max = cos(SUN_CONE_HALF_ANGLE);
    pdf = 1.0f / (2.0f * PI * (1.0f - cos_max));
    float phi = 2.0f * PI * xi.x;
    float cos_theta = lerp(cos_max, 1.0f, xi.y);
    float sin_theta = sqrt(max(0.0f, 1.0f - cos_theta * cos_theta));
    return local_to_world(float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta), direction);
}

// the analytic directional light exclusively owns the sun, every restir sky read excludes the
// panorama disc so its energy is never counted a second time, the exclusion radius covers the
// disc (0.00935) plus the mip 2 blur halo, cos(0.05)
static const float RESTIR_SUN_EXCLUSION_COS  = 0.99875f;

// samples a sky color along a direction (used when a bounce misses geometry)
// sun free, the analytic light loop delivers the sun with the correct unclamped energy
float3 sample_sky(float3 dir)
{
    if ((uint)buffer_frame.restir_pt_light_count > 0)
    {
        LightParameters p = light_parameters[0];
        if ((p.flags & (1u << 0)) != 0 && p.intensity > 0.0f && dot(dir, -p.direction) >= RESTIR_SUN_EXCLUSION_COS)
        {
            return float3(0, 0, 0);
        }
    }
    float2 uv  = direction_sphere_uv(dir);
    float3 sky = tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), uv, SKY_MIP_LEVEL).rgb;
    return sky;
}

// emissive triangle nee pool active check, false skips the emtri strategy entirely
bool is_emtri_pool_active()
{
    return buffer_frame.restir_pt_emissive_tri_count > 0.5f;
}

// binary search over the cdf, returns the triangle whose cumulative weight first exceeds u
uint emtri_pick_index(float u, uint count)
{
    uint lo = 0u;
    uint hi = count;
    while (lo < hi)
    {
        uint mid = (lo + hi) >> 1u;
        if (emissive_triangles[mid].cdf < u)
        {
            lo = mid + 1u;
        }
        else
        {
            hi = mid;
        }
    }
    return min(lo, count - 1u);
}

// power proportional light pick weight, lin 2022 6.1, all four light types are eligible
float light_pick_weight(LightParameters l)
{
    if (l.intensity <= 0.0f)
        return 0.0f;

    bool is_directional = (l.flags & (1u << 0)) != 0;
    bool is_point       = (l.flags & (1u << 1)) != 0;
    bool is_spot        = (l.flags & (1u << 2)) != 0;
    bool is_area        = (l.flags & (1u << 6)) != 0;

    float lum = max(luminance(l.color.rgb), 1e-3f);
    if (is_directional)
    {
        return l.intensity * lum;
    }
    if (is_area)
    {
        float emitter_area = max(l.area_width * l.area_height, 0.0001f);
        return l.intensity * lum * emitter_area;
    }
    if (is_point || is_spot)
    {
        // dirac local light, weight by intensity, luminance and cone fraction
        float cone_factor = 1.0f;
        if (is_spot)
        {
            float cos_outer = cos(l.angle);
            cone_factor     = max(1.0f - cos_outer, 0.05f);
        }
        return l.intensity * lum * cone_factor;
    }
    return 0.0f;
}

// total nee pick weight, the loop is o(light_count)
float compute_total_light_weight()
{
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    float total = 0.0f;
    for (uint i = 0; i < light_count; i++)
    {
        total += light_pick_weight(light_parameters[i]);
    }
    return total;
}

#include "restir_bsdf.hlsl"

// The selected NEE endpoint retains physical radiance and its full source PDF.
struct RestirLightSample
{
    float3 radiance_sum;
    float3 direction;
    float3 incident;
    float weight_sum;
    float selected_target;
    float initial_weight;
    uint light_index;
    uint emtri_draw;
    uint emitter_triangle;
    uint2 endpoint_identity;
    float endpoint_area; // zero for non-rectangular lights
    uint endpoint_area_position; // original packed tile coordinates
    uint3 endpoint_hit; // TLAS or pool triangle identity, followed by packed barycentrics.
    float3 endpoint_position;
    float3 endpoint_normal;
    float3 endpoint_radiance;
    float endpoint_pdf;
    bool endpoint_valid;
    bool endpoint_sky;
    bool endpoint_dirac;
    bool transmission;
};

float3 restir_light_sample_origin(RestirLightSample light, float3 position, float3 geometric_normal)
{
    return restir_ray_origin(position, geometric_normal, light.transmission);
}

float3 restir_light_proposal_origin(float3 position, float3 geometric_normal, float3 view,
    float3 direction, bool dielectric)
{
    bool transmission = dielectric && !restir_reflection_hemisphere(geometric_normal, view, direction);
    return restir_ray_origin(position, geometric_normal, transmission);
}

PathSample restir_light_endpoint_descriptor(RestirLightSample light, float3 origin, uint vertex, uint seed)
{
    PathSample s = (PathSample)0;
    if (!light.endpoint_valid) return s;
    s.seed_path = seed;
    s.rc_length = vertex + 1u;
    s.path_length = vertex + 1u;
    s.flags = PATH_FLAG_HAS_RC | PATH_FLAG_NEE | (light.endpoint_sky ? PATH_FLAG_SKY : 0u);
    if (light.transmission) s.flags |= PATH_FLAG_TRANSMISSION_BEFORE_RC;
    s.rc_hit = light.endpoint_hit;
    s.rc_pos = light.endpoint_position;
    s.rc_normal = light.endpoint_normal;
    s.rc_outgoing_dir = light.endpoint_normal;
    s.rc_L_nee = light.endpoint_radiance;
    s.rc_geometric_normal_packed = pack_rc_geometric_normal(light.endpoint_normal);
    float3 delta = light.endpoint_position - origin;
    float distance_sq = dot(delta, delta);
    s.rc_pdf = light.endpoint_sky || light.endpoint_dirac ? 1.0f :
        (restir_connection_distance_valid(distance_sq) ?
            abs(dot(unpack_rc_geometric_normal(s.rc_geometric_normal_packed), delta * rsqrt(distance_sq))) / distance_sq : 0.0f);
    s.rc_pdf *= light.endpoint_area > 0.0f ? light.endpoint_area : 1.0f;
    if (light.endpoint_area > 0.0f) s.rc_hit.z = light.endpoint_area_position;
    s.endpoint_light = light.light_index;
    s.endpoint_pooled = light.light_index == RESTIR_ENDPOINT_EMTRI;
    s.endpoint_environment = light.light_index == RESTIR_ENDPOINT_SKY;
    s.endpoint_geometry = light.endpoint_dirac || light.endpoint_area > 0.0f ? light.endpoint_identity : uint2(light.emitter_triangle, 0u);
    // Analytic endpoints do not need the proposal index. Keeping it would let
    // rectangular-light proposal 7 collide with the persistent dirac tag.
    s.endpoint_emtri_draw = light.light_index < RESTIR_ENDPOINT_EMTRI
        ? (light.endpoint_dirac ? RESTIR_ENDPOINT_DIRAC_TAG :
            (light.endpoint_area > 0.0f ? RESTIR_ENDPOINT_AREA_TAG : 0u)) : light.emtri_draw;
    s.rc_nee_pdf = light.endpoint_pdf;
    return s;
}

// One light-tile RIS technique per vertex, with 32/v^2 unshadowed proposals.
// Selection uses a separate RNG stream; prefix replay consumes only its seed.
RestirLightSample sample_direct_lighting_at_vertex(
    float3 shading_pos, float3 shading_normal, float3 geometric_normal,
    float3 view_dir, float3 albedo, float roughness, float metallic,
    float specular_blend, inout uint seed, bool replay, uint vertex, bool dielectric = false, float eta = 1.0f, bool allow_reflection = true, float3 transmission_tint = 1.0f)
{
    RestirLightSample result = (RestirLightSample)0;
    float offset_xi = random_float(seed);
    uint selection_seed = seed ^ 0x9e3779b9u;
    if (replay) return result; // NEE leaves always reconnect; full replay is BSDF-only.
    float3 surface_pos = shading_pos;
    shading_pos = restir_ray_origin(surface_pos, geometric_normal);
    PathSurface surface = (PathSurface)0;
    surface.hit_normal = shading_normal;
    surface.geometric_normal = geometric_normal;
    surface.albedo = albedo;
    surface.roughness = roughness;
    surface.metallic = metallic;
    uint candidate_count = restir_nee_candidate_count(vertex);
    uint tile_offset = 0u;
    [loop] for (uint proposal = 0u; proposal < candidate_count; ++proposal)
    {
        RestirLightTileEntry entry = restir_light_tile_entry(offset_xi, proposal, candidate_count, tile_offset);
        float u = random_float(selection_seed);
        RestirLightSample candidate = (RestirLightSample)0;
        candidate.emtri_draw = proposal;
        if (entry.lights.x != 0xffffffffu)
        {
            if (vertex == 1u && !is_restir_pt_direct_enabled()) continue;
            uint index = entry.lights.x;
            if (index >= (uint)buffer_frame.restir_pt_light_count) continue;
            LightParameters light = light_parameters[index];
            float pick_pdf = asfloat(entry.lights.y);
            if (light.intensity <= 0.0f || !restir_pdf_valid(pick_pdf)) continue;
            candidate.light_index = index;
            candidate.endpoint_identity = uint2(light.identity_low, light.identity_high);
            float attenuation = 1.0f;
            float point_pdf = 1.0f;
            if ((light.flags & (1u << 0)) != 0u)
            {
                candidate.direction = sample_restir_sun_cone(-light.direction, restir_analytic_sample_position(entry), point_pdf);
                candidate.endpoint_position = candidate.direction;
                candidate.endpoint_normal = -candidate.direction;
                candidate.endpoint_sky = true;
                attenuation = point_pdf;
            }
            else
            {
                float3 endpoint = light.position;
                bool area = (light.flags & (1u << 6)) != 0u;
                bool punctual = (light.flags & ((1u << 1) | (1u << 2))) != 0u;
                if (area)
                {
                    if (light.area_width <= 0.0f || light.area_height <= 0.0f) continue;
                    float3 right = normalize(light.direction_right);
                    float3 up = normalize(cross(light.direction, right));
                    candidate.endpoint_area = light.area_width * light.area_height;
                    candidate.endpoint_area_position = entry.analytic_position;
                    float2 position = restir_analytic_sample_position(entry) - 0.5f;
                    endpoint += position.x * light.area_width * right + position.y * light.area_height * up;
                    candidate.endpoint_normal = light.direction;
                }
                else if (!punctual) continue;
                shading_pos = restir_light_proposal_origin(surface_pos, geometric_normal, view_dir,
                    endpoint - surface_pos, dielectric);
                float3 delta = endpoint - shading_pos;
                float distance_sq = dot(delta, delta);
                if (!restir_connection_distance_valid(distance_sq)) continue;
                float distance = sqrt(distance_sq);
                candidate.direction = delta / distance;
                candidate.endpoint_position = endpoint;
                if (area)
                {
                    float cosine = dot(light.direction, -candidate.direction);
                    if (cosine <= 0.0f) continue;
                    point_pdf = distance_sq / (light.area_width * light.area_height * cosine);
                    attenuation = restir_range_window(distance, light.range);
                }
                else
                {
                    if (light.range <= 0.0f || distance >= light.range) continue;
                    candidate.endpoint_dirac = true;
                    candidate.endpoint_normal = -candidate.direction;
                    attenuation = restir_range_window(distance, light.range) / (distance_sq + 0.0001f);
                    attenuation *= restir_spot_factor(light, candidate.direction);
                }
            }
            candidate.endpoint_radiance = light.color.rgb * light.intensity * attenuation;
            candidate.endpoint_pdf = pick_pdf * point_pdf;
        }
        else if (entry.lights.z != 0xffffffffu)
        {
            uint index = entry.lights.z;
            if (index >= (uint)buffer_frame.restir_pt_emissive_tri_count) continue;
            EmissiveTriangle tri = emissive_triangles[index];
            float3 bary = unpack_hit_barycentrics(entry.lights.w);
            float3 endpoint = tri.v0 * bary.x + tri.v1 * bary.y + tri.v2 * bary.z;
            shading_pos = restir_light_proposal_origin(surface_pos, geometric_normal, view_dir,
                endpoint - surface_pos, dielectric);
            float3 delta = endpoint - shading_pos;
            float distance_sq = dot(delta, delta);
            if (!restir_connection_distance_valid(distance_sq)) continue;
            candidate.direction = delta * rsqrt(distance_sq);
            if (dot(tri.normal, -candidate.direction) <= 0.0f) continue;
            candidate.light_index = RESTIR_ENDPOINT_EMTRI;
            candidate.emitter_triangle = index;
            candidate.endpoint_hit = uint3(index, 0u, entry.lights.w);
            candidate.endpoint_position = endpoint;
            candidate.endpoint_normal = tri.normal;
            candidate.endpoint_radiance = tri.emission;
            candidate.endpoint_pdf = asfloat(entry.lights.y) * restir_triangle_point_pdf(shading_pos, tri.v0, tri.v1, tri.v2, endpoint);
        }
        else
        {
            candidate.light_index = RESTIR_ENDPOINT_SKY;
            candidate.direction = entry.environment_direction;
            candidate.endpoint_position = candidate.direction;
            candidate.endpoint_normal = -candidate.direction;
            candidate.endpoint_sky = true;
            candidate.endpoint_radiance = sample_sky(candidate.direction);
            candidate.endpoint_pdf = entry.environment_pdf;
        }
        uint kind = candidate.light_index == RESTIR_ENDPOINT_SKY ? RESTIR_LIGHT_KIND_ENVIRONMENT :
            (candidate.light_index == RESTIR_ENDPOINT_EMTRI ? RESTIR_LIGHT_KIND_EMISSIVE : RESTIR_LIGHT_KIND_ANALYTIC);
        candidate.endpoint_pdf *= restir_light_kind_pmf_at_vertex(kind, vertex) / max(restir_light_kind_pmf(kind), 1e-20f);
        candidate.transmission = dielectric && !restir_reflection_hemisphere(geometric_normal, view_dir, candidate.direction);
        if ((!dielectric && dot(shading_normal, candidate.direction) <= 0.0f) || !restir_pdf_valid(candidate.endpoint_pdf)) continue;
        // Match the reference default: upper-hemisphere NEE is disabled while
        // inside a dielectric volume. Rejected proposals remain zero draws.
        if (!allow_reflection && !candidate.transmission) continue;
        // An endpoint within the offset band can appear on the opposite side
        // after offsetting. Keep the proposal's geometry/PDF and visibility on
        // the same side; such a proposal is a zero draw, not a resample.
        if (dielectric && !candidate.endpoint_sky &&
            any(shading_pos != restir_light_sample_origin(candidate, surface_pos, geometric_normal))) continue;
        float bsdf_pdf;
        float3 brdf = restir_surface_evaluate(surface, view_dir, candidate.direction, bsdf_pdf,
            specular_blend, dielectric, eta, transmission_tint);
        float mis = candidate.light_index == RESTIR_ENDPOINT_EMTRI ?
            restir_pooled_endpoint_mis(vertex + 1u, candidate.light_index, candidate.endpoint_pdf, bsdf_pdf) :
            (candidate.light_index == RESTIR_ENDPOINT_SKY ?
                restir_environment_endpoint_mis(vertex + 1u, candidate.light_index, shading_normal, candidate.direction, bsdf_pdf) : 1.0f);
        float3 contribution = brdf * candidate.endpoint_radiance * mis;
        float target = target_scalar(contribution);
        float weight = target / candidate.endpoint_pdf;
        if (!(weight > 0.0f) || !isfinite(weight)) continue;
        float weight_sum = result.weight_sum + weight;
        if (u * weight_sum < weight)
        {
            result = candidate;
            result.radiance_sum = contribution;
            result.selected_target = target;
            result.endpoint_valid = true;
        }
        result.weight_sum = weight_sum;
    }
    if (!result.endpoint_valid) return result;
    // Trace only the RIS winner, using the same segment and alpha handling as
    // reconnection. An occluded winner remains a zero draw, never a replacement.
    shading_pos = restir_light_sample_origin(result, surface_pos, geometric_normal);
    PathSample endpoint = restir_light_endpoint_descriptor(result, shading_pos, 1u, seed);
    if (!trace_shift_visibility(endpoint, surface_pos, geometric_normal)) return (RestirLightSample)0;
    float inverse_density = result.weight_sum / (float(candidate_count) * result.selected_target);
    result.radiance_sum *= inverse_density;
    result.incident = result.endpoint_radiance / result.endpoint_pdf;
    result.initial_weight = inverse_density * result.endpoint_pdf;
    return result;
}

// visibility ray from dst primary to rc, sky samples test reachability to the sky
bool trace_shift_visibility(PathSample src, float3 dst_pos, float3 dst_normal)
{
    // Prefix replay already traced the actual last-prefix-to-RC segment.
    if ((has_reconnection(src) && src.rc_length > 2u) || (src.rc_length == 0u && src.path_length > 0u))
        return true;
    RayDesc ray;
    ray.Origin = restir_reconnection_ray_origin(src, dst_pos, dst_normal);
    // Reference evalSegmentVisibility: the surface origin is already offset.
    // Its default disables any additional gap at the start of the segment.
    ray.TMin = 0.0f;
    if (is_sky_sample(src))
    {
        ray.Direction = src.rc_pos;
        ray.TMax = RESTIR_RAY_T_MAX;
    }
    else
    {
        // Aim at the endpoint from the actual offset origin. A parallel ray
        // from the unoffset direction tests a different segment near corners.
        float3 to_rc = src.rc_pos - ray.Origin;
        float distance = length(to_rc);
        if (!(distance > 0.0f) || !isfinite(distance))
            return false;
        ray.Direction = to_rc / distance;
        // Reference kShadowRayTMaxRatio. A camera-dependent endpoint gap can
        // skip nearby occluders and change visibility when only the camera moves.
        ray.TMax = 0.999f * distance;
    }

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            rt_candidate_opaque(query.CandidateInstanceIndex(), query.CandidatePrimitiveIndex(),
                query.CandidateTriangleBarycentrics(), ray.Origin + ray.Direction * query.CandidateTriangleRayT()))
            query.CommitNonOpaqueTriangleHit();
    }

    return query.CommittedStatus() == COMMITTED_NOTHING;
}

// identity shift, jacobian is 1, no rc flag or footprint gate since this is the source pixel
// the acceptance gates below must stay identical to try_reconnection_shift, this function
// supplies the own domain density in the pairwise mis denominators while try_reconnection_shift
// supplies the cross domain one, so the moment the two disagree the shares stop summing to one
// and the estimator gains energy, corners are where they used to disagree because two walls
// meeting put the reconnection vertex a centimetre away and edge on, firing every gate that only
// the cross domain evaluator had, which drove each technique's share to 1 and let the temporal
// feedback loop compound the surplus until the clamps saturated
ShiftResult evaluate_path_integrand(
    PathSample src,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_geometric_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic)
{
    ShiftResult result;
    result.sample = src;
    result.f_dst    = float3(0, 0, 0);
    result.jacobian = 1.0f;
    result.ok       = false;

    if (has_reconnection(src) && src.rc_length > 2u)
    {
        result.f_dst = src.F;
        result.ok = all(isfinite(src.F)) && any(src.F > 0.0f);
        return result;
    }

    float3 dir;
    if (is_sky_sample(src))
    {
        dir = src.rc_pos;
    }
    else
    {
        float3 to_rc = src.rc_pos - dst_pos;
        float  d2    = dot(to_rc, to_rc);
        if (!restir_connection_distance_valid(d2))
            return result;
        dir = to_rc * rsqrt(d2);

        if (dot(src.rc_normal, -dir) <= RESTIR_RC_COS_FRONT)
            return result;
    }

    if (dot(dst_normal, dir) <= RESTIR_RC_COS_FRONT)
        return result;

    float3 brdf_cos = eval_surface_reflection_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_geometric_normal, dst_view_dir, dir, restir_primary_specular_blend(dst_roughness));
    if (all(brdf_cos <= 0.0f))
        return result;

    // dst incoming equals src incoming by construction so f_rc matches the original, no drift
    result.f_dst = brdf_cos * rc_outgoing_radiance(src, dir) *
        restir_primary_endpoint_mis(src, dst_pos, dst_normal, dst_geometric_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic, dir);
    result.ok    = true;
    return result;
}

// The cached RGB value and its scalar target must describe the same sample.
// Re-evaluating packed normals/directions here introduces a multiplicative error
// every time history is reused, particularly at glossy reconnection vertices.
ShiftResult self_shift_evaluate(PathSample src, float3 pos, float3 normal, float3 view,
    float3 albedo, float roughness, float metallic)
{
    ShiftResult result = (ShiftResult)0;
    result.sample = src;
    result.f_dst = src.F;
    result.jacobian = 1.0f;
    result.ok = all(isfinite(src.F)) && any(src.F > 0.0f);
    return result;
}

// scalar target of f(y) at dst via reconnection shift, for cross pixel reuse
// use target_pdf_self at the source pixel so invalid rc samples keep a non zero target there
float target_pdf_at_dst(
    PathSample src,
    float3 src_primary_pos,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic,
    float3 dst_geometric_normal)
{
    ShiftResult r = try_reconnection_shift(src, src_primary_pos, dst_pos, dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic,
        normalize(get_camera_position() - src_primary_pos), dst_geometric_normal);
    if (!r.ok)
        return 0.0f;

    return target_scalar(r.f_dst);
}

float target_pdf_self(
    PathSample src,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic)
{
    ShiftResult r = self_shift_evaluate(src, dst_pos, dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic);
    if (!r.ok)
        return 0.0f;

    return target_scalar(r.f_dst);
}

// shades the reservoir sample at the current pixel via self shift
// returns diffuse only gi f(y) * W demodulated by primary albedo
float3 shade_reservoir_path(Reservoir r, float3 dst_pos, float3 dst_normal, float3 dst_view_dir, float3 dst_albedo, float dst_roughness, float dst_metallic)
{
    if (r.M <= 0.0f || r.W <= 0.0f)
        return float3(0, 0, 0);

    ShiftResult shift = self_shift_evaluate(r.sample, dst_pos, dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic);
    if (!shift.ok)
        return float3(0, 0, 0);

    // diffuse albedo demodulation, the stored gi is albedo proportional so this yields clean
    // irradiance
    float3 gi = (shift.f_dst * r.W) / restir_gi_demodulator(dst_albedo);

    return gi;
}

#include "restir_path.hlsl"

#endif // SPARTAN_RESTIR_RESERVOIR
