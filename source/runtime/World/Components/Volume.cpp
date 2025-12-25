/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES =================================
#include "pch.h"
#include "IO/pugixml.hpp"

#include <algorithm>

#include "Camera.h"
#include "Rendering/Renderer.h"
#include "World/Entity.h"
#include "Volume.h"

#include "Display/Display.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    unordered_map<Renderer_Option, RenderOptionType> Volume::global_render_options;
    unordered_map<Renderer_Option, const char*> Volume::options_labels;
    unordered_map<Renderer_Option, RenderOptionType> Volume::blended_options; // volume data accumulation (on interpolation)
    unordered_map<Renderer_Option, float> Volume::accumulator_floats {};
    unordered_map<Renderer_Option, float> Volume::accumulator_weights {};
    int Volume::overlapping_count = 0;
    uint64_t Volume::accumulation_frame = UINT64_MAX;
    uint64_t Volume::finalization_frame = UINT64_MAX;

    Volume::Volume(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_volume_shape_type, VolumeType);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_shape_size, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_transition_size, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_debug_draw_enabled, bool);

        m_volume_shape_type = VolumeType::Sphere;
        m_bounding_box = BoundingBox::Unit;
        m_shape_size = default_shape_size;
        m_transition_size = default_transition_size;
        m_is_debug_draw_enabled = true;

        m_options.clear();
        m_options[Renderer_Option::WhitePoint] = RenderOptionType { 350.0f };
        m_options[Renderer_Option::Tonemapping] = RenderOptionType { static_cast<float>(Renderer_Tonemapping::Max) };
        m_options[Renderer_Option::Bloom] = RenderOptionType { 1.0f };
        m_options[Renderer_Option::MotionBlur] = RenderOptionType { true };
        m_options[Renderer_Option::DepthOfField] = RenderOptionType { true };
        m_options[Renderer_Option::ScreenSpaceAmbientOcclusion] = RenderOptionType { true };
        m_options[Renderer_Option::ScreenSpaceReflections] = RenderOptionType { true };
        m_options[Renderer_Option::RayTracedReflections] = RenderOptionType { false };
        m_options[Renderer_Option::Anisotropy] = RenderOptionType { 16.0f };
        m_options[Renderer_Option::Sharpness] = RenderOptionType { 0.0f };
        m_options[Renderer_Option::Fog] = RenderOptionType { 1.0f };
        m_options[Renderer_Option::AntiAliasing_Upsampling] = RenderOptionType { static_cast<uint32_t>(Renderer_AntiAliasing_Upsampling::AA_Fsr_Upscale_Fsr) };
        m_options[Renderer_Option::ResolutionScale] = RenderOptionType { 1.0f };
        m_options[Renderer_Option::VariableRateShading] = RenderOptionType { false };
        m_options[Renderer_Option::Vsync] = RenderOptionType { false };
        m_options[Renderer_Option::TransformHandle] = RenderOptionType { true };
        m_options[Renderer_Option::SelectionOutline] = RenderOptionType { false };
        m_options[Renderer_Option::Grid] = RenderOptionType { false };
        m_options[Renderer_Option::Lights] = RenderOptionType { true };
        m_options[Renderer_Option::AudioSources] = RenderOptionType { true };
        m_options[Renderer_Option::Physics] = RenderOptionType { false };
        m_options[Renderer_Option::PerformanceMetrics] = RenderOptionType { true };
        m_options[Renderer_Option::Gamma] = RenderOptionType { Display::GetGamma() };
        m_options[Renderer_Option::AutoExposureAdaptationSpeed] = RenderOptionType { 0.5f };
    }

    Volume::~Volume()
    {

    }

    const char* Volume::RenderOptionToString(const Renderer_Option option)
    {
        // Options labelled the same way as the Render Options editor view
        switch (option)
        {
            case Renderer_Option::Aabb:                        return "AABBs";
            case Renderer_Option::PickingRay:                  return "Picking ray";
            case Renderer_Option::Grid:                        return "Grid";
            case Renderer_Option::TransformHandle:             return "Transform handles";
            case Renderer_Option::SelectionOutline:            return "Selection outline";
            case Renderer_Option::Lights:                      return "Lights";
            case Renderer_Option::AudioSources:                return "Audio sources";
            case Renderer_Option::PerformanceMetrics:          return "Show performance Metrics";
            case Renderer_Option::Physics:                     return "Physics";
            case Renderer_Option::Wireframe:                   return "Wireframe";
            case Renderer_Option::Bloom:                       return "Bloom";
            case Renderer_Option::Fog:                         return "Fog";
            case Renderer_Option::ScreenSpaceAmbientOcclusion: return "Ambient Occlusion (SSR)";
            case Renderer_Option::ScreenSpaceReflections:      return "Reflections (SSR)";
            case Renderer_Option::MotionBlur:                  return "Motion blur";
            case Renderer_Option::DepthOfField:                return "Depth of field";
            case Renderer_Option::FilmGrain:                   return "Film grain";
            case Renderer_Option::ChromaticAberration:         return "Chromatic aberration";
            case Renderer_Option::Anisotropy:                  return "Anisotropy";
            case Renderer_Option::WhitePoint:                  return "White point (nits)";
            case Renderer_Option::Tonemapping:                 return "Tone Mapping";
            case Renderer_Option::AntiAliasing_Upsampling:     return "Anti-Aliasing Upsampling";
            case Renderer_Option::Sharpness:                   return "Sharpness";
            case Renderer_Option::Hdr:                         return "HDR";
            case Renderer_Option::Gamma:                       return "Gamma";
            case Renderer_Option::Vsync:                       return "VSync";
            case Renderer_Option::VariableRateShading:         return "Variable rate shading";
            case Renderer_Option::ResolutionScale:             return "Resolution scale";
            case Renderer_Option::DynamicResolution:           return "Dynamic Resolution";
            case Renderer_Option::Dithering:                   return "Dithering";
            case Renderer_Option::Vhs:                         return "VHS";
            case Renderer_Option::OcclusionCulling:            return "Occlusion culling";
            case Renderer_Option::AutoExposureAdaptationSpeed: return "Exposure adaptation speed";
            case Renderer_Option::RayTracedReflections:        return "Reflections (Ray-Traced)";
            default:
            {
                SP_ASSERT_MSG(false, "Renderer_Option not handled");
                return "";
            }
        }
    }

    void Volume::Save(pugi::xml_node& node)
    {
        node.append_attribute("shape_type")      = static_cast<int>(m_volume_shape_type);
        node.append_attribute("shape_size")      = m_shape_size;
        node.append_attribute("transition_size") = m_transition_size;
        node.append_attribute("debug_enabled")   = m_is_debug_draw_enabled;
    }

    void Volume::Load(pugi::xml_node& node)
    {
        m_volume_shape_type = static_cast<VolumeType>(node.attribute("shape_type").as_int(static_cast<int>(VolumeType::Max)));
        m_shape_size = node.attribute("shape_size").as_float();
        m_transition_size = node.attribute("transition_size").as_float();
        m_is_debug_draw_enabled = node.attribute("debug_enabled").as_bool();

        m_bounding_box = BoundingBox::Unit;
    }

    void Volume::PreTick()
    {
        const uint64_t frame = Renderer::GetFrameNumber();

        // Reset data only once per frame
        if (accumulation_frame != frame)
        {
            accumulation_frame = frame;
            accumulator_floats.clear();
            accumulator_weights.clear();
            overlapping_count = 0;

            DecodeRenderOptions();
            blended_options = global_render_options;
        }

        const Camera* camera = World::GetCamera();
        if (!camera)
            return;

        const Vector3 cam_position = camera->GetEntity()->GetPosition();
        const float alpha = ComputeAlpha(cam_position);

        const bool is_overlapping_now = alpha > 0.0f;

        // Override Renderer only when necessary (when the camera has entered at least one volume)
        if (is_overlapping_now)
        {
            ++overlapping_count;
            Renderer::SetOverrideEnabled(true);
        }

        if (!is_overlapping_now)
            return;

        AccumulateVolumeRenderOptions(alpha);
    }

    void Volume::Tick()
    {
        if (m_is_debug_draw_enabled)
        {
            DrawVolume();
        }

        const uint64_t frame = Renderer::GetFrameNumber();

        // Finalize and apply data only once per frame
        if (finalization_frame == frame)
            return;

        finalization_frame = frame;

        if (overlapping_count == 0)
        {
            Renderer::SetOverrideEnabled(false);
            return;
        }

        ApplyBlendedRenderOptions();
    }

    void Volume::DecodeRenderOptions()
    {
        // Get global render options set by the user
        unordered_map<Renderer_Option, float> global_options = Renderer::GetOptions(true);

        // Decode all options to their corresponding value type
        MapFloatToRenderOption<float>(Renderer_Option::WhitePoint, global_options[Renderer_Option::WhitePoint]);
        MapFloatToRenderOption<uint32_t>(Renderer_Option::Tonemapping, global_options[Renderer_Option::Tonemapping]);
        MapFloatToRenderOption<float>(Renderer_Option::Bloom, global_options[Renderer_Option::Bloom]);
        MapFloatToRenderOption<bool>(Renderer_Option::MotionBlur, global_options[Renderer_Option::MotionBlur]);
        MapFloatToRenderOption<bool>(Renderer_Option::DepthOfField, global_options[Renderer_Option::DepthOfField]);
        MapFloatToRenderOption<bool>(Renderer_Option::ScreenSpaceAmbientOcclusion, global_options[Renderer_Option::ScreenSpaceAmbientOcclusion]);
        MapFloatToRenderOption<bool>(Renderer_Option::ScreenSpaceReflections, global_options[Renderer_Option::ScreenSpaceReflections]);
        MapFloatToRenderOption<bool>(Renderer_Option::RayTracedReflections, global_options[Renderer_Option::RayTracedReflections]);
        MapFloatToRenderOption<float>(Renderer_Option::Anisotropy, global_options[Renderer_Option::Anisotropy]);
        MapFloatToRenderOption<float>(Renderer_Option::Sharpness, global_options[Renderer_Option::Sharpness]);
        MapFloatToRenderOption<float>(Renderer_Option::Fog, global_options[Renderer_Option::Fog]);
        MapFloatToRenderOption<uint32_t>(Renderer_Option::AntiAliasing_Upsampling, global_options[Renderer_Option::AntiAliasing_Upsampling]);
        MapFloatToRenderOption<float>(Renderer_Option::ResolutionScale, global_options[Renderer_Option::ResolutionScale]);
        MapFloatToRenderOption<bool>(Renderer_Option::VariableRateShading, global_options[Renderer_Option::VariableRateShading]);
        MapFloatToRenderOption<bool>(Renderer_Option::Vsync, global_options[Renderer_Option::Vsync]);
        MapFloatToRenderOption<bool>(Renderer_Option::TransformHandle, global_options[Renderer_Option::TransformHandle]);
        MapFloatToRenderOption<bool>(Renderer_Option::SelectionOutline, global_options[Renderer_Option::SelectionOutline]);
        MapFloatToRenderOption<bool>(Renderer_Option::Grid, global_options[Renderer_Option::Grid]);
        MapFloatToRenderOption<bool>(Renderer_Option::Lights, global_options[Renderer_Option::Lights]);
        MapFloatToRenderOption<bool>(Renderer_Option::AudioSources, global_options[Renderer_Option::AudioSources]);
        MapFloatToRenderOption<bool>(Renderer_Option::Physics, global_options[Renderer_Option::Physics]);
        MapFloatToRenderOption<bool>(Renderer_Option::PerformanceMetrics, global_options[Renderer_Option::PerformanceMetrics]);
        MapFloatToRenderOption<bool>(Renderer_Option::Dithering, global_options[Renderer_Option::Dithering]);
        MapFloatToRenderOption<bool>(Renderer_Option::Gamma, global_options[Renderer_Option::Gamma]);
        MapFloatToRenderOption<float>(Renderer_Option::AutoExposureAdaptationSpeed, global_options[Renderer_Option::AutoExposureAdaptationSpeed]);
    }

    float Volume::MapRenderOptionToFloat(const RenderOptionType& value)
    {
        return std::visit([](auto&& v) -> float
        {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, bool>)
                return v ? 1.0f : 0.0f;
            else if constexpr (std::is_same_v<T, int>)
                return static_cast<float>(v);
            else if constexpr (std::is_same_v<T, float>)
                return v;
            else if constexpr (std::is_same_v<T, uint32_t>)
                return static_cast<float>(v);
        }, value);
    }

    void Volume::AccumulateVolumeRenderOptions(const float alpha)
    {
        for (auto& [key, value] : m_options)
        {
            if (std::holds_alternative<float>(value))
            {
                const float v = std::get<float>(value);
                accumulator_floats[key]  += v * alpha;
                accumulator_weights[key] += alpha;
            }
            else
            {
                blended_options[key] = value;
            }
        }
    }

    void Volume::ApplyBlendedRenderOptions()
    {
        for (auto& [key, sum] : accumulator_floats)
        {
            const float weight = accumulator_weights[key];
            if (weight <= 0.0f)
                continue;

            const float float_average = sum / weight;
            const float global_float_option = MapRenderOptionToFloat(global_render_options[key]);

            const float t = clamp(weight, 0.0f, 1.0f);

            blended_options[key] = lerp(global_float_option, float_average, t);
        }

        // Apply to renderer only if values have changed
        for (auto& [key, value] : blended_options)
        {
            float new_value = MapRenderOptionToFloat(value);
            if (fabs(Renderer::GetOptions()[key] - new_value) > epsilon)
            {
                Renderer::SetOption(key, new_value);
            }
        }
    }

    void Volume::DrawVolume()
    {
        // For debugging
        if (m_volume_shape_type == VolumeType::Sphere)
        {
            Renderer::DrawSphere(m_entity_ptr->GetPosition(), m_shape_size, 16, Color::standard_yellow);
            Renderer::DrawSphere(m_entity_ptr->GetPosition(), m_shape_size + m_transition_size, 16, Color::standard_renderer_lines);
        }
        else if (m_volume_shape_type == VolumeType::Box)
        {
            const Matrix new_matrix_inner = Matrix(m_entity_ptr->GetPosition(), m_entity_ptr->GetRotation(), m_entity_ptr->GetScale() + m_shape_size);
            Renderer::DrawBox(m_bounding_box * new_matrix_inner, Color::standard_yellow);

            const Matrix new_matrix_outer = Matrix(m_entity_ptr->GetPosition(), m_entity_ptr->GetRotation(), m_entity_ptr->GetScale() + m_shape_size + m_transition_size);
            Renderer::DrawBox(m_bounding_box * new_matrix_outer, Color::standard_renderer_lines);
        }
    }

    float Volume::ComputeAlpha(const Vector3& camera_position) const
    {
        if (m_volume_shape_type == VolumeType::Box)
        {
            const Vector3 distance_absolute = (camera_position - m_entity_ptr->GetPosition()).Abs();

            const Vector3 inner_half_extents = m_bounding_box.GetExtents() + Vector3(m_shape_size / 2.0f);
            const Vector3 outer_half_extents = inner_half_extents + Vector3(m_transition_size);

            const float dist_to_inner = (distance_absolute - inner_half_extents).Max(Vector3::Zero).Length();
            const float dist_to_outer = (distance_absolute - outer_half_extents).Max(Vector3::Zero).Length();

            if (dist_to_inner <= 0.0f)
            {
                return 1.0f;
            }
            if (dist_to_outer > 0.0f)
            {
                return 0.0f;
            }

            return 1.0f - dist_to_inner / m_transition_size;
        }
        if (m_volume_shape_type == VolumeType::Sphere)
        {
            const float distance = Vector3::Distance(camera_position, m_entity_ptr->GetPosition());

            if (distance > m_transition_size + m_shape_size)
            {
                return 0.0f;
            }
            if (distance <= m_shape_size)
            {
                return 1.0f;
            }

            return 1.0f - (distance - m_shape_size) / m_transition_size;
        }

        // Unknown shape. Reset to hard transition.
        return 1.0f;
    }
}
