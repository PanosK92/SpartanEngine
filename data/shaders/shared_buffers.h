/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SPARTAN_SHARED_BUFFERS
#define SPARTAN_SHARED_BUFFERS

// shared type abstraction - resolves to native types on each side
#ifdef __cplusplus
    #define SHARED_FLOAT    float
    #define SHARED_FLOAT2   spartan::math::Vector2
    #define SHARED_FLOAT3   spartan::math::Vector3
    #define SHARED_FLOAT4   spartan::math::Vector4
    #define SHARED_COLOR    spartan::Color
    #define SHARED_MATRIX   spartan::math::Matrix
    #define SHARED_UINT     uint32_t
    #define SHARED_INT      int32_t
    #define SHARED_DOUBLE   double
    #define SHARED_BOOL     bool
    #define SHARED_DEFAULT(x) = x
#else
    #define SHARED_FLOAT    float
    #define SHARED_FLOAT2   float2
    #define SHARED_FLOAT3   float3
    #define SHARED_FLOAT4   float4
    #define SHARED_COLOR    float4
    #define SHARED_MATRIX   matrix
    #define SHARED_UINT     uint
    #define SHARED_INT      int
    #define SHARED_DOUBLE   double
    #define SHARED_BOOL     bool
    #define SHARED_DEFAULT(x)
#endif

// constant buffer - updates once per frame
struct FrameBufferData
{
    SHARED_MATRIX view;
    SHARED_MATRIX view_inverted;
    SHARED_MATRIX view_previous;
    SHARED_MATRIX projection;
    SHARED_MATRIX projection_inverted;
    SHARED_MATRIX projection_previous;
    SHARED_MATRIX view_projection;
    SHARED_MATRIX view_projection_inverted;
    SHARED_MATRIX view_projection_orthographic;
    SHARED_MATRIX view_projection_unjittered;
    SHARED_MATRIX view_projection_previous;
    SHARED_MATRIX view_projection_previous_unjittered;

    SHARED_FLOAT2 resolution_render;
    SHARED_FLOAT2 resolution_output;

    SHARED_FLOAT2 taa_jitter_current;
    SHARED_FLOAT2 taa_jitter_previous;

    SHARED_FLOAT camera_aperture;
    SHARED_FLOAT delta_time;
    SHARED_UINT  frame;
    SHARED_UINT  options;

    SHARED_FLOAT3 camera_position;
    SHARED_FLOAT  camera_near;

    SHARED_FLOAT3 camera_forward;
    SHARED_FLOAT  camera_far;

    SHARED_FLOAT camera_last_movement_time;
    SHARED_FLOAT hdr_enabled;
    SHARED_FLOAT hdr_max_nits;
    SHARED_FLOAT padding;

    SHARED_FLOAT3 camera_position_previous;
    SHARED_FLOAT  resolution_scale;

    SHARED_DOUBLE time;
    SHARED_FLOAT  camera_fov;
    SHARED_FLOAT  padding2;

    SHARED_FLOAT3 wind;
    SHARED_FLOAT  gamma;

    SHARED_FLOAT3 camera_right;
    SHARED_FLOAT  camera_exposure;

    // clouds
    SHARED_FLOAT cloud_coverage;
    SHARED_FLOAT cloud_shadows;
    SHARED_FLOAT padding3;
    SHARED_FLOAT padding4;

    // vr stereo - right eye matrices (left eye uses the primary matrices above)
    SHARED_MATRIX view_right;
    SHARED_MATRIX projection_right;
    SHARED_MATRIX view_projection_right;
    SHARED_MATRIX view_projection_inverted_right;
    SHARED_MATRIX view_projection_previous_right;
    SHARED_UINT   is_multiview;
    SHARED_UINT   padding_mv0;
    SHARED_UINT   padding_mv1;
    SHARED_UINT   padding_mv2;

#ifdef __cplusplus
    void set_bit(const bool set, const uint32_t bit)
    {
        options = set ? (options |= bit) : (options & ~bit);
    }
#endif
};

// push constant buffer - carries per-draw and per-pass data
// draw_index indexes into the bindless draw data buffer for transforms and material info
// material_index and is_transparent are pass-level state for compute shaders
// values[] carries generic per-pass parameters (3 x float4)
struct PassBufferData
{
    SHARED_UINT draw_index     SHARED_DEFAULT(0);
    SHARED_UINT material_index SHARED_DEFAULT(0);
    SHARED_UINT is_transparent SHARED_DEFAULT(0);
    SHARED_UINT padding        SHARED_DEFAULT(0);

#ifdef __cplusplus
    // c++ uses a flat float array with setter helpers
    float v[12] = {};

