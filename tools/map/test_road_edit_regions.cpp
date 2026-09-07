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

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
#include "../../source/world/components/RoadEditRegions.h"

struct Point
{
    int x, y;
    bool operator==(const Point&) const = default;
};
using Road = std::vector<Point>;

static void check(const Road& old, const Road& next, std::vector<size_t> expected_old,
    std::vector<size_t> expected_new, bool force = false)
{
    std::vector<size_t> changed_old, changed_new;
    spartan::road_edit::changed_segments(old, next,
        [&](size_t a, size_t b) { return old[a] == next[b]; },
        [&](size_t i) { changed_old.push_back(i); },
        [&](size_t i) { changed_new.push_back(i); }, force);
    assert(changed_old == expected_old);
    assert(changed_new == expected_new);

    // Restore and re-stamp only the dirty footprint, including an overlapping unchanged road.
    // Its result must match a full rebuild while distant cells remain untouched.
    const Road neighbour = {{8, 7}, {14, 7}};
    auto stamp = [](const Road& road, int x)
    {
        int height = 0;
        for (size_t i = 1; i < road.size(); ++i)
            if (x >= road[i - 1].x && x <= road[i].x)
                height = std::max(height, std::max(road[i - 1].y, road[i].y));
        return height;
    };
    std::vector<int> incremental(100), rebuilt(100);
    std::vector<bool> dirty(100);
    auto mark = [&](const Road& road, size_t i)
    {
        for (int x = road[i].x; x <= road[i + 1].x; ++x) dirty[x] = true;
    };
    for (size_t i : changed_old) mark(old, i);
    for (size_t i : changed_new) mark(next, i);
    for (int x = 0; x < 100; ++x)
    {
        incremental[x] = std::max(stamp(old, x), stamp(neighbour, x));
        rebuilt[x] = std::max(stamp(next, x), stamp(neighbour, x));
        if (dirty[x]) incremental[x] = rebuilt[x];
    }
    assert(incremental == rebuilt);
    assert(!dirty[99]);
}

int main()
{
    const Road base = {{0, 1}, {5, 1}, {10, 1}, {15, 1}, {20, 1}, {25, 1}};
    check(base, base, {}, {});
    Road moved = base;
    moved[2] = {11, 4};
    check(base, moved, {1, 2}, {1, 2});
    moved = base;
    moved.front().y = 2;
    check(base, moved, {0}, {0});
    moved = base;
    moved.back().y = 2;
    check(base, moved, {4}, {4});
    moved = base;
    moved.insert(moved.begin() + 2, {8, 3});
    check(base, moved, {1}, {1, 2});
    check(moved, base, {1, 2}, {1});
    check(base, {}, {0, 1, 2, 3, 4}, {});
    check({}, base, {}, {0, 1, 2, 3, 4});
    check(base, base, {0, 1, 2, 3, 4}, {0, 1, 2, 3, 4}, true);
    moved = base;
    moved[1].y = 3;
    moved[4].y = 3;
    check(base, moved, {0, 1, 3, 4}, {0, 1, 3, 4});
    check({}, {}, {}, {});
    std::cout << "Road edit region tests passed\n";
}
