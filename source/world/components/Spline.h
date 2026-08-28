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

#pragma once

//= INCLUDES ========================
#include "Component.h"
#include "../../math/Quaternion.h"
#include "../../math/Vector2.h"
#include "../../math/Vector3.h"
#include "../../core/Event.h"
//===================================

namespace spartan
{
    class Mesh;

    // cross-section profile that gets extruded along the spline
    enum class SplineProfile : uint8_t
    {
        Road,    // flat strip
        Wall,    // vertical quad
        Tube,    // circular cross-section
        Fence,   // thin tall rectangle
        Channel, // u-shaped gutter/trench
        Max
    };

    // how a spline derives its path from another spline (the source)
    enum class SplineAttachMode : uint8_t
    {
        None,        // standalone, uses own control points
        Centerline,  // follow source centerline
        LeftEdge,    // snap to source left road edge
        RightEdge,   // snap to source right road edge
        LeftOuter,   // outer edge of source left sidewalk, falls back to LeftEdge
        RightOuter,  // outer edge of source right sidewalk, falls back to RightEdge
        Max
    };

    // a single sampled frame along the spline, in the entity local space
    struct SplineFrame
    {
        math::Vector3 position;
        math::Vector3 tangent;
        math::Vector3 right;
        math::Vector3 up;
        float t        = 0.0f;
        float distance = 0.0f;

        // how far the deck sits above the ground at each edge, drives the embankment skirt
        float fill_left  = 0.0f;
        float fill_right = 0.0f;
    };

    // one point of the finished road deck in world space, this is what the terrain grades itself to
    struct SplineCarveSample
    {
        math::Vector3 position;
        float half_width = 0.0f;
    };

    class Spline : public Component
    {
    public:
        Spline(Entity* entity);
        ~Spline();

        // lifecycle
        void Tick() override;

        // serialization
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        // scripting
        static void RegisterForScripting(sol::state_view state);
        sol::reference AsLua(sol::state_view state) override;

        // evaluation - t is normalized [0, 1] across the entire spline
        math::Vector3 GetPoint(float t) const;
        math::Vector3 GetTangent(float t) const;
        float GetLength(uint32_t samples_per_span = 10) const;
        float GetTAtDistance(float distance, uint32_t samples_per_span = 10) const;

        // control point management (children of the owning entity)
        uint32_t GetControlPointCount() const;
        std::vector<math::Vector3> GetControlPoints() const;
        // handle position on the road deck, stored y is left alone
        static math::Vector3 GetEditorHandlePosition(Entity* entity);
        void AddControlPoint(const math::Vector3& local_position = math::Vector3::Zero);
        void RemoveLastControlPoint();

        // mesh generation - extrudes the current profile along the spline
        void GenerateRoadMesh();
        void ClearRoadMesh();
        bool HasRoadMesh() const { return m_mesh != nullptr; }

        // mesh generation toggle
        bool GetMeshEnabled() const       { return m_mesh_enabled; }
        void SetMeshEnabled(bool enabled) { m_mesh_enabled = enabled; }

        // instanced mesh placement along the spline
        void SpawnInstances();
        void ClearInstances();
        bool HasSpawnedInstances() const;

        // spline properties
        bool GetClosedLoop() const              { return m_closed_loop; }
        void SetClosedLoop(bool closed)         { m_closed_loop = closed; }
        uint32_t GetResolution() const          { return m_resolution; }
        void SetResolution(uint32_t resolution) { m_resolution = resolution; }
        float GetRoadWidth() const              { return m_road_width; }
        void SetRoadWidth(float width)          { m_road_width = width; }

        // profile properties
        SplineProfile GetProfile() const              { return m_profile; }
        void SetProfile(SplineProfile profile)        { m_profile = profile; }
        float GetHeight() const                       { return m_height; }
        void SetHeight(float height)                  { m_height = height; }
        float GetThickness() const                    { return m_thickness; }
        void SetThickness(float thickness)            { m_thickness = thickness; }
        uint32_t GetTubeSides() const                 { return m_tube_sides; }
        void SetTubeSides(uint32_t sides)             { m_tube_sides = sides; }

