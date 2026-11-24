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
    inline TConsoleVar<float> geometry_error_pixels("r.geometry_error_pixels", 1.5f, "Maximum projected geometric error in render pixels");

    // Coverage proxies approximate a group of textured leaves. Keep fine cards active
    // across near and mid distances so silhouettes stay crisp before the impostor handoff.
    inline float GeometryErrorPixels(Render* render)
    {
        const float pixels = std::max(0.1f, geometry_error_pixels.GetValue());
        return (render->GetMesh()->GetFlags() & static_cast<uint32_t>(MeshFlags::FoliageCardClusters)) &&
            render->GetSubMeshIndex() == 1 ? 1.15f * pixels : pixels;
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
            const float inst_density = scale_bound / depth;
            bool any_needs_refinement = false;
            for (const auto& cluster : clusters)
            {
                if (!cluster.refinements.empty() && cluster.error * inst_density > 1.0f)
                {
                    any_needs_refinement = true;
                    break;
                }
            }
            if (any_needs_refinement)
            {
                densities.push_back(inst_density);
            }
            return;
        }
        densities.resize(clusters.size());
        bool any_needs_refinement = false;
        for (size_t i = 0; i < clusters.size(); ++i)
        {
            densities[i] = 0.0f;
            if (clusters[i].refinements.empty()) continue;
            const auto world = clusters[i].bounds * base.transform;
            if (camera->IsInViewFrustum(world))
            {
                const float d = pixel_scale / nearest_depth(world);
                densities[i] = d;
                if (clusters[i].error * d > 1.0f)
                {
                    any_needs_refinement = true;
                }
            }
        }
        if (!any_needs_refinement)
        {
            densities.clear();
        }
    }

    struct ExpandedClusterPlan
    {
        struct Range
        {
            const GeometryCluster* first = nullptr;
            math::BoundingBox bounds;
            uint32_t meshlet_count = 0;
            uint32_t page_offset = 0;
            uint32_t page_count = 0;
        };
        struct Level
        {
            ResidentGeometryPage page;
            float error = 0.0f;
        };
        std::vector<Range> ranges;
        std::vector<Level> levels;
        std::vector<GeometryBuffer::GeometryPageRequest> requests;
    };

    inline void PrepareExpandedClusters(Render* render, const std::vector<float>& densities, ExpandedClusterPlan& out)
    {
        out.ranges.clear();
        out.levels.clear();
        out.requests.clear();
        if (densities.empty())
        {
            return;
        }
        const auto& clusters = render->GetMesh()->GetSubMesh(render->GetSubMeshIndex()).clusters;
        const bool instanced = render->HasInstancing();
        // Native directories validate at most 16 refinements. A generated
        // page starts with 512 triangles and halves at each level.
        ResidentGeometryPage pages[16];
        float errors[16];
        for (size_t i = 0; i < clusters.size(); ++i)
        {
            const auto& cluster = clusters[i];
            const float density = instanced ? densities.front() : densities[i];
            uint32_t count = 0;
            if (!cluster.refinements.empty() && cluster.error * density > 1.0f)
            {
                if (instanced)
                {
                    count = GeometryBuffer::SelectInstancePagesParallel(cluster, density, pages, errors, out.requests);
                }
                else if (GeometryBuffer::SelectPageParallel(cluster, density, pages[0], out.requests))
                {
                    count = 1;
                    errors[0] = 0;
                }
            }
            if (out.levels.empty() && count == 0)
            {
                // Defer building the initial unrefined range until a resident refinement actually hits.
                continue;
            }
            if (out.levels.empty() && i > 0)
            {
                // Materialize the skipped coarse prefix [0, i) once on the first resident refinement.
                math::BoundingBox prefix_bounds = clusters[0].bounds;
                uint32_t prefix_meshlets = clusters[0].meshlet_count;
                for (size_t k = 1; k < i; ++k)
                {
                    prefix_meshlets += clusters[k].meshlet_count;
                    prefix_bounds.Merge(clusters[k].bounds);
                }
                out.ranges.push_back({&clusters[0], prefix_bounds, prefix_meshlets, 0u, 0u});
            }
            if (!count && !out.ranges.empty() && !out.ranges.back().page_count)
            {
                out.ranges.back().meshlet_count += cluster.meshlet_count;
                out.ranges.back().bounds.Merge(cluster.bounds);
            }
            else
            {
                out.ranges.push_back({&cluster, cluster.bounds, cluster.meshlet_count, static_cast<uint32_t>(out.levels.size()), count});
            }
            for (uint32_t j = count; j > 0; --j)
            {
                out.levels.push_back({pages[j - 1], errors[j - 1]});
            }
        }
    }

    struct ExpandedCommitSlot
    {
        uint32_t cluster_draw_base   = 0;
        uint32_t cluster_task_base   = 0;
        uint32_t cluster_extra_tasks = 0;
        uint32_t proxy_draw          = UINT32_MAX;
        bool     commit_clusters     = false;
        bool     attach_proxy        = false;
    };

    inline void PlanExpandedCommit(Render* render, uint32_t first_draw, uint32_t first_task,
        uint32_t instance_count, uint32_t capacity, uint64_t& instance_budget, uint64_t& meshlet_budget,
        const ExpandedClusterPlan& plan, ExpandedCommitSlot& slot)
    {
        slot = {};
        const bool instanced = render->HasInstancing();
        const uint32_t batches = (instance_count + INSTANCE_CULL_BATCH_SIZE - 1) / INSTANCE_CULL_BATCH_SIZE;
        if (!plan.levels.empty())
        {
            const uint32_t extra_draws = static_cast<uint32_t>((plan.ranges.size() - 1) + (instanced ? plan.levels.size() : 0));
            const uint64_t extra_tasks = (plan.ranges.size() - 1) * uint64_t(batches);
            if (extra_draws <= capacity - m_indirect_draw_count && extra_tasks + m_cull_task_count <= renderer_max_cull_tasks)
            {
                slot.commit_clusters     = true;
                slot.cluster_draw_base   = m_indirect_draw_count;
                slot.cluster_task_base   = m_cull_task_count;
                slot.cluster_extra_tasks = static_cast<uint32_t>(extra_tasks);
                m_indirect_draw_count   += extra_draws;
                m_cull_task_count       += static_cast<uint32_t>(extra_tasks);
                instance_budget         += uint64_t(plan.ranges.size() - 1) * instance_count;
                for (size_t c = 0; c < plan.ranges.size(); ++c)
                {
                    const ExpandedClusterPlan::Range& range = plan.ranges[c];
                    uint32_t max_meshlets = range.meshlet_count;
                    for (uint32_t j = 0; j < range.page_count; ++j)
                    {
                        max_meshlets = std::max(max_meshlets, plan.levels[range.page_offset + j].page.meshlet_count);
                    }
                    meshlet_budget += uint64_t(instance_count) * (max_meshlets - range.meshlet_count);
                }
            }
        }
        if (instanced && m_indirect_draw_count < capacity)
        {
            Mesh* mesh = render->GetMesh();
            const bool tree = (mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::FoliageCardClusters)) != 0;
            const bool bark_covered_by_impostor = tree && render->GetSubMeshIndex() == 0 &&
                mesh->GetSubMeshCount() > 1 && mesh->GetSubMesh(1).parent_page && mesh->GetSubMesh(1).parent_page->baked_rgba;
            const auto& proxy = bark_covered_by_impostor ? mesh->GetSubMesh(1).parent_page : mesh->GetSubMesh(render->GetSubMeshIndex()).parent_page;
            if (proxy)
            {
                const uint32_t vertices = proxy->uses_root_vertices ? render->GetVertexOffset() : proxy->vertices;
                if (bark_covered_by_impostor || GeometryBuffer::IsResident(vertices + proxy->vertex_count, proxy->indices + proxy->index_count))
                {
                    slot.attach_proxy = true;
                    slot.proxy_draw   = m_indirect_draw_count++;
                }
            }
        }
        (void)first_draw;
        (void)first_task;
    }

    inline void CommitExpandedClustersAt(Render* render, uint32_t first_draw, uint32_t first_task,
        uint32_t instance_count, const ExpandedClusterPlan& plan, const ExpandedCommitSlot& slot,
        uint32_t base_canopy_draw = 0u, uint32_t extra_canopy_draw = 0u,
        const GeometryParentPage* canopy_proxy = nullptr, float diameter_pixels = 0.0f)
    {
        if (!slot.commit_clusters)
        {
            return;
        }
        const auto base = m_indirect_draw_data[first_draw];
        const bool instanced = render->HasInstancing();
        const uint32_t batches = (instance_count + INSTANCE_CULL_BATCH_SIZE - 1) / INSTANCE_CULL_BATCH_SIZE;
        const float error_pixels = GeometryErrorPixels(render);
        uint32_t draw_cursor = slot.cluster_draw_base;
        uint32_t task_cursor = slot.cluster_task_base;
        for (size_t c = 0; c < plan.ranges.size(); ++c)
        {
            const ExpandedClusterPlan::Range& range = plan.ranges[c];
            const GeometryCluster& cluster = *range.first;
            const uint32_t draw = c == 0 ? first_draw : draw_cursor++;
            auto& coarse = m_indirect_draw_data[draw];
            if (c != 0)
            {
                coarse = base;
                m_indirect_renders[draw] = render;
            }
            coarse.geometry_meshlet_offset += cluster.first_meshlet;
            coarse.geometry_meshlet_count = range.meshlet_count;
            coarse.cluster_center = range.bounds.GetCenter();
            coarse.cluster_radius = range.bounds.GetExtents().Length();
            coarse.cluster_error = cluster.error / error_pixels;
            coarse.previous_vertex_offset = 0;
            if (canopy_proxy)
            {
                coarse.impostor_center     = canopy_proxy->center;
                coarse.impostor_radius     = canopy_proxy->radius;
                coarse.impostor_uv_scale.x = diameter_pixels;
            }

            const uint32_t first_level = draw_cursor;
            for (uint32_t j = 0; j < range.page_count; ++j)
            {
                const auto& level = plan.levels[range.page_offset + j];
                const auto& fine = level.page;
                const uint32_t selected_draw = instanced ? draw_cursor++ : draw;
                auto& selected = m_indirect_draw_data[selected_draw];
                if (instanced)
                {
                    selected = coarse;
                }
                selected.cluster_error = level.error / error_pixels;
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
            const uint32_t task_offset = c == 0 ? first_task : task_cursor;
            if (c != 0) task_cursor += batches;
            const uint32_t task_canopy = c == 0 ? base_canopy_draw : extra_canopy_draw;
            const uint32_t refinement = instanced && range.page_count ? (CULL_TASK_CLUSTER_REFINEMENT |
                ((range.page_count - 1) << CULL_TASK_LEVEL_SHIFT) | first_level) : 0u;
            Sb_CullTask* tasks_dst = m_cull_tasks.data() + task_offset;
            for (uint32_t t = 0; t < batches; ++t)
            {
                auto& task = tasks_dst[t];
                task.draw_index = draw;
                task.canopy_draw = task_canopy;
                task.instance_index = t * INSTANCE_CULL_BATCH_SIZE;
                task.instance_count = std::min(INSTANCE_CULL_BATCH_SIZE, instance_count - task.instance_index);
                task.refinement_draw = refinement;
            }
        }
    }

    inline void CommitPlanWithProxyAt(Render* render, uint32_t first_draw, uint32_t first_task, uint32_t instances,
        const ExpandedClusterPlan& plan, const ExpandedCommitSlot& slot, float diameter_pixels)
    {
        if (!slot.attach_proxy)
        {
            CommitExpandedClustersAt(render, first_draw, first_task, instances, plan, slot);
            return;
        }
        Mesh* mesh = render->GetMesh();
        const bool tree = (mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::FoliageCardClusters)) != 0;
        const bool canopy = tree && render->GetSubMeshIndex() == 1;
        const bool bark_covered_by_impostor = tree && render->GetSubMeshIndex() == 0 &&
            mesh->GetSubMeshCount() > 1 && mesh->GetSubMesh(1).parent_page && mesh->GetSubMesh(1).parent_page->baked_rgba;
        const auto& proxy = bark_covered_by_impostor ? mesh->GetSubMesh(1).parent_page : mesh->GetSubMesh(render->GetSubMeshIndex()).parent_page;
        const uint32_t vertices = proxy->uses_root_vertices ? render->GetVertexOffset() : proxy->vertices;
        const uint32_t index = slot.proxy_draw;
        const uint32_t base_canopy_draw  = bark_covered_by_impostor ? ((index + 1) | 0x80000000u) : (index + 1);
        const uint32_t extra_canopy_draw = (index + 1) | 0x80000000u;

        // Snapshot first_draw BEFORE CommitExpandedClustersAt mutates it for the proxy draw.
        auto& draw = m_indirect_draw_data[index];
        draw = m_indirect_draw_data[first_draw];
        if (slot.commit_clusters)
        {
            CommitExpandedClustersAt(render, first_draw, first_task, instances, plan, slot,
                base_canopy_draw, extra_canopy_draw, canopy ? proxy.get() : nullptr, diameter_pixels);
        }
        else
        {
            if (canopy)
            {
                auto& target = m_indirect_draw_data[first_draw];
                target.impostor_center     = proxy->center;
                target.impostor_radius     = proxy->radius;
                target.impostor_uv_scale.x = diameter_pixels;
            }
            const uint32_t batches = (instances + INSTANCE_CULL_BATCH_SIZE - 1) / INSTANCE_CULL_BATCH_SIZE;
            for (uint32_t i = first_task; i < first_task + batches; ++i)
            {
                m_cull_tasks[i].canopy_draw = base_canopy_draw;
            }
        }
        if (canopy)
        {
            draw.flags |= (1u << 14) | 8u | 16u;
            draw.impostor_layout = GeometryParentPage::frames |
                (proxy->impostor_resolution << 8) |
                (proxy->impostor_layers << 24) |
                (proxy->baked_rgba ? 0x80000000u : 0u);
            draw.impostor_center = proxy->center;
            draw.impostor_radius = proxy->radius;
            draw.impostor_uv_min = proxy->uv_min;
            draw.impostor_uv_scale = proxy->uv_scale;
            draw.impostor_texel_offset = proxy->atlas;
        }
        draw.cluster_center = proxy->center;
        draw.cluster_radius = proxy->radius;
        draw.cluster_error = (canopy || bark_covered_by_impostor) ? diameter_pixels :
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
    }

    inline float GetTreeProxyCanopyPixels()
    {
        static TConsoleVar<float> canopy_pixels("r.geometry_canopy_pixels", 420.0f, "Maximum projected canopy diameter in pixels for the baked virtual parent page");
        return canopy_pixels.GetValue();
    }

    inline void CommitExpandedClusters(Render* render, uint32_t first_draw, uint32_t first_task,
        uint32_t instance_count, uint32_t capacity, uint64_t& instance_budget, uint64_t& meshlet_budget,
        const ExpandedClusterPlan& plan)
    {
        if (plan.levels.empty())
        {
            return;
        }
        const auto base = m_indirect_draw_data[first_draw];
        const bool instanced = render->HasInstancing();
        const uint32_t batches = (instance_count + INSTANCE_CULL_BATCH_SIZE - 1) / INSTANCE_CULL_BATCH_SIZE;
        if (plan.ranges.size() + (instanced ? plan.levels.size() : 0) > capacity - m_indirect_draw_count + 1)
        {
            return;
        }
        const uint64_t extra_tasks = (plan.ranges.size() - 1) * uint64_t(batches);
        if (extra_tasks + m_cull_task_count > renderer_max_cull_tasks)
        {
            return;
        }
        if (m_cull_tasks.size() < m_cull_task_count + extra_tasks)
        {
            m_cull_tasks.resize(static_cast<size_t>(m_cull_task_count + extra_tasks));
        }
        const float error_pixels = GeometryErrorPixels(render);
        for (size_t c = 0; c < plan.ranges.size(); ++c)
        {
            const ExpandedClusterPlan::Range& range = plan.ranges[c];
            const GeometryCluster& cluster = *range.first;
            const uint32_t draw = c == 0 ? first_draw : m_indirect_draw_count++;
            auto& coarse = m_indirect_draw_data[draw];
            if (c != 0)
            {
                coarse = base;
                m_indirect_renders[draw] = render;
            }
            coarse.geometry_meshlet_offset += cluster.first_meshlet;
            coarse.geometry_meshlet_count = range.meshlet_count;
            coarse.cluster_center = range.bounds.GetCenter();
            coarse.cluster_radius = range.bounds.GetExtents().Length();
            coarse.cluster_error = cluster.error / error_pixels;
            coarse.previous_vertex_offset = 0;

            const uint32_t first_level = m_indirect_draw_count;
            uint32_t max_meshlets = range.meshlet_count;
            for (uint32_t j = 0; j < range.page_count; ++j)
            {
                const auto& level = plan.levels[range.page_offset + j];
                const auto& fine = level.page;
                const uint32_t selected_draw = instanced ? m_indirect_draw_count++ : draw;
                auto& selected = m_indirect_draw_data[selected_draw];
                if (instanced)
                {
                    selected = coarse;
                }
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

    inline void ExpandGeometryClusters(Render* render, uint32_t first_draw, uint32_t first_task,
        uint32_t instance_count, uint32_t capacity, uint64_t& instance_budget, uint64_t& meshlet_budget,
        const std::vector<float>& densities)
    {
        static ExpandedClusterPlan plan;
        PrepareExpandedClusters(render, densities, plan);
        if (!plan.requests.empty())
        {
            GeometryBuffer::MergePageRequests(plan.requests);
        }
        CommitExpandedClusters(render, first_draw, first_task, instance_count, capacity, instance_budget, meshlet_budget, plan);
    }
    inline void AttachTreeProxyDraw(Render* render, uint32_t first_draw, uint32_t first_task, uint32_t instances,
        uint32_t first_extra, uint32_t capacity)
    {
        const float diameter_pixels = GetTreeProxyCanopyPixels();
        Mesh* mesh = render->GetMesh();
        const bool tree = (mesh->GetFlags() & static_cast<uint32_t>(MeshFlags::FoliageCardClusters)) != 0;
        const bool canopy = tree && render->GetSubMeshIndex() == 1;
        const bool bark_covered_by_impostor = tree && render->GetSubMeshIndex() == 0 &&
            mesh->GetSubMeshCount() > 1 && mesh->GetSubMesh(1).parent_page && mesh->GetSubMesh(1).parent_page->baked_rgba;
        const auto& proxy = bark_covered_by_impostor ? mesh->GetSubMesh(1).parent_page : mesh->GetSubMesh(render->GetSubMeshIndex()).parent_page;
        if (!proxy || !render->HasInstancing() || m_indirect_draw_count >= capacity) return;
        const uint32_t vertices = proxy->uses_root_vertices ? render->GetVertexOffset() : proxy->vertices;
        if (!bark_covered_by_impostor && !GeometryBuffer::IsResident(vertices + proxy->vertex_count, proxy->indices + proxy->index_count)) return;
        const uint32_t index = m_indirect_draw_count++;
        auto& draw = m_indirect_draw_data[index];
        draw = m_indirect_draw_data[first_draw];
        if (canopy)
        {
            draw.flags |= (1u << 14) | 8u | 16u;
            draw.impostor_layout = GeometryParentPage::frames |
                (proxy->impostor_resolution << 8) |
                (proxy->impostor_layers << 24) |
                (proxy->baked_rgba ? 0x80000000u : 0u);
            draw.impostor_center = proxy->center;
            draw.impostor_radius = proxy->radius;
            draw.impostor_uv_min = proxy->uv_min;
            draw.impostor_uv_scale = proxy->uv_scale;
            draw.impostor_texel_offset = proxy->atlas;
        }
        draw.cluster_center = proxy->center;
        draw.cluster_radius = proxy->radius;
        // When the whole-tree impostor already bakes the trunk, cull the bark submesh at the exact same pixel threshold.
        draw.cluster_error = (canopy || bark_covered_by_impostor) ? diameter_pixels :
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
        if (canopy)
        {
            // Tag all 3D canopy draws (and their refinement levels) with the impostor proxy bounds and
            // diameter threshold so depth_prepass can run a complementary screen-door crossfade.
            auto mark_crossfade = [&](uint32_t d_idx)
            {
                auto& target = m_indirect_draw_data[d_idx];
                target.impostor_center     = proxy->center;
                target.impostor_radius     = proxy->radius;
                target.impostor_uv_scale.x = diameter_pixels;
            };
            auto mark_task = [&](const Sb_CullTask& t)
            {
                mark_crossfade(t.draw_index);
                if ((t.refinement_draw & 0xc0000000u) == CULL_TASK_CLUSTER_REFINEMENT)
                {
                    const uint32_t rf_first = t.refinement_draw & CULL_TASK_LEVEL_MASK;
                    const uint32_t rf_count = 1u + ((t.refinement_draw >> CULL_TASK_LEVEL_SHIFT) & 15u);
                    for (uint32_t r = 0; r < rf_count; ++r)
                    {
                        mark_crossfade(rf_first + r);
                    }
                }
            };
            mark_task(m_cull_tasks[first_task]);
            for (uint32_t i = first_extra; i < m_cull_task_count; i += batches)
            {
                mark_task(m_cull_tasks[i]);
            }
        }
        // Setting bit 31 tells instance_cull to drop the instance completely once it passes the proxy threshold.
        const uint32_t base_canopy_draw = bark_covered_by_impostor ? ((index + 1) | 0x80000000u) : (index + 1);
        for (uint32_t i = first_task; i < first_task + batches; ++i) m_cull_tasks[i].canopy_draw = base_canopy_draw;
        for (uint32_t i = first_extra; i < m_cull_task_count; ++i) m_cull_tasks[i].canopy_draw = (index + 1) | 0x80000000u;
    }
}