    void set_f3_value(const spartan::math::Vector3& value) { v[0] = value.x; v[1] = value.y; v[2] = value.z; }
    void set_f3_value(float x, float y = 0.0f, float z = 0.0f) { v[0] = x; v[1] = y; v[2] = z; }

    void set_f3_value2(const spartan::math::Vector3& value) { v[4] = value.x; v[5] = value.y; v[6] = value.z; }
    void set_f3_value2(float x, float y, float z) { v[4] = x; v[5] = y; v[6] = z; }

    void set_f4_value(const spartan::Color& color) { v[8] = color.r; v[9] = color.g; v[10] = color.b; v[11] = color.a; }
    void set_f4_value(float x, float y, float z, float w) { v[8] = x; v[9] = y; v[10] = z; v[11] = w; }

    void set_f2_value(float x, float y) { v[3] = x; v[7] = y; }
#else
    // hlsl uses float4 array with swizzle accessors
    float4 values[3];
#endif
};

struct MaterialParameters
{
    SHARED_FLOAT4 color SHARED_DEFAULT(spartan::math::Vector4::Zero);

    SHARED_FLOAT2 tiling    SHARED_DEFAULT(spartan::math::Vector2::Zero);
    SHARED_FLOAT2 offset    SHARED_DEFAULT(spartan::math::Vector2::Zero);
    SHARED_FLOAT2 invert_uv SHARED_DEFAULT(spartan::math::Vector2::Zero);

    SHARED_FLOAT roughness  SHARED_DEFAULT(0.0f);
    SHARED_FLOAT metallness SHARED_DEFAULT(0.0f);
    SHARED_FLOAT normal     SHARED_DEFAULT(0.0f);
    SHARED_FLOAT height     SHARED_DEFAULT(0.0f);

    SHARED_UINT  flags       SHARED_DEFAULT(0);
    SHARED_FLOAT local_width SHARED_DEFAULT(0.0f);
    SHARED_FLOAT padding;
    SHARED_FLOAT subsurface_scattering;

    SHARED_FLOAT sheen;
    SHARED_FLOAT local_height    SHARED_DEFAULT(0.0f);
    SHARED_FLOAT world_space_uv  SHARED_DEFAULT(0.0f);
    SHARED_FLOAT padding2;

    SHARED_FLOAT anisotropic;
    SHARED_FLOAT anisotropic_rotation;
    SHARED_FLOAT clearcoat;
    SHARED_FLOAT clearcoat_roughness;

#ifndef __cplusplus
    bool has_texture_albedo()    { return (flags & (1 << 2))  != 0; }
    bool has_texture_normal()    { return (flags & (1 << 1))  != 0; }
    bool has_texture_occlusion() { return (flags & (1 << 7))  != 0; }
    bool has_texture_roughness() { return (flags & (1 << 3))  != 0; }
    bool has_texture_metalness() { return (flags & (1 << 4))  != 0; }
    bool has_texture_emissive()  { return (flags & (1 << 6))  != 0; }
    bool emissive_from_albedo()  { return (flags & (1 << 15)) != 0; }
#endif
};

struct LightParameters
{
    SHARED_COLOR  color;
    SHARED_FLOAT3 position;
    SHARED_FLOAT  intensity;
    SHARED_FLOAT3 direction;
    SHARED_FLOAT  range;
    SHARED_FLOAT  angle;
    SHARED_UINT   flags;
    SHARED_UINT   screen_space_shadow_slice_index;
    SHARED_FLOAT  area_width;
    SHARED_FLOAT  area_height;
    SHARED_MATRIX transform[6];
    SHARED_FLOAT2 atlas_offsets[6];
    SHARED_FLOAT2 atlas_scales[6];
    SHARED_FLOAT2 atlas_texel_sizes[6];
};

struct Aabb
{
    SHARED_FLOAT3 min;
    SHARED_FLOAT  is_occluder;
    SHARED_FLOAT3 max;
    SHARED_FLOAT  padding2;
};