        // width variation (interpolated from start to end along the spline)
        float GetRoadWidthEnd() const                 { return m_road_width_end; }
        void SetRoadWidthEnd(float width)             { m_road_width_end = width; }

        // uv tiling
        float GetUvTilingU() const                    { return m_uv_tiling_u; }
        void SetUvTilingU(float tiling)               { m_uv_tiling_u = tiling; }
        float GetUvTilingV() const                    { return m_uv_tiling_v; }
        void SetUvTilingV(float tiling)               { m_uv_tiling_v = tiling; }

        // sidewalk/curb (road profile only)
        bool GetSidewalkEnabled() const               { return m_sidewalk_enabled; }
        void SetSidewalkEnabled(bool enabled)         { m_sidewalk_enabled = enabled; }
        float GetSidewalkWidth() const                { return m_sidewalk_width; }
        void SetSidewalkWidth(float width)            { m_sidewalk_width = width; }
        float GetCurbHeight() const                   { return m_curb_height; }
        void SetCurbHeight(float height)              { m_curb_height = height; }

        // terrain conforming
        bool GetConformToTerrain() const              { return m_conform_to_terrain; }
        void SetConformToTerrain(bool conform)        { m_conform_to_terrain = conform; }
        float GetTerrainOffset() const                { return m_terrain_offset; }
        void SetTerrainOffset(float offset)           { m_terrain_offset = offset; }

        // grade limiting, keeps the longitudinal slope drivable instead of tracking the terrain
        bool GetGradeLimitEnabled() const             { return m_grade_limit_enabled; }
        void SetGradeLimitEnabled(bool enabled)       { m_grade_limit_enabled = enabled; }
        float GetMaxGradeDegrees() const              { return m_max_grade_degrees; }
        void SetMaxGradeDegrees(float degrees)        { m_max_grade_degrees = degrees; }
        float GetMaxCut() const                       { return m_max_cut; }
        void SetMaxCut(float meters)                  { m_max_cut = meters; }
        float GetGradeSmoothing() const               { return m_grade_smoothing; }
        void SetGradeSmoothing(float strength)        { m_grade_smoothing = strength; }
        float GetSmoothingLength() const              { return m_smoothing_length; }
        void SetSmoothingLength(float meters)         { m_smoothing_length = meters; }

        // terrain carving, the ground is graded to meet the road instead of the road chasing the ground
        bool GetCarveTerrain() const                  { return m_carve_terrain; }
        void SetCarveTerrain(bool carve)              { m_carve_terrain = carve; }
        float GetCarveBedDrop() const                 { return m_carve_bed_drop; }
        void SetCarveBedDrop(float meters)            { m_carve_bed_drop = meters; }
        float GetCarveFillSlopeDegrees() const        { return m_carve_fill_slope_degrees; }
        void SetCarveFillSlopeDegrees(float degrees)  { m_carve_fill_slope_degrees = degrees; }
        float GetCarveCutSlopeDegrees() const         { return m_carve_cut_slope_degrees; }
        void SetCarveCutSlopeDegrees(float degrees)   { m_carve_cut_slope_degrees = degrees; }
        float GetCarveMaxShoulder() const             { return m_carve_max_shoulder; }
        void SetCarveMaxShoulder(float meters)        { m_carve_max_shoulder = meters; }

        // finished deck in world space, cached on the last mesh build, empty when the road cannot carve
        const std::vector<SplineCarveSample>& GetCarveSamples() const { return m_carve_samples; }
        bool CarvesTerrain() const;

        // embankment skirt, closes the gap between a raised deck and the ground
        bool GetEmbankmentEnabled() const             { return m_embankment_enabled; }
        void SetEmbankmentEnabled(bool enabled)       { m_embankment_enabled = enabled; }
        float GetEmbankmentSlopeDegrees() const       { return m_embankment_slope_degrees; }
        void SetEmbankmentSlopeDegrees(float degrees) { m_embankment_slope_degrees = degrees; }
        float GetEmbankmentMaxHeight() const          { return m_embankment_max_height; }
        void SetEmbankmentMaxHeight(float meters)     { m_embankment_max_height = meters; }

