/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
*/

#pragma once

#include "Renderer_Internal.h"
#include "GeometryBuffer.h"
#include "../geometry/Mesh.h"
#include "../memory/FrameAllocator.h"
#include "../world/components/Camera.h"
#include "../world/components/Render.h"
#include "../world/Entity.h"

namespace spartan::Renderer
{
    inline TConsoleVar<float> geometry_error_pixels("r.geometry_error_pixels", 1.0f, "Maximum projected geometric error in render pixels");

    // Coverage proxies approximate a group of textured leaves. Request their
    // finer pages when the group covers a visible patch of canopy.
    inline float GeometryErrorPixels(Render* render)
    {
        const float pixels = std::max(0.1f, geometry_error_pixels.GetValue());
        return (render->GetMesh()->GetFlags() & static_cast<uint32_t>(MeshFlags::FoliageCardClusters)) &&
            render->GetSubMeshIndex() == 1 ? 12.0f * pixels : pixels;
    }

    inline bool SelectObjectParent(Render* render, Sb_DrawData& draw, Camera* camera,
        const math::Vector3& position, const math::Vector3& forward, float focal_pixels)
    {
        if (!camera || render->HasInstancing()) return false;
        const auto& first = render->GetMesh()->GetSubMesh(render->GetSubMeshIndex()).parent_page;
        if (!first || (render->GetMesh()->GetFlags() & static_cast<uint32_t>(MeshFlags::FoliageCardClusters))) return false;
        const auto& box = render->GetBoundingBox();
        const float depth = camera->GetProjectionType() == Projection_Perspective ?
            std::max(camera->GetNearPlane(), math::Vector3::Dot(box.GetCenter() - position, forward) - box.GetExtents().Length()) : 1.0f;
        const auto scale = draw.transform.GetScale();
        const float projection = std::max({scale.x, scale.y, scale.z}) * focal_pixels;
        const GeometryParentPage* page = nullptr;
        uint32_t vertices = 0;
        for (auto* candidate = first.get(); candidate; candidate = candidate->coarser.get())
        {
            const uint32_t base = candidate->uses_root_vertices ? draw.geometry_vertex_offset : candidate->vertices;
            if (candidate->error * projection <= depth * GeometryErrorPixels(render) && GeometryBuffer::IsResident(base + candidate->vertex_count,
                candidate->indices + candidate->index_count))
            {
                page = candidate;
                vertices = base;
            }
        }
        if (!page) return false;
        draw.geometry_vertex_offset = vertices;
        if (!page->uses_root_vertices) draw.previous_vertex_offset = 0;
        draw.geometry_bounds_min = page->bounds.GetMin();
        draw.geometry_bounds_extent = page->bounds.GetSize();
        draw.geometry_bounds_diag = draw.geometry_bounds_extent.Length();
        draw.geometry_first_index = page->indices;
        draw.geometry_meshlet_offset = page->meshlet;
        draw.geometry_meshlet_count = page->meshlet_count;
        draw.meshlet_vertex_base = page->remaps;
        draw.meshlet_micro_base = page->micro;
        return true;
    }

    inline void MeasureGeometryClusters(Render* render, const Sb_DrawData& base, Camera* camera,
        const math::Vector3& camera_position, const math::Vector3& forward, float focal_pixels, std::vector<float>& densities)
    {
        Mesh* mesh = render->GetMesh();
        const auto& sub = mesh->GetSubMesh(render->GetSubMeshIndex());
        const auto& clusters = sub.clusters;
        densities.clear();
        if (clusters.empty() || sub.HasResidentRootOnly() || !camera)
            return;

        const auto scale = base.transform.GetScale();
        const float pixel_scale = std::max({scale.x, scale.y, scale.z}) * focal_pixels / GeometryErrorPixels(render);
        const math::Vector3 axis(std::abs(forward.x), std::abs(forward.y), std::abs(forward.z));
        const auto nearest_depth = [&](const math::BoundingBox& world)
        {
            return camera->GetProjectionType() == Projection_Perspective ?
                std::max(camera->GetNearPlane(), math::Vector3::Dot(world.GetCenter() - camera_position, forward) -
                    math::Vector3::Dot(axis, world.GetExtents())) : 1.0f;
        };
        const bool instanced = render->HasInstancing();
        if (instanced)
        {
            // Only bound the finest page the batch could use. The GPU walks the
            // resident pages for each instance at its own depth.
            const float scale_bound = render->GetInstanceScaleBound() * pixel_scale;
            float depth = nearest_depth(render->GetBoundingBox());
            const auto& groups = render->GetInstanceBoundsGroups();
            if (depth == camera->GetNearPlane() && !groups.empty())
            {
                // A forest tile can surround the camera even when its nearest visible
                // stand is across the road. Each group already includes wind padding.
                depth = std::numeric_limits<float>::max();
                for (const auto& group : groups)
                {
                    if (camera->IsInViewFrustum(group.bounds))
                        depth = std::min(depth, nearest_depth(group.bounds));
                    if (depth == camera->GetNearPlane()) break;
                }
            }
            densities.push_back(scale_bound / depth);
            return;
        }
        densities.resize(clusters.size());
        for (size_t i = 0; i < clusters.size(); ++i)
        {
            densities[i] = 0.0f;
            if (clusters[i].refinements.empty()) continue;
            const auto world = clusters[i].bounds * base.transform;
            if (camera->IsInViewFrustum(world))
                densities[i] = pixel_scale / nearest_depth(world);
        }
    }

