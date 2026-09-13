// Copyright(c) 2015-2026 Panos Karabelas. Distributed under the MIT license.
#pragma once
#include <algorithm>

namespace spartan::road_cross_section
{
    // Racing main roads: 4 x 3.5 m lanes + 0.5 m paved margin per side.
    // Technical roads: 8 m asphalt, retaining room for two cars to pass.
    constexpr float main_width = 15.0f;
    constexpr float technical_width = 8.0f;
    constexpr float edge_margin = 0.5f;
    constexpr bool FourLanes(float width) { return width >= 14.0f; }
    constexpr float EdgeOffset(float width) { return std::max(0.0f,width*0.5f-edge_margin); }
    constexpr float DividerOffset(float width) { return EdgeOffset(width)*0.5f; }
    constexpr float TrafficOffset(float width)
    {
        // Ambient traffic occupies the outer lane on wide roads. Keep the
        // established two-way routing/offset for legacy and smaller roads.
        return FourLanes(width) ? EdgeOffset(width)*0.75f : std::min(width*.25f,width*.5f-1.15f);
    }
}