        // instancing properties
        float GetInstanceSpacing() const                        { return m_instance_spacing; }
        void SetInstanceSpacing(float spacing)                  { m_instance_spacing = spacing; }
        bool GetAlignInstancesToSpline() const                  { return m_align_instances_to_spline; }
        void SetAlignInstancesToSpline(bool align)              { m_align_instances_to_spline = align; }
        const std::string& GetInstanceMeshPath() const          { return m_instance_mesh_path; }
        void SetInstanceMeshPath(const std::string& path)       { m_instance_mesh_path = path; }

        // template entity to clone for each instance (0 = use built-in cylinder)
        uint64_t GetInstanceTemplateId() const                  { return m_instance_template_id; }
        void SetInstanceTemplateId(uint64_t id)                 { m_instance_template_id = id; }

        // perpendicular offset from the spline centerline (along the right vector)
        float GetInstanceLateralOffset() const                  { return m_instance_lateral_offset; }
        void SetInstanceLateralOffset(float offset)             { m_instance_lateral_offset = offset; }

        // attachment to another spline (the source)
        uint64_t GetSourceSplineEntityId() const                { return m_source_spline_entity_id; }
        void SetSourceSplineEntityId(uint64_t id);
        Entity* GetSourceSplineEntity() const                   { return m_source_spline_entity; }
        SplineAttachMode GetAttachMode() const                  { return m_attach_mode; }
        void SetAttachMode(SplineAttachMode mode)               { m_attach_mode = mode; }
        float GetAttachLateralOffset() const                    { return m_attach_lateral_offset; }
        void SetAttachLateralOffset(float offset)               { m_attach_lateral_offset = offset; }
        float GetAttachVerticalOffset() const                   { return m_attach_vertical_offset; }
        void SetAttachVerticalOffset(float offset)              { m_attach_vertical_offset = offset; }
        bool GetAttachInheritClosedLoop() const                 { return m_attach_inherit_closed_loop; }
        void SetAttachInheritClosedLoop(bool inherit)           { m_attach_inherit_closed_loop = inherit; }
        uint32_t GetAttachSampleCount() const                   { return m_attach_sample_count; }
        void SetAttachSampleCount(uint32_t count)               { m_attach_sample_count = count; }
        bool IsAttached() const                                 { return m_attach_mode != SplineAttachMode::None && m_source_spline_entity_id != 0; }

        // also spawn a mirrored instance on the opposite side
        bool GetInstanceMirror() const                          { return m_instance_mirror; }
        void SetInstanceMirror(bool mirror)                     { m_instance_mirror = mirror; }

        // orient instances so their local +z faces the spline centerline (overrides align_to_spline)
        bool GetInstanceFaceInward() const                      { return m_instance_face_inward; }
        void SetInstanceFaceInward(bool face_inward)            { m_instance_face_inward = face_inward; }

        // procedural placement randomization
        float GetInstanceRandomOffset() const                   { return m_instance_random_offset; }
        void SetInstanceRandomOffset(float offset)              { m_instance_random_offset = offset; }
        float GetInstanceRandomScaleMin() const                 { return m_instance_random_scale_min; }
        void SetInstanceRandomScaleMin(float scale)             { m_instance_random_scale_min = scale; }
        float GetInstanceRandomScaleMax() const                 { return m_instance_random_scale_max; }
        void SetInstanceRandomScaleMax(float scale)             { m_instance_random_scale_max = scale; }
        float GetInstanceRandomYaw() const                      { return m_instance_random_yaw; }
        void SetInstanceRandomYaw(float degrees)                { m_instance_random_yaw = degrees; }

    private:
        // regenerate the mesh after the world finishes loading
        void OnWorldLoaded();