// per-blas-instance offsets into the global geometry buffer (indexed by InstanceIndex() in rt shaders)
struct GeometryInfo
{
    SHARED_UINT vertex_offset;
    SHARED_UINT index_offset;
};

// gpu-driven indirect draw arguments (matches VkDrawIndexedIndirectCommand layout)
struct IndirectDrawArgs
{
    SHARED_UINT index_count    SHARED_DEFAULT(0);
    SHARED_UINT instance_count SHARED_DEFAULT(0);
    SHARED_UINT first_index    SHARED_DEFAULT(0);
    SHARED_INT  vertex_offset  SHARED_DEFAULT(0);
    SHARED_UINT first_instance SHARED_DEFAULT(0);
};

// per-draw data for gpu-driven rendering (indexed by draw_id in shaders)
struct DrawData
{
    SHARED_MATRIX transform;
    SHARED_MATRIX transform_previous;
    SHARED_UINT   material_index SHARED_DEFAULT(0);
    SHARED_UINT   is_transparent SHARED_DEFAULT(0);
    SHARED_UINT   aabb_index     SHARED_DEFAULT(0);
    SHARED_UINT   padding        SHARED_DEFAULT(0);
};

// vertex pulling - global geometry buffer exposed as a structured buffer
struct PulledVertex
{
    SHARED_FLOAT3 position;
    SHARED_FLOAT2 uv;
    SHARED_FLOAT3 normal;
    SHARED_FLOAT3 tangent;
};

// vertex pulling - instance buffer exposed as packed uint data (10 bytes per instance)
struct PackedInstance
{
    SHARED_UINT pos_xy;     // position_x (half16) | position_y (half16)
    SHARED_UINT pos_z_norm; // position_z (half16) | normal_oct (uint16)
    SHARED_UINT yaw_scale;  // yaw_packed (uint8) | scale_packed (uint8) | padding (uint16)
};

// gpu particle (64 bytes)
struct Particle
{
    SHARED_FLOAT3 position;
    SHARED_FLOAT  lifetime     SHARED_DEFAULT(0.0f); // remaining
    SHARED_FLOAT3 velocity;
    SHARED_FLOAT  max_lifetime SHARED_DEFAULT(0.0f); // initial
    SHARED_FLOAT4 color;                             // current rgba
    SHARED_FLOAT  size         SHARED_DEFAULT(0.0f); // current
    SHARED_FLOAT3 padding;
};

// gpu emitter parameters
struct EmitterParams
{
    SHARED_FLOAT3 position;
    SHARED_FLOAT  emission_rate    SHARED_DEFAULT(0.0f);
    SHARED_FLOAT  lifetime         SHARED_DEFAULT(0.0f);
    SHARED_FLOAT  start_speed      SHARED_DEFAULT(0.0f);
    SHARED_FLOAT  start_size       SHARED_DEFAULT(0.0f);
    SHARED_FLOAT  end_size         SHARED_DEFAULT(0.0f);
    SHARED_COLOR  start_color;
    SHARED_COLOR  end_color;
    SHARED_FLOAT  gravity_modifier SHARED_DEFAULT(0.0f);
    SHARED_FLOAT  radius           SHARED_DEFAULT(0.0f);
    SHARED_FLOAT  delta_time       SHARED_DEFAULT(0.0f);
    SHARED_UINT   max_particles    SHARED_DEFAULT(0);
    SHARED_UINT   frame            SHARED_DEFAULT(0);
    SHARED_UINT   emitter_count    SHARED_DEFAULT(0);
    SHARED_FLOAT  padding1         SHARED_DEFAULT(0.0f);
    SHARED_FLOAT  padding2         SHARED_DEFAULT(0.0f);
};

// c++ backward compatibility aliases
#ifdef __cplusplus
namespace spartan
{
    using Cb_Frame          = FrameBufferData;
    using Pcb_Pass          = PassBufferData;
    using Sb_Material       = MaterialParameters;
    using Sb_Light          = LightParameters;
    using Sb_Aabb           = Aabb;
    using Sb_GeometryInfo   = GeometryInfo;
    using Sb_IndirectDrawArgs = IndirectDrawArgs;
    using Sb_DrawData       = DrawData;
    using Sb_Particle       = Particle;
    using Sb_EmitterParams  = EmitterParams;
}
#else
// hlsl backward compatibility alias
#define aabb Aabb
#endif

#endif // SPARTAN_SHARED_BUFFERS
