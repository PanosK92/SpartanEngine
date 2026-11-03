// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace spartan::road_vertical_profile
{
    // Limits change of grade per horizontal metre (roughly a 500 m vertical radius).
    // This is a geometry constraint, not a guarantee of tire contact at arbitrary speed.
    constexpr double max_curvature = 0.002;
    constexpr double max_sample_spacing = 2.0;

    inline std::vector<double> Distances(const std::vector<float>& spans)
    {
        std::vector<double> x(spans.size() + 1, 0.0);
        for (size_t i = 1; i < x.size(); ++i)
            x[i] = x[i - 1] + std::max(static_cast<double>(spans[i - 1]), 0.001);
        return x;
    }

    // Exact triangular convolution of a piecewise-linear height profile. Unlike
    // two sampled box passes, this is independent of tessellation density and
    // has a continuous grade. Integrate twice to evaluate each window in O(log n).
    inline void Round(std::vector<float>& heights, const std::vector<float>& spans, double radius, bool closed)
    {
        if (heights.size() < 3 || spans.size() + 1 != heights.size() || radius <= 0.0)
            return;
        const auto x = Distances(spans);
        const size_t n = heights.size();
        const double total = x.back();
        // Subtract a datum to avoid cancellation on elevated, kilometre-long roads.
        const double datum = heights.front();
        std::vector<double> a(n, 0.0), b(n, 0.0);
        for (size_t i = 1; i < n; ++i)
        {
            const double d = x[i] - x[i - 1];
            const double h = heights[i - 1] - datum;
            const double dh = heights[i] - heights[i - 1];
            a[i] = a[i - 1] + d * (h + dh * 0.5);
            b[i] = b[i - 1] + a[i - 1] * d + d * d * (h * 0.5 + dh / 6.0);
        }
        auto integral = [&](double s)
        {
            double offset = 0.0;
            if (closed)
            {
                const double cycles = std::floor(s / total);
                s -= cycles * total;
                offset = cycles * b.back() + cycles * (cycles - 1.0) * total * a.back() * 0.5 + cycles * a.back() * s;
            }
            if (s <= 0.0)
                return offset; // flat extension at the datum
            if (s >= total)
            {
                const double d = s - total;
                return offset + b.back() + a.back() * d + (heights.back() - datum) * d * d * 0.5;
            }
            const size_t hi = static_cast<size_t>(std::upper_bound(x.begin(), x.end(), s) - x.begin());
            const size_t lo = hi - 1;
            const double d = s - x[lo];
            const double grade = (heights[hi] - heights[lo]) / (x[hi] - x[lo]);
            return offset + b[lo] + a[lo] * d + (heights[lo] - datum) * d * d * 0.5 + grade * d * d * d / 6.0;
        };
        std::vector<float> result(n);
        for (size_t i = 0; i < n; ++i)
            result[i] = static_cast<float>(datum + (integral(x[i] + radius) - 2.0 * integral(x[i]) + integral(x[i] - radius)) / (radius * radius));
        if (closed)
            result.back() = result.front();
        heights = std::move(result);
    }

    // Expand the clearance envelope BEFORE rounding. Every convolution window
    // then clears the original floor, so no final max/clamp can recreate a kink.
    // Padding by one sample span covers interpolation between envelope samples.
    inline void RoundAboveFloor(std::vector<float>& heights, const std::vector<float>& floor,
        const std::vector<float>& spans, double radius, bool closed)
    {
        if (heights.size() < 3 || floor.size() != heights.size() || spans.size() + 1 != heights.size())
            return;
        const auto x = Distances(spans);
        const double reach = radius + *std::max_element(spans.begin(), spans.end());
        std::vector<double> positions;
        std::vector<float> values;
        for (int cycle = closed ? -1 : 0; cycle <= (closed ? 1 : 0); ++cycle)
            for (size_t i = 0; i < x.size() - (closed ? 1 : 0); ++i)
            {
                positions.push_back(x[i] + cycle * x.back());
                values.push_back(floor[i]);
            }
        std::deque<size_t> window;
        size_t next = 0;
        auto floor_at = [&](double s)
        {
            if (closed) s -= std::floor(s / x.back()) * x.back();
            if (s <= 0.0) return static_cast<double>(floor.front());
            if (s >= x.back()) return static_cast<double>(floor.back());
            const size_t hi = static_cast<size_t>(std::upper_bound(x.begin(), x.end(), s) - x.begin());
            const size_t lo = hi - 1;
            return floor[lo] + (floor[hi] - floor[lo]) * (s - x[lo]) / (x[hi] - x[lo]);
        };
        for (size_t i = 0; i < heights.size(); ++i)
        {
            while (next < positions.size() && positions[next] <= x[i] + reach)
            {
                while (!window.empty() && values[window.back()] <= values[next]) window.pop_back();
                window.push_back(next++);
            }
            while (!window.empty() && positions[window.front()] < x[i] - reach) window.pop_front();
            if (!window.empty()) heights[i] = std::max(heights[i], values[window.front()]);
            heights[i] = std::max(heights[i], static_cast<float>(std::max(floor_at(x[i] - reach), floor_at(x[i] + reach))));
        }
        if (closed) heights.back() = heights.front();
        Round(heights, spans, radius, closed);
    }
}
