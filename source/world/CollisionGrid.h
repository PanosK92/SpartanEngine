/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
*/
#pragma once

#include "../math/BoundingBox.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace spartan
{
    class Entity;

    // Sparse XZ grid. Each collider uses the finest level at which its bounds
    // fit in at most four cells, including long bridges and whole terrain tiles.
    class CollisionGrid
    {
        using Cell = std::pair<int, int>;
        using Members = std::unordered_set<Entity*>;
        struct Range { int level; Cell first, last; };
        std::map<int, std::map<Cell, Members>> levels;
        std::unordered_map<Entity*, Range> membership;
        Members unbounded;

        static Cell CellAt(const math::Vector3& p, int level)
        {
            auto coordinate = [level](float value)
            {
                return static_cast<int>(std::clamp(std::floor(std::ldexp(static_cast<double>(value), -level)), -1.0e9, 1.0e9));
            };
            return {coordinate(p.x), coordinate(p.z)};
        }

        template<typename F>
        static void VisitCells(Cell first, Cell last, F visit)
        {
            for (int x = first.first; x <= last.first; ++x)
                for (int z = first.second; z <= last.second; ++z)
                    visit(Cell{x, z});
        }

    public:
        bool Contains(Entity* entity) const { return membership.contains(entity); }

        void Remove(Entity* entity)
        {
            auto it = membership.find(entity);
            if (it == membership.end()) return;
            const Range range = it->second;
            membership.erase(it);
            if (range.level < 0) { unbounded.erase(entity); return; }
            auto& cells = levels.at(range.level);
            VisitCells(range.first, range.last, [&](Cell cell)
            {
                auto bucket = cells.find(cell);
                bucket->second.erase(entity);
                if (bucket->second.empty()) cells.erase(bucket);
            });
            if (cells.empty()) levels.erase(range.level);
        }

        void Update(Entity* entity, const math::BoundingBox& bounds)
        {
            Remove(entity);
            const auto& lo = bounds.GetMin();
            const auto& hi = bounds.GetMax();
            if (!lo.IsFinite() || !hi.IsFinite() || hi.x < lo.x || hi.z < lo.z)
            {
                membership.emplace(entity, Range{-1, {}, {}});
                unbounded.insert(entity);
                return;
            }
            int level = 6; // 64 m cells for ordinary props
            while (std::ldexp(1.0, level) < std::max(double(hi.x) - lo.x, double(hi.z) - lo.z)) ++level;
            const Range range{level, CellAt(lo, level), CellAt(hi, level)};
            membership.emplace(entity, range);
            auto& cells = levels[level];
            VisitCells(range.first, range.last, [&](Cell cell) { cells[cell].insert(entity); });
        }

        void Query(const math::BoundingBox& box, Members& result) const
        {
            if (!box.GetMin().IsFinite() || !box.GetMax().IsFinite()) { All(result); return; }
            result.insert(unbounded.begin(), unbounded.end());
            for (const auto& [level, cells] : levels)
            {
                const Cell first = CellAt(box.GetMin(), level), last = CellAt(box.GetMax(), level);
                const uint64_t count = uint64_t(last.first - first.first + 1) * (last.second - first.second + 1);
                if (count > cells.size())
                {
                    for (auto it = cells.lower_bound(first); it != cells.end() && it->first.first <= last.first; ++it)
                        if (it->first.second >= first.second && it->first.second <= last.second)
                            result.insert(it->second.begin(), it->second.end());
                }
                else VisitCells(first, last, [&](Cell cell)
                {
                    if (auto it = cells.find(cell); it != cells.end()) result.insert(it->second.begin(), it->second.end());
                });
            }
        }

        void All(Members& result) const
        {
            for (const auto& entry : membership) result.insert(entry.first);
        }
    };
}
