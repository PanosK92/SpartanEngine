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

//= INCLUDES ==============================
#include "pch.h"
#include "GameReady.h"
#include "Entity.h"
#include "World.h"
#include "Components/Render.h"
#include "../Geometry/Mesh.h"
#include "../Geometry/GeometryProcessing.h"
#include "../Rendering/Material.h"
#include "../Resource/ResourceCache.h"
#include <algorithm>
#include <filesystem>
#include <unordered_map>
//=========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan::game_ready
{
    namespace
    {
        // a renderer that can be folded into a group, with the transform that carries its geometry from
        // its own space into the root's
        struct Candidate
        {
            Entity* entity     = nullptr;
            Render* render     = nullptr;
            Material* material = nullptr;
            Matrix to_root     = Matrix::Identity;
        };

        struct Group
        {
            Material* material = nullptr;
            MaterialOverride material_override;
            bool casts_shadows          = false;
            bool excluded_from_tracing  = false;
            float distance_render       = 0.0f;
            float distance_shadow       = 0.0f;
            uint32_t vertex_count       = 0;
            uint32_t index_count        = 0;
            vector<Candidate> members;
        };

        bool same_override(const MaterialOverride& a, const MaterialOverride& b)
        {
            // an unset field is a nan and compares false against itself, so an unset pair has to be
            // recognised as equal rather than as a difference
            const auto field_matches = [](const float left, const float right)
            {
                const bool left_set  = MaterialOverride::is_set(left);
                const bool right_set = MaterialOverride::is_set(right);
                if (
                    !left_set &&
                    !right_set
                )
                {
                    return true;
                }
                return
                    left_set &&
                    right_set &&
                    left == right;
            };

            return
                field_matches(a.uv_tiling_x, b.uv_tiling_x) &&
                field_matches(a.uv_tiling_y, b.uv_tiling_y) &&
                field_matches(a.uv_offset_x, b.uv_offset_x) &&
                field_matches(a.uv_offset_y, b.uv_offset_y) &&
                field_matches(a.uv_rotation, b.uv_rotation) &&
                field_matches(a.uv_invert_x, b.uv_invert_x) &&
                field_matches(a.uv_invert_y, b.uv_invert_y) &&
                field_matches(a.uv_world_space, b.uv_world_space);
        }

        // the material alone does not decide it, two renderers that disagree on a uv override, a shadow
        // flag or a cull distance draw differently and a merge would silently pick one of the two
        bool can_share_a_mesh(const Group& group, const Candidate& candidate)
        {
            return
                group.material == candidate.material &&
                same_override(group.material_override, candidate.render->GetMaterialOverride()) &&
                group.casts_shadows == candidate.render->HasFlag(RenderFlags::CastsShadows) &&
                group.excluded_from_tracing == candidate.render->HasFlag(RenderFlags::ExcludeFromRayTracing) &&
                group.distance_render == candidate.render->GetMaxRenderDistance() &&
                group.distance_shadow == candidate.render->GetMaxShadowDistance();
        }

        // the fourth column of an inverse transpose is not a homogeneous divisor, so the 3x3 product is
        // written out rather than going through Matrix's vector product, which would divide by it
        Vector3 rotate_direction(const Matrix& normal_matrix, const Vector3& direction)
        {
            return Vector3(
                direction.x * normal_matrix.m00 + direction.y * normal_matrix.m10 + direction.z * normal_matrix.m20,
                direction.x * normal_matrix.m01 + direction.y * normal_matrix.m11 + direction.z * normal_matrix.m21,
                direction.x * normal_matrix.m02 + direction.y * normal_matrix.m12 + direction.z * normal_matrix.m22
            );
        }

        bool flips_winding(const Matrix& matrix)
        {
            const float determinant =
                matrix.m00 * (matrix.m11 * matrix.m22 - matrix.m12 * matrix.m21) -
                matrix.m01 * (matrix.m10 * matrix.m22 - matrix.m12 * matrix.m20) +
                matrix.m02 * (matrix.m10 * matrix.m21 - matrix.m11 * matrix.m20);

            return determinant < 0.0f;
        }

        // bakes one part's geometry into the group's buffers, in the root's space
        void append_baked(
            const Candidate& candidate,
            vector<RHI_Vertex_PosTexNorTan>& vertices_out,
            vector<uint32_t>& indices_out
        )
        {
            vector<RHI_Vertex_PosTexNorTan> vertices;
            vector<uint32_t> indices;
            candidate.render->GetGeometry(&indices, &vertices);
            if (
                vertices.empty() ||
                indices.empty()
            )
            {
                return;
            }

            const Matrix normal_matrix = candidate.to_root.Inverted().Transposed();
            const bool reverse         = flips_winding(candidate.to_root);

            const uint32_t vertex_base = static_cast<uint32_t>(vertices_out.size());
            for (RHI_Vertex_PosTexNorTan& vertex : vertices)
            {
                vertex.set_position(candidate.to_root * vertex.get_position());
                vertex.set_normal(Vector3::Normalize(rotate_direction(normal_matrix, vertex.get_normal())));
                vertex.set_tangent(Vector3::Normalize(rotate_direction(normal_matrix, vertex.get_tangent())));
            }
            vertices_out.insert(vertices_out.end(), vertices.begin(), vertices.end());

            indices_out.reserve(indices_out.size() + indices.size());
            for (size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3)
            {
                // a negative scale mirrors the geometry, which turns every triangle inside out unless the
                // winding is put back
                const size_t second = reverse ? triangle + 2 : triangle + 1;
                const size_t third  = reverse ? triangle + 1 : triangle + 2;
                indices_out.push_back(vertex_base + indices[triangle]);
                indices_out.push_back(vertex_base + indices[second]);
                indices_out.push_back(vertex_base + indices[third]);
            }
        }

        // reuses the resource already sitting at the path, a second merge onto the same file must not
        // strand the renderers that are pointing at the first one
        shared_ptr<Mesh> acquire_mesh(const string& file_path, const bool generate_lods)
        {
            shared_ptr<Mesh> mesh = ResourceCache::GetByPath<Mesh>(file_path);
            if (mesh)
            {
                // the sub-meshes are about to be renumbered, so the structures built against the old
                // numbering describe geometry that will not be there
                mesh->InvalidateAllBlas();
                mesh->Clear();
            }
            else
            {
                mesh = make_shared<Mesh>();
                mesh->SetResourceFilePath(file_path);
                mesh = ResourceCache::Cache(mesh);
                if (!mesh)
                {
                    return nullptr;
                }
            }

            // the geometry has already been welded and ordered by hand, and the post process optimizer
            // simplifies anything past a size threshold, which is quality this step promised not to spend
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods), generate_lods);
            return mesh;
        }

        void place_at_root(Entity* entity, Entity* root)
        {
            if (entity == root)
            {
                return;
            }

            entity->SetParent(root);
            entity->SetPositionLocal(Vector3::Zero);
            entity->SetRotationLocal(Quaternion::Identity);
            entity->SetScaleLocal(Vector3::One);
        }

        // keeps an entity where it looks like it is while its parent goes away, SetParent preserves the
        // local transform rather than the world one
        //
        // the placement is passed in rather than read here, by this point the survivors have already been
        // moved onto the root and an entity that sat under one of them no longer reports where it was
        void reparent_keeping_placement(
            Entity* entity,
            Entity* root,
            const Matrix& to_root
        )
        {
            Vector3 scale;
            Quaternion rotation;
            Vector3 translation;
            to_root.Decompose(scale, rotation, translation);

            entity->SetParent(root);
            entity->SetPositionLocal(translation);
            entity->SetRotationLocal(rotation);
            entity->SetScaleLocal(scale);
        }
    }

    MergeReport MergeRenderersByMaterial(
        Entity* root,
        const string& mesh_file_path,
        const bool generate_lods
    )
    {
        MergeReport report;

        if (!root)
        {
            report.error = "no root entity";
            return report;
        }
        if (mesh_file_path.empty())
        {
            report.error = "no output mesh path";
            return report;
        }

        // gather every renderer in the subtree, then decide which of them can give up its own entity
        vector<Entity*> subtree;
        subtree.push_back(root);
        root->GetDescendants(&subtree);

        // where everything sat before any of it moved, the merge rewrites transforms as it goes and the
        // entities that have to be rescued from a deleted parent are rescued afterwards
        const Matrix root_inverse = root->GetMatrix().Inverted();
        unordered_map<Entity*, Matrix> placement;
        for (Entity* entity : subtree)
        {
            if (entity)
            {
                placement[entity] = entity->GetMatrix() * root_inverse;
            }
        }

        vector<Candidate> candidates;
        for (Entity* entity : subtree)
        {
            Render* render = entity ? entity->GetComponent<Render>() : nullptr;
            if (
                !render ||
                !render->GetMesh() ||
                render->GetIndexCount() == 0
            )
            {
                continue;
            }

            report.renderers_before++;
            report.vertices_before += render->GetVertexCount();
            report.indices_before  += render->GetIndexCount();

            const auto skip = [&report, entity](const char* reason)
            {
                report.skipped.push_back({ entity->GetObjectName(), reason });
            };

            // a skinned mesh is driven by a skeleton and a set of instances is driven by a transform
            // list, in both cases baking the geometry down would throw the driver away
            if (render->GetMesh()->IsSkinned())
            {
                skip("skinned");
                continue;
            }
            if (render->HasInstancing())
            {
                skip("instanced");
                continue;
            }
            if (!render->GetMaterial())
            {
                skip("no material");
                continue;
            }
            // the entity carries something besides the geometry, a collider or a light or a sound, and
            // that something is placed where the entity is
            if (entity->GetComponentCount() > 1)
            {
                skip("carries other components");
                continue;
            }

            Candidate candidate;
            candidate.entity   = entity;
            candidate.render   = render;
            candidate.material = render->GetMaterial();
            candidate.to_root  = placement[entity];
            candidates.push_back(candidate);
        }

        vector<Group> groups;
        for (const Candidate& candidate : candidates)
        {
            const auto match = find_if(
                groups.begin(),
                groups.end(),
                [&candidate](const Group& group)
                {
                    return can_share_a_mesh(group, candidate);
                }
            );

            if (match != groups.end())
            {
                match->members.push_back(candidate);
                continue;
            }

            Group group;
            group.material             = candidate.material;
            group.material_override    = candidate.render->GetMaterialOverride();
            group.casts_shadows        = candidate.render->HasFlag(RenderFlags::CastsShadows);
            group.excluded_from_tracing = candidate.render->HasFlag(RenderFlags::ExcludeFromRayTracing);
            group.distance_render      = candidate.render->GetMaxRenderDistance();
            group.distance_shadow      = candidate.render->GetMaxShadowDistance();
            group.members.push_back(candidate);
            groups.push_back(move(group));
        }

        // a group of one is already the cheapest it can be, rewriting it would only move its geometry
        // into a new file for nothing
        groups.erase(
            remove_if(
                groups.begin(),
                groups.end(),
                [](const Group& group)
                {
                    return group.members.size() < 2;
                }
            ),
            groups.end()
        );

        if (groups.empty())
        {
            report.ok              = true;
            report.renderers_after = report.renderers_before;
            report.vertices_after  = report.vertices_before;
            report.indices_after   = report.indices_before;
            return report;
        }

        // bake first, a group that produces nothing must not consume a sub-mesh slot
        vector<vector<RHI_Vertex_PosTexNorTan>> group_vertices(groups.size());
        vector<vector<uint32_t>> group_indices(groups.size());
        for (size_t index = 0; index < groups.size(); index++)
        {
            for (const Candidate& candidate : groups[index].members)
            {
                append_baked(candidate, group_vertices[index], group_indices[index]);
            }
            geometry_processing::weld_and_optimize(group_vertices[index], group_indices[index]);

            // recorded now, AddGeometry rewrites the buffers it is handed while it builds the levels
            groups[index].vertex_count = static_cast<uint32_t>(group_vertices[index].size());
            groups[index].index_count  = static_cast<uint32_t>(group_indices[index].size());
        }

        for (size_t index = groups.size(); index > 0; index--)
        {
            const size_t at = index - 1;
            if (
                group_vertices[at].empty() ||
                group_indices[at].empty()
            )
            {
                groups.erase(groups.begin() + at);
                group_vertices.erase(group_vertices.begin() + at);
                group_indices.erase(group_indices.begin() + at);
            }
        }

        if (groups.empty())
        {
            report.error = "the parts that shared a material carried no geometry";
            return report;
        }

        const filesystem::path output(mesh_file_path);
        if (output.has_parent_path())
        {
            filesystem::create_directories(output.parent_path());
        }

        shared_ptr<Mesh> mesh = acquire_mesh(mesh_file_path, generate_lods);
        if (!mesh)
        {
            report.error = "failed to create the merged mesh resource";
            return report;
        }

        mesh->ReserveSubMeshes(static_cast<uint32_t>(groups.size()));
        for (size_t index = 0; index < groups.size(); index++)
        {
            mesh->AddGeometry(
                group_vertices[index],
                group_indices[index],
                generate_lods,
                static_cast<uint32_t>(index)
            );
        }
        mesh->SaveToFile(mesh_file_path);
        mesh->CreateGpuBuffers();

        // one member of each group keeps its entity and takes over the merged sub-mesh, the rest go
        vector<Entity*> doomed;
        for (size_t index = 0; index < groups.size(); index++)
        {
            Group& group     = groups[index];
            Entity* survivor = group.members.front().entity;

            place_at_root(survivor, root);
            survivor->GetComponent<Render>()->SetMesh(mesh.get(), static_cast<uint32_t>(index));

            MergeGroup summary;
            summary.material_name  = group.material->GetObjectName();
            summary.entity_name    = survivor->GetObjectName();
            summary.source_count   = static_cast<uint32_t>(group.members.size());
            summary.sub_mesh_index = static_cast<uint32_t>(index);
            summary.vertex_count   = group.vertex_count;
            summary.index_count    = group.index_count;
            report.groups.push_back(move(summary));

            for (size_t member = 1; member < group.members.size(); member++)
            {
                doomed.push_back(group.members[member].entity);
            }
        }

        // anything that is not going has to come out from under something that is, removing an entity
        // takes its descendants with it
        for (Entity* entity : subtree)
        {
            if (
                !entity ||
                entity == root ||
                find(doomed.begin(), doomed.end(), entity) != doomed.end()
            )
            {
                continue;
            }

            const bool under_a_doomed_entity = any_of(
                doomed.begin(),
                doomed.end(),
                [entity](Entity* candidate)
                {
                    return entity->IsDescendantOf(candidate);
                }
            );
            if (under_a_doomed_entity)
            {
                reparent_keeping_placement(entity, root, placement[entity]);
            }
        }

        // which of them to actually ask for, decided while they are all still alive. removing an entity
        // frees its descendants with it, so a nested one must not be visited afterwards, not even to read
        // its parent
        vector<Entity*> removal_roots;
        for (Entity* entity : doomed)
        {
            const bool has_a_doomed_ancestor = any_of(
                doomed.begin(),
                doomed.end(),
                [entity](Entity* candidate)
                {
                    return
                        candidate != entity &&
                        entity->IsDescendantOf(candidate);
                }
            );
            if (!has_a_doomed_ancestor)
            {
                removal_roots.push_back(entity);
            }
        }

        for (Entity* entity : removal_roots)
        {
            World::RemoveEntityImmediate(entity);
        }
        report.entities_removed = static_cast<uint32_t>(doomed.size());

        report.ok        = true;
        report.mesh_path = mesh_file_path;

        // recount rather than deriving it, and count the same way the before pass did so the two numbers
        // can be compared. the mesh's own totals include every lod, which would read as geometry gained
        vector<Entity*> remaining;
        remaining.push_back(root);
        root->GetDescendants(&remaining);
        for (Entity* entity : remaining)
        {
            Render* render = entity ? entity->GetComponent<Render>() : nullptr;
            if (
                !render ||
                !render->GetMesh() ||
                render->GetIndexCount() == 0
            )
            {
                continue;
            }

            report.renderers_after++;
            report.vertices_after += render->GetVertexCount();
            report.indices_after  += render->GetIndexCount();
        }

        return report;
    }
}
