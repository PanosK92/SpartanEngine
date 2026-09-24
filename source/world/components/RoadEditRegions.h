/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <cstddef>

namespace spartan::road_edit
{
    // Visit both footprints of changed segments. Retain matching prefixes/suffixes when
    // sampling density or topology changes, and include the neighbours of a moved point.
    template<typename Points, typename Equal, typename VisitOld, typename VisitNew>
    void changed_segments(const Points& old_points, const Points& new_points, Equal equal,
        VisitOld visit_old, VisitNew visit_new, bool force = false)
    {
        const size_t old_count = old_points.size() > 1 ? old_points.size() - 1 : 0;
        const size_t new_count = new_points.size() > 1 ? new_points.size() - 1 : 0;
        size_t prefix = 0;
        size_t suffix = 0;
        if (!force)
        {
            while (prefix < old_count && prefix < new_count &&
                equal(prefix, prefix) && equal(prefix + 1, prefix + 1)) ++prefix;
            while (suffix < old_count - prefix && suffix < new_count - prefix &&
                equal(old_count - suffix, new_count - suffix) &&
                equal(old_count - suffix - 1, new_count - suffix - 1)) ++suffix;
        }
        for (size_t i = prefix; i < old_count - suffix; ++i)
        {
            if (!force && old_count == new_count && equal(i, i) && equal(i + 1, i + 1)) continue;
            visit_old(i);
        }
        for (size_t i = prefix; i < new_count - suffix; ++i)
        {
            if (!force && old_count == new_count && equal(i, i) && equal(i + 1, i + 1)) continue;
            visit_new(i);
        }
    }
}