        // gather control point positions local to the spline entity
        std::vector<math::Vector3> GetControlPointsLocal() const;

        // resolve the current profile into a set of 2d cross-section points (in right-up plane)
        std::vector<math::Vector2> GetProfilePoints() const;
        std::vector<math::Vector2> GetProfilePointsForWidth(float width) const;

        // cross-section for one frame, extends down to the ground when the deck is raised
        std::vector<math::Vector2> GetProfileForFrame(float width, float fill_left, float fill_right) const;

        // whether the current settings add an embankment skirt to the cross-section
        bool UsesEmbankment() const;

        // keep the finished deck in world space so the terrain can grade itself to it
        void CacheCarveSamples(const std::vector<SplineFrame>& frames);

        // whether the current profile forms a closed loop cross-section (e.g. tube)
        bool IsProfileClosed() const;

        // produce a dense list of frames in this entity local space, either from
        // own control points or by sampling the attached source spline
        std::vector<SplineFrame> SampleFrames(uint32_t samples_per_span) const;

        // generalized mesh extrusion using a precomputed list of frames
        void GenerateMesh(const std::vector<SplineFrame>& frames, const std::vector<math::Vector2>& profile_points, bool close_profile);

        // resolve the runtime source spline entity pointer from the stored id
        void ResolveSourceSplineEntity();

        // hash of the source spline state used to detect changes
        uint64_t ComputeSourceHash() const;

        // capture current property/control point state to compare against next tick
        void SnapshotState();

        // catmull-rom interpolation between four points
        static math::Vector3 CatmullRom(
            const math::Vector3& p0, const math::Vector3& p1,
            const math::Vector3& p2, const math::Vector3& p3,
            float t
        );

        // catmull-rom tangent (first derivative) between four points
        static math::Vector3 CatmullRomTangent(
            const math::Vector3& p0, const math::Vector3& p1,
            const math::Vector3& p2, const math::Vector3& p3,
            float t
        );

        // evaluate spline position from an arbitrary set of control points
        math::Vector3 EvaluatePoint(const std::vector<math::Vector3>& points, float t) const;
        math::Vector3 EvaluateTangent(const std::vector<math::Vector3>& points, float t) const;

        // maps a normalized t to a span index and local t
        void MapToSpan(float t, const std::vector<math::Vector3>& points, uint32_t& span_index, float& local_t) const;

        // spline
        bool m_closed_loop             = false;
        uint32_t m_resolution          = 20;
        float m_road_width             = 8.0f;
        bool m_needs_road_regeneration = false;
        bool m_mesh_enabled            = false;

        // profile
        SplineProfile m_profile  = SplineProfile::Road;
        float m_height           = 3.0f;
        float m_thickness        = 0.3f;
        uint32_t m_tube_sides    = 12;

        // width variation
        float m_road_width_end = 8.0f;

        // uv tiling
        float m_uv_tiling_u = 1.0f;
        float m_uv_tiling_v = 1.0f;

        // sidewalk/curb
        bool m_sidewalk_enabled  = false;
        float m_sidewalk_width   = 2.0f;
        float m_curb_height      = 0.15f;

        // terrain conforming
        bool m_conform_to_terrain = false;
        float m_terrain_offset    = 0.25f;

        // grade limiting
        bool m_grade_limit_enabled = true;
        float m_max_grade_degrees  = 8.0f;
        float m_max_cut            = 20.0f;
        float m_grade_smoothing    = 0.9f;
        // arc length the elevation profile is averaged over, this is what stops the rollercoaster
        float m_smoothing_length   = 160.0f;

        // terrain carving
        bool m_carve_terrain              = true;
        float m_carve_bed_drop            = 0.15f;
        float m_carve_fill_slope_degrees  = 33.0f;
        float m_carve_cut_slope_degrees   = 45.0f;
        float m_carve_max_shoulder        = 60.0f;
        std::vector<SplineCarveSample> m_carve_samples;
        // remembered so the destructor can release the carve without touching the dying entity
        uint64_t m_carve_entity_id        = 0;