    inline void ExpandGeometryClusters(Render* render, uint32_t first_draw, uint32_t first_task,
        uint32_t instance_count, uint32_t capacity, uint64_t& instance_budget, uint64_t& meshlet_budget,
        const std::vector<float>& densities)
    {
        if (densities.empty()) return;
        const auto& clusters = render->GetMesh()->GetSubMesh(render->GetSubMeshIndex()).clusters;
        const auto base = m_indirect_draw_data[first_draw];
        const bool instanced = render->HasInstancing();
        const uint32_t batches = (instance_count + INSTANCE_CULL_BATCH_SIZE - 1) / INSTANCE_CULL_BATCH_SIZE;
        struct Range
        {
            const GeometryCluster* first;
            math::BoundingBox bounds;
            uint32_t meshlet_count;
            uint32_t page_offset, page_count;
        };
        struct Level { ResidentGeometryPage page; float error; };
        std::pmr::vector<Range> ranges(FrameAllocator::GetResource());
        std::pmr::vector<Level> levels(FrameAllocator::GetResource());
        for (size_t i = 0; i < clusters.size(); ++i)
        {
            const auto& cluster = clusters[i];
            // Native directories validate at most 16 refinements. A generated
            // page starts with 512 triangles and halves at each level.
            ResidentGeometryPage pages[16];
            float errors[16];
            uint32_t count = 0;
            if (instanced)
                count = GeometryBuffer::SelectInstancePages(cluster, densities.front(), pages, errors);
            else if (GeometryBuffer::SelectPage(cluster, densities[i], pages[0]))
            {
                count = 1;
                errors[0] = 0;
            }
            if (!count && !ranges.empty() && !ranges.back().page_count)
            {
                ranges.back().meshlet_count += cluster.meshlet_count;
                ranges.back().bounds.Merge(cluster.bounds);
            }
            else
                ranges.push_back({&cluster, cluster.bounds, cluster.meshlet_count, static_cast<uint32_t>(levels.size()), count});
            for (uint32_t j = count; j > 0; --j)
                levels.push_back({pages[j - 1], errors[j - 1]});
        }
        if (levels.empty() || ranges.size() + (instanced ? levels.size() : 0) > capacity - m_indirect_draw_count + 1)
            return;
        const uint64_t extra_tasks = (ranges.size() - 1) * uint64_t(batches);
        if (extra_tasks + m_cull_task_count > renderer_max_cull_tasks) return;
        if (m_cull_tasks.size() < m_cull_task_count + extra_tasks)
            m_cull_tasks.resize(static_cast<size_t>(m_cull_task_count + extra_tasks));
        for (size_t c = 0; c < ranges.size(); ++c)
        {
            const Range& range = ranges[c];
            const GeometryCluster& cluster = *range.first;
            const float error_pixels = GeometryErrorPixels(render);
            const uint32_t draw = c == 0 ? first_draw : m_indirect_draw_count++;
            auto& coarse = m_indirect_draw_data[draw];
            coarse = base;
            coarse.geometry_meshlet_offset += cluster.first_meshlet;
            coarse.geometry_meshlet_count = range.meshlet_count;
            coarse.cluster_center = range.bounds.GetCenter();
            coarse.cluster_radius = range.bounds.GetExtents().Length();
            coarse.cluster_error = cluster.error / error_pixels;
            coarse.previous_vertex_offset = 0;
            m_indirect_renders[draw] = render;

            const uint32_t first_level = m_indirect_draw_count;
            uint32_t max_meshlets = range.meshlet_count;
            for (uint32_t j = 0; j < range.page_count; ++j)
            {
                const auto& level = levels[range.page_offset + j];
                const auto& fine = level.page;
                const uint32_t selected_draw = instanced ? m_indirect_draw_count++ : draw;
                auto& selected = m_indirect_draw_data[selected_draw];
                selected = coarse;
                selected.cluster_error = level.error / error_pixels;
                max_meshlets = std::max(max_meshlets, fine.meshlet_count);
                selected.geometry_bounds_min = cluster.bounds.GetMin();
                selected.geometry_bounds_extent = cluster.bounds.GetSize();
                selected.geometry_bounds_diag = selected.geometry_bounds_extent.Length();
                selected.geometry_first_index = fine.indices;
                selected.geometry_vertex_offset = fine.vertices;
                selected.geometry_meshlet_offset = fine.meshlets;
                selected.geometry_meshlet_count = fine.meshlet_count;
                selected.meshlet_vertex_base = fine.remaps;
                selected.meshlet_micro_base = fine.micro;
                m_indirect_renders[selected_draw] = render;
            }
            const uint32_t task_offset = c == 0 ? first_task : m_cull_task_count;
            if (c != 0) m_cull_task_count += batches;
            for (uint32_t t = 0; t < batches; ++t)
            {
                auto& task = m_cull_tasks[task_offset + t];
                task.draw_index = draw;
                task.canopy_draw = 0;
                task.instance_index = t * INSTANCE_CULL_BATCH_SIZE;
                task.instance_count = std::min(INSTANCE_CULL_BATCH_SIZE, instance_count - task.instance_index);
                task.refinement_draw = instanced && range.page_count ? (CULL_TASK_CLUSTER_REFINEMENT |
                    ((range.page_count - 1) << CULL_TASK_LEVEL_SHIFT) | first_level) : 0u;
            }
            if (c) instance_budget += instance_count;
            meshlet_budget += uint64_t(instance_count) * (max_meshlets - range.meshlet_count);
        }
    }
    inline void AttachTreeProxyDraw(Render* render, uint32_t first_draw, uint32_t first_task, uint32_t instances,
        uint32_t first_extra, uint32_t capacity)
    {
        static TConsoleVar<float> canopy_pixels("r.geometry_canopy_pixels", 96.0f, "Maximum projected canopy diameter in pixels for the baked virtual parent page");
        const auto& proxy = render->GetMesh()->GetSubMesh(render->GetSubMeshIndex()).parent_page;
        if (!proxy || !render->HasInstancing() || m_indirect_draw_count >= capacity) return;
        const uint32_t vertices = proxy->uses_root_vertices ? render->GetVertexOffset() : proxy->vertices;
        if (!GeometryBuffer::IsResident(vertices + proxy->vertex_count, proxy->indices + proxy->index_count)) return;
        const uint32_t index = m_indirect_draw_count++;
        auto& draw = m_indirect_draw_data[index];
        draw = m_indirect_draw_data[first_draw];
        const bool tree = (render->GetMesh()->GetFlags() & static_cast<uint32_t>(MeshFlags::FoliageCardClusters)) != 0;
        const bool canopy = tree && render->GetSubMeshIndex() == 1;
        if (canopy)
        {
            draw.flags |= (1u << 14) | 8u | 16u;
            draw.impostor_layout = GeometryParentPage::frames | (GeometryParentPage::resolution << 8) | (GeometryParentPage::layers << 24);
            draw.impostor_center = proxy->center;
            draw.impostor_radius = proxy->radius;
            draw.impostor_uv_min = proxy->uv_min;
            draw.impostor_uv_scale = proxy->uv_scale;
            draw.impostor_texel_offset = proxy->atlas;
        }
        draw.cluster_center = proxy->center;
        draw.cluster_radius = proxy->radius;
        const float diameter_pixels = canopy_pixels.GetValue();
        // The trunk parent must also fit its measured shape error into two pixels.
        draw.cluster_error = canopy ? diameter_pixels :
            (tree ? std::min(diameter_pixels, 4.0f * proxy->radius / std::max(proxy->error, 1e-5f))
                  : 2.0f * GeometryErrorPixels(render) * proxy->radius / std::max(proxy->error, 1e-5f));
        draw.geometry_bounds_min = proxy->bounds.GetMin();
        draw.geometry_bounds_extent = proxy->bounds.GetSize();
        draw.geometry_bounds_diag = draw.geometry_bounds_extent.Length();
        draw.geometry_first_index = proxy->indices;
        draw.geometry_vertex_offset = vertices;
        draw.geometry_meshlet_offset = proxy->meshlet;
        draw.geometry_meshlet_count = proxy->meshlet_count;
        draw.meshlet_vertex_base = proxy->remaps;
        draw.meshlet_micro_base = proxy->micro;
        draw.previous_vertex_offset = 0;
        m_indirect_renders[index] = render;
        const uint32_t batches = (instances + INSTANCE_CULL_BATCH_SIZE - 1) / INSTANCE_CULL_BATCH_SIZE;
        for (uint32_t i = first_task; i < first_task + batches; ++i) m_cull_tasks[i].canopy_draw = index + 1;
        for (uint32_t i = first_extra; i < m_cull_task_count; ++i) m_cull_tasks[i].canopy_draw = (index + 1) | 0x80000000u;
    }
}
