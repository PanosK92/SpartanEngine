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
#include "Rendering/Renderer.h"
#include "Volume.h"

#include <algorithm>

#include "Camera.h"
#include "IO/pugixml.hpp"
#include "World/Entity.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    vector<Volume*> VolumeSystem::registered_volumes{};
    vector<Volume*> VolumeSystem::overlapping_volumes{};
    RenderOptionsPool VolumeSystem::mixed_volume_options = RenderOptionsPool(RenderOptionsListType::Component); // volume data accumulation (when overlapped)
    RenderOptionsPool VolumeSystem::blended_options = RenderOptionsPool(RenderOptionsListType::Global); // volume data accumulation (on interpolation)
    bool VolumeSystem::is_transitioning = false;

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

        m_options_pool = RenderOptionsPool(RenderOptionsListType::Component);
    }

    Volume::~Volume()
    {
        VolumeSystem::RemoveVolume(this);
    }

    void Volume::Initialize()
    {
        VolumeSystem::AddVolume(this);
    }

    void Volume::Tick()
    {
        if (m_is_debug_draw_enabled)
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
        m_options_pool = RenderOptionsPool(RenderOptionsListType::Component);
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

        // Unknown shape
        return 1.0f;
    }

    void VolumeSystem::Shutdown()
    {
        overlapping_volumes.clear();
        registered_volumes.clear();
    }

    void VolumeSystem::AddVolume(Volume* volume)
    {
        if (!volume)
            return;

        if (const auto it = ranges::find(registered_volumes, volume); it == registered_volumes.end())
        {
            registered_volumes.push_back(volume);
        }
    }

    void VolumeSystem::RemoveVolume(Volume* volume)
    {
        if (!volume)
            return;

        if (const auto it = ranges::find(registered_volumes, volume); it != registered_volumes.end())
        {
            registered_volumes.erase(ranges::remove(registered_volumes, volume).begin(), registered_volumes.end());
        }
    }

    void VolumeSystem::Tick()
    {
        if (const Camera* camera = World::GetCamera())
        {
            const Vector3 cam_position = camera->GetEntity()->GetPosition();
            UpdateActiveVolumes(cam_position);
            InterpolateOverlappingVolumes(cam_position);
        }
    }

    void VolumeSystem::UpdateActiveVolumes(const Vector3& camera_position)
    {
        for (Volume* volume : registered_volumes)
        {
            const float alpha = volume->ComputeAlpha(camera_position);

            auto volume_it = ranges::find(overlapping_volumes, volume);
            if (alpha > 0.0f && volume_it == overlapping_volumes.end())
            {
                // Camera is within volume's range. Mark it as overlapping
                overlapping_volumes.push_back(volume);
                UpdateMixedRenderOptions();
                is_transitioning = true;
            }
            else if (alpha <= 0.0f && volume_it != overlapping_volumes.end())
            {
                // Camera just got out of volume's range. Cancel overlapping behaviour.
                overlapping_volumes.erase(volume_it);
                UpdateMixedRenderOptions();
            }
        }

        // If we are moving out from any volumes, update back to global values once
        if (overlapping_volumes.empty())
        {
            is_transitioning = false;
            blended_options = Renderer::GetRenderOptionsPoolRef(true);
            UpdateRendererOptions();
        }
    }

    void VolumeSystem::UpdateMixedRenderOptions()
    {
        // Clear previous accumulation for proper reset
        mixed_volume_options = RenderOptionsPool(RenderOptionsListType::Component);

        // Update the render options results for overlapping volumes.
        // Works for single-volume overlapping cases as well.
        // - for floats:          take average result (ex: Fog_v1=50, Fog_v2=100, Fog_Mix=75)
        // - for booleans:        take combination result
        // - for enums:           "newest wins" semantic
        // - for ints:            not implemented or applicable (no options have this type)

        // Frequency table for boolean render options to determine the combination result
        map<Renderer_Option, int> bool_options_frequencies;

        // Simple accumulation containers for floats (so we don't rely on GetOption before it's set)
        map<Renderer_Option, float> float_options_accumulations;
        map<Renderer_Option, int> float_options_counts;

        for (Volume* volume : overlapping_volumes)
        {
            for (auto& [option_key, option_value] : volume->GetOptionsCollection().GetOptions())
            {
                if (holds_alternative<bool>(option_value))
                {
                    bool new_bool_value = get<bool>(option_value);
                    mixed_volume_options.SetOption(option_key, new_bool_value);
                }
                else if (holds_alternative<uint32_t>(option_value))
                {
                    uint32_t new_enum_value = std::get<uint32_t>(option_value);
                    mixed_volume_options.SetOption(option_key, new_enum_value);
                }
                else if (holds_alternative<float>(option_value))
                {
                    const float new_float_value = std::get<float>(option_value);
                    float_options_accumulations[option_key] += new_float_value;
                    float_options_counts[option_key] += 1;
                }
            }
        }

        // Finalize float averages into mix_volume_options
        for (auto& [option_key, float_sum_value] : float_options_accumulations)
        {
            if (const int count = float_options_counts[option_key]; count > 0)
            {
                float float_average_value = float_sum_value / static_cast<float>(count);
                mixed_volume_options.SetOption(option_key, float_average_value);
            }
        }
    }

    void VolumeSystem::InterpolateOverlappingVolumes(const Vector3& camera_position)
    {
        RenderOptionsPool& global_render_options = Renderer::GetRenderOptionsPoolRef(true);

        float total_alpha = 0.0f;
        bool any_volume_transitioning = false;

        map<Renderer_Option, float> accumulator_floats;
        map<Renderer_Option, float> accumulator_weights;

        // Accumulate sums for interpolated float values
        for (Volume* volume : overlapping_volumes)
        {
            const float alpha = volume->ComputeAlpha(camera_position);
            any_volume_transitioning |= (alpha > 0.0f && alpha < 1.0f);
            total_alpha += alpha;

            for (auto& [option_key, option_value] : volume->GetOptionsCollection().GetOptions())
            {
                if (std::holds_alternative<float>(option_value))
                {
                    const float v = std::get<float>(option_value);
                    accumulator_floats[option_key] += v * alpha;
                    accumulator_weights[option_key] += alpha;
                }
            }
        }

        if (overlapping_volumes.empty())
            return;

        // Instantly update for non-float values only once
        for (auto& [option_key, option_value] : mixed_volume_options.GetOptions())
        {
            RenderOptionType blended_value = blended_options.GetOption(option_key);
            if (!std::holds_alternative<float>(option_value) && !RenderOptionsPool::AreVariantsEqual(blended_value, option_value))
            {
                blended_options.SetOption(option_key, option_value);
            }
        }

        // Update to mixed values if not inside any volume's transition zone
        if (!any_volume_transitioning)
        {
            blended_options = mixed_volume_options;
            UpdateRendererOptions();
            is_transitioning = false;
            return;
        }

        // Finish interpolation for float values
        for (auto& [option_key, sum_value] : accumulator_floats)
        {
            const float weight_sum = accumulator_weights[option_key];

            if (weight_sum <= 0.0f)
                continue;

            const float weighted_average = sum_value / weight_sum;
            const float global_v = global_render_options.GetOption<float>(option_key);
            const float t = clamp(total_alpha, 0.0f, 1.0f);

            blended_options.SetOption(option_key, lerp(global_v, weighted_average, t));
        }

        UpdateRendererOptions();
        is_transitioning = true;
    }


    void VolumeSystem::UpdateRendererOptions()
    {
        for (auto& [option_key, option_value] : blended_options.GetOptions())
        {
            if (RenderOptionType existing_value = Renderer::GetOption(option_key); !RenderOptionsPool::AreVariantsEqual(existing_value, option_value))
            {
                Renderer::SetOption(option_key, option_value);
            }
        }
    }
}
