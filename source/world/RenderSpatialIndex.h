/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
*/
#pragma once

#include "components/Render.h"
#include "components/Camera.h"
#include "Entity.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace spartan
{
    // Persistent world-space BVH. Ownership stays with World; rebuild before using
    // it after membership changes. Dirty leaves refit only their ancestor chains.
    // This is a visibility index, never a physics or gameplay activation policy.
    class RenderSpatialIndex
    {
        struct Node
        {
            math::BoundingBox bounds;
            uint32_t begin = 0, end = 0, left = 0, right = 0, parent = UINT32_MAX;
        };
        struct Leaf
        {
            uint32_t node = 0;
            uint64_t id   = 0; // a recycled component address must not pass as the indexed render
        };
        std::vector<Node> nodes;
        std::vector<const RenderSceneData*> entries;
        std::vector<const RenderSceneData*> moving;
        std::unordered_map<Render*, Leaf> leaves;
        std::unordered_set<const RenderSceneData*> static_members;
        std::unordered_set<const RenderSceneData*> moving_members;

        static math::BoundingBox Bounds(const RenderSceneData& data)
        {
            const auto& box = data.bounding_box;
            // Invalid/unbounded geometry must never poison finite sibling bounds.
            return box.GetMin().IsFinite() && box.GetMax().IsFinite() ? box : math::BoundingBox::Infinite;
        }
        math::BoundingBox LeafBounds(const Node& node) const
        {
            auto box = Bounds(*entries[node.begin]);
            for (uint32_t i = node.begin + 1; i < node.end; ++i) box.Merge(Bounds(*entries[i]));
            return box;
        }
        uint32_t BuildNode(uint32_t begin, uint32_t end, uint32_t parent)
        {
            const uint32_t id = static_cast<uint32_t>(nodes.size());
            nodes.push_back({{}, begin, end, 0, 0, parent});
            nodes[id].bounds = LeafBounds(nodes[id]);
            if (end - begin <= 8)
            {
                for (uint32_t i = begin; i < end; ++i) leaves[entries[i]->render] = {id, entries[i]->render->GetObjectId()};
                return id;
            }
            const auto size = nodes[id].bounds.GetSize();
            const int axis = size.x >= size.y && size.x >= size.z ? 0 : (size.y >= size.z ? 1 : 2);
            auto coordinate = [axis](const RenderSceneData* data)
            {
                const auto center = Bounds(*data).GetCenter();
                return axis == 0 ? center.x : (axis == 1 ? center.y : center.z);
            };
            const uint32_t middle = begin + (end - begin) / 2;
            std::nth_element(entries.begin() + begin, entries.begin() + middle, entries.begin() + end,
                [&](auto* a, auto* b) { return coordinate(a) < coordinate(b); });
            const uint32_t left = BuildNode(begin, middle, id);
            const uint32_t right = BuildNode(middle, end, id);
            nodes[id].left = left;
            nodes[id].right = right;
            return id;
        }
        void QueryNode(uint32_t id, Camera* camera, std::vector<const RenderSceneData*>& result) const
        {
            const Node& node = nodes[id];
            if (camera && !camera->IsInViewFrustum(node.bounds)) return;
            if (node.left)
            {
                QueryNode(node.left, camera, result);
                QueryNode(node.right, camera, result);
            }
            else
                for (uint32_t i = node.begin; i < node.end; ++i) result.push_back(entries[i]);
        }
    public:
        void Build(const std::vector<const RenderSceneData*>& source)
        {
            nodes.clear(); leaves.clear(); entries.clear(); moving.clear();
            static_members.clear(); moving_members.clear();
            for (auto* data : source)
            {
                const bool dynamic = data->entity->IsDynamic();
                data->render->SetSpatiallyDynamic(dynamic);
                if (dynamic) moving.push_back(data);
                else entries.push_back(data);
            }
            nodes.reserve(source.size()); leaves.reserve(source.size());
            static_members.reserve(entries.size());
            static_members.insert(entries.begin(), entries.end());
            moving_members.insert(moving.begin(), moving.end());
            if (!entries.empty()) BuildNode(0, static_cast<uint32_t>(entries.size()), UINT32_MAX);
        }
        // Accepts a membership change that leaves the static set exactly as indexed
        // and only replaces the moving list. Returns false when a Build is required.
        bool RetainStatic(const std::vector<const RenderSceneData*>& source)
        {
            if (nodes.empty()) return false;
            std::vector<const RenderSceneData*> next_moving;
            size_t static_count = 0;
            for (auto* data : source)
            {
                if (data->entity->IsDynamic())
                {
                    next_moving.push_back(data);
                    continue;
                }
                const auto found = leaves.find(data->render);
                if (found == leaves.end() || found->second.id != data->render->GetObjectId()) return false;
                ++static_count;
            }
            if (static_count != leaves.size()) return false;
            moving.swap(next_moving);
            moving_members.clear();
            for (auto* data : moving)
            {
                data->render->SetSpatiallyDynamic(true);
                moving_members.insert(data);
            }
            return true;
        }
        // Pointer-only lookups, safe for borrowed pointers whose component may be gone.
        bool IsStatic(Render* render) const { return leaves.find(render) != leaves.end(); }
        bool Contains(const RenderSceneData* data) const { return static_members.count(data) || moving_members.count(data); }
        void Refit(Render* render)
        {
            const auto found = leaves.find(render);
            if (found == leaves.end()) return;
            uint32_t id = found->second.node;
            auto bounds = LeafBounds(nodes[id]);
            while (true)
            {
                Node& node = nodes[id];
                if (node.bounds == bounds) break;
                node.bounds = bounds;
                if (node.parent == UINT32_MAX) break;
                id = node.parent;
                bounds = nodes[nodes[id].left].bounds;
                bounds.Merge(nodes[nodes[id].right].bounds);
            }
        }
        void Query(Camera* camera, std::vector<const RenderSceneData*>& result) const
        {
            result.clear();
            if (!nodes.empty()) QueryNode(0, camera, result);
            result.insert(result.end(), moving.begin(), moving.end());
        }
    };
}