        // embankment, steeper than the terrain fill slope so it hides inside the carved bank
        bool m_embankment_enabled        = true;
        float m_embankment_slope_degrees = 50.0f;
        float m_embankment_max_height    = 8.0f;

        // attachment
        uint64_t m_source_spline_entity_id    = 0;
        Entity* m_source_spline_entity        = nullptr;
        SplineAttachMode m_attach_mode        = SplineAttachMode::None;
        float m_attach_lateral_offset         = 0.0f;
        float m_attach_vertical_offset        = 0.0f;
        bool m_attach_inherit_closed_loop     = true;
        uint32_t m_attach_sample_count        = 0;

        // instancing
        float m_instance_spacing              = 5.0f;
        bool m_align_instances_to_spline      = true;
        std::string m_instance_mesh_path;
        uint64_t m_instance_template_id       = 0;
        float m_instance_lateral_offset       = 0.0f;
        bool m_instance_mirror                = false;
        bool m_instance_face_inward           = false;
        float m_instance_random_offset        = 0.0f;
        float m_instance_random_scale_min     = 1.0f;
        float m_instance_random_scale_max     = 1.0f;
        float m_instance_random_yaw           = 0.0f;

        // material name to restore after mesh regeneration
        std::string m_saved_material_name;

        // generated mesh
        std::shared_ptr<Mesh> m_mesh;

        // event subscription handle for WorldLoaded
        subscription_handle m_world_loaded_handle = 0;

        // snapshot of previous state for auto-regeneration
        bool m_prev_closed_loop                         = false;
        uint32_t m_prev_resolution                      = 0;
        float m_prev_road_width                         = 0.0f;
        float m_prev_road_width_end                     = 0.0f;
        SplineProfile m_prev_profile                    = SplineProfile::Road;
        float m_prev_height                             = 0.0f;
        float m_prev_thickness                          = 0.0f;
        uint32_t m_prev_tube_sides                      = 0;
        float m_prev_uv_tiling_u                        = 0.0f;
        float m_prev_uv_tiling_v                        = 0.0f;
        bool m_prev_sidewalk_enabled                    = false;
        float m_prev_sidewalk_width                     = 0.0f;
        float m_prev_curb_height                        = 0.0f;
        bool m_prev_conform_to_terrain                  = false;
        float m_prev_terrain_offset                     = 0.0f;
        bool m_prev_grade_limit_enabled                 = true;
        float m_prev_max_grade_degrees                  = 0.0f;
        float m_prev_max_cut                            = 0.0f;
        float m_prev_grade_smoothing                    = 0.0f;
        float m_prev_smoothing_length                   = 0.0f;
        bool m_prev_embankment_enabled                  = true;
        float m_prev_embankment_slope_degrees           = 0.0f;
        float m_prev_embankment_max_height              = 0.0f;
        bool m_prev_carve_terrain                       = true;
        float m_prev_carve_bed_drop                     = 0.0f;
        float m_prev_carve_fill_slope_degrees           = 0.0f;
        float m_prev_carve_cut_slope_degrees            = 0.0f;
        float m_prev_carve_max_shoulder                 = 0.0f;
        std::vector<math::Vector3> m_prev_control_points;
        SplineAttachMode m_prev_attach_mode             = SplineAttachMode::None;
        uint64_t m_prev_source_spline_entity_id         = 0;
        float m_prev_attach_lateral_offset              = 0.0f;
        float m_prev_attach_vertical_offset             = 0.0f;
        bool m_prev_attach_inherit_closed_loop          = true;
        uint32_t m_prev_attach_sample_count             = 0;
        uint64_t m_prev_source_hash                     = 0;
        math::Vector3 m_prev_world_position             = math::Vector3::Zero;
        math::Quaternion m_prev_world_rotation          = math::Quaternion::Identity;
        math::Vector3 m_prev_world_scale                = math::Vector3::One;
    };
}
