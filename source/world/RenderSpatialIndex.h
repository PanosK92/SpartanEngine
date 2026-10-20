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
    // Persistent world-space BVH. Ownership stays with World; Update before using
    // it after membership changes. Dirty leaves refit only their ancestor chains.
    // This is a visibility index, never a physics or gameplay activation policy.
    class RenderSpatialIndex
    {
        static constexpr uint32_t loose = UINT32_MAX; // leaf node of a static render added after the build

        struct Node
        {
            math::BoundingBox bounds;
            uint32_t begin = 0, end = 0, left = 0, right = 0, parent = UINT32_MAX;
        };
        struct Leaf
        {
            uint32_t node = 0;
            uint64_t id   = 0; // a recycled component address must not pass as the indexed render
            const RenderSceneData* data = nullptr; // compared, never read, the component may be gone
            uint32_t slot = 0; // index into loose_entries when node is loose
        };
        std::vector<Node> nodes;
        std::vector<const RenderSceneData*> entries; // null once removed, the tree keeps its shape
        std::vector<Render*> entry_renders;          // pointer values only, finds a removed entry without reading it
        std::vector<const RenderSceneData*> loose_entries;
        std::vector<Render*> loose_renders;
        std::vector<const RenderSceneData*> moving;
        std::unordered_map<Render*, Leaf> leaves;
        std::unordered_set<const RenderSceneData*> static_members;
        std::unordered_set<const RenderSceneData*> moving_members;
        uint32_t tombstones = 0;
        // the source as of the last Build or Update, the next one walks it in step to find what changed
        std::vector<const RenderSceneData*> previous;
        std::vector<Render*> previous_renders;
        std::vector<uint64_t> previous_ids;
        std::vector<uint8_t> previous_dynamic;

        static math::BoundingBox Bounds(const RenderSceneData& data)
        {
            const auto& box = data.bounding_box;
            // Invalid/unbounded geometry must never poison finite sibling bounds.
            return box.GetMin().IsFinite() && box.GetMax().IsFinite() ? box : math::BoundingBox::Infinite;
        }
        math::BoundingBox LeafBounds(const Node& node) const
        {
            bool any = false;
            math::BoundingBox box;
            for (uint32_t i = node.begin; i < node.end; ++i)
            {
                if (!entries[i]) continue;
                if (any) box.Merge(Bounds(*entries[i]));
                else box = Bounds(*entries[i]);
                any = true;
            }
            // an emptied leaf keeps its last bounds, a stale box only costs a wasted visit
            return any ? box : node.bounds;
        }
        void RemoveLeaf(Render* render, const Leaf& leaf)
        {
            static_members.erase(leaf.data);
            if (leaf.node == loose)
            {
                const uint32_t last = static_cast<uint32_t>(loose_entries.size()) - 1;
                if (leaf.slot != last)
                {
                    loose_entries[leaf.slot] = loose_entries[last];
                    loose_renders[leaf.slot] = loose_renders[last];
                    leaves[loose_renders[leaf.slot]].slot = leaf.slot;
                }
                loose_entries.pop_back();
                loose_renders.pop_back();
                return;
            }

            const Node& node = nodes[leaf.node];
            for (uint32_t i = node.begin; i < node.end; ++i)
            {
                if (entry_renders[i] == render && entries[i])
                {
                    entries[i] = nullptr;
                    ++tombstones;
                    break;
                }
            }
        }
        uint32_t BuildNode(uint32_t begin, uint32_t end, uint32_t parent)
        {
            const uint32_t id = static_cast<uint32_t>(nodes.size());
            nodes.push_back({{}, begin, end, 0, 0, parent});
            nodes[id].bounds = LeafBounds(nodes[id]);
            if (end - begin <= 8)
            {
                for (uint32_t i = begin; i < end; ++i)
                {
                    Render* render   = entries[i]->render;
                    entry_renders[i] = render;
                    leaves[render]   = {id, render->GetObjectId(), entries[i], 0};
                }
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
                for (uint32_t i = node.begin; i < node.end; ++i)
                    if (entries[i]) result.push_back(entries[i]);
        }
    public:
        void Build(const std::vector<const RenderSceneData*>& source)
        {
            nodes.clear(); leaves.clear(); entries.clear(); entry_renders.clear(); moving.clear();
            loose_entries.clear(); loose_renders.clear();
            static_members.clear(); moving_members.clear();
            tombstones = 0;
            previous = source;
            previous_renders.resize(source.size());
            previous_ids.resize(source.size());
            previous_dynamic.resize(source.size());
            for (size_t i = 0; i < source.size(); ++i)
            {
                const RenderSceneData* data = source[i];
                const bool dynamic = data->entity->IsDynamic();
                data->render->SetSpatiallyDynamic(dynamic);
                if (dynamic) moving.push_back(data);
                else entries.push_back(data);
                previous_renders[i] = data->render;
                previous_ids[i]     = data->render->GetObjectId();
                previous_dynamic[i] = dynamic ? 1 : 0;
            }
            nodes.reserve(source.size()); leaves.reserve(source.size());
            entry_renders.resize(entries.size());
            static_members.reserve(entries.size());
            static_members.insert(entries.begin(), entries.end());
            moving_members.insert(moving.begin(), moving.end());
            if (!entries.empty()) BuildNode(0, static_cast<uint32_t>(entries.size()), UINT32_MAX);
        }
        // Applies a membership change without rebuilding the tree: removed static renders leave
        // a hole, new ones join a loose list that is culled one by one. Renders that joined are
        // written to added, their bounds have never been computed. Returns false when the holes
        // and the loose list have grown enough that a Build is the cheaper option.
        bool Update(const std::vector<const RenderSceneData*>& source, std::vector<Render*>& added)
        {
            added.clear();
            if (nodes.empty() && loose_entries.empty()) return false;

            // the world keeps entity order, removals close up and additions append, so the two lists match
            // in step except where something changed, only those entries pay for a lookup
            enum : uint8_t { known_static, known_moving, unknown };
            std::vector<uint8_t> state(source.size(), unknown);
            std::vector<Render*> renders(source.size());
            std::vector<uint64_t> ids(source.size());
            std::vector<size_t> skipped;
            std::unordered_set<const RenderSceneData*> seen;
            size_t j = 0;
            for (size_t i = 0; i < source.size(); ++i)
            {
                const RenderSceneData* data = source[i];
                renders[i] = data->render;
                const uint64_t id = ids[i] = data->render->GetObjectId();
                if (j < previous.size() && previous[j] == data && previous_ids[j] == id)
                {
                    state[i] = previous_dynamic[j] ? known_moving : known_static;
                    ++j;
                    continue;
                }

                const auto found = leaves.find(data->render);
                if (found != leaves.end() && found->second.id == id && found->second.data == data) state[i] = known_static;
                else if (moving_members.count(data)) state[i] = known_moving;
                else continue;

                seen.insert(data);
                while (j < previous.size() && !(previous[j] == data && previous_ids[j] == id)) skipped.push_back(j++);
                if (j < previous.size()) ++j;
            }
            while (j < previous.size()) skipped.push_back(j++);

            // a skipped entry that never showed up again left, a recycled address is removed before it is re-added
            for (const size_t k : skipped)
            {
                if (previous_dynamic[k] || seen.count(previous[k])) continue;
                const auto found = leaves.find(previous_renders[k]);
                if (found == leaves.end() || found->second.id != previous_ids[k] || found->second.data != previous[k]) continue;
                RemoveLeaf(previous_renders[k], found->second);
                leaves.erase(found);
            }

            // moving renders are few, they are the only known ones whose classification is re-read
            std::vector<const RenderSceneData*> next_moving;
            previous_dynamic.assign(source.size(), 0);
            for (size_t i = 0; i < source.size(); ++i)
            {
                const RenderSceneData* data = source[i];
                if (state[i] == known_static) continue;
                if (data->entity->IsDynamic())
                {
                    data->render->SetSpatiallyDynamic(true);
                    next_moving.push_back(data);
                    previous_dynamic[i] = 1;
                    continue;
                }

                Render* render = data->render;
                render->SetSpatiallyDynamic(false);
                leaves[render] = {loose, render->GetObjectId(), data, static_cast<uint32_t>(loose_entries.size())};
                loose_entries.push_back(data);
                loose_renders.push_back(render);
                static_members.insert(data);
                added.push_back(render);
            }

            moving.swap(next_moving);
            moving_members.clear();
            moving_members.insert(moving.begin(), moving.end());

            previous = source;
            previous_renders.swap(renders);
            previous_ids.swap(ids);

            const size_t churn = tombstones + loose_entries.size();
            return churn <= std::max<size_t>(256, entries.size() / 4);
        }
        // Pointer-only lookups, safe for borrowed pointers whose component may be gone.
        bool IsStatic(Render* render) const { return leaves.find(render) != leaves.end(); }
        bool Contains(const RenderSceneData* data) const { return static_members.count(data) || moving_members.count(data); }
        void Refit(Render* render)
        {
            const auto found = leaves.find(render);
            // loose renders are culled with their live bounds
            if (found == leaves.end() || found->second.node == loose) return;
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
            for (auto* data : loose_entries)
            {
                if (!camera || camera->IsInViewFrustum(Bounds(*data))) result.push_back(data);
            }
            result.insert(result.end(), moving.begin(), moving.end());
        }
    };
}
