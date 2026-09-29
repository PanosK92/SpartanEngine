/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "../math/Vector3.h"
#include <memory>
#include <vector>

namespace spartan
{
    class Entity;

    // the fastest path around a closed spline road, sampled every couple of meters
    // pure geometry, one line is shared by every ai driver racing that road
    class RacingLine
    {
    public:
        struct Point
        {
            math::Vector3 center;         // road centerline on the deck
            math::Vector3 right;          // horizontal, to the right of the direction of travel
            math::Vector3 position;       // racing line
            math::Vector3 line_right;     // horizontal, to the right of the racing line, which crosses the road at an angle
            float half_width = 0.0f;
            float offset     = 0.0f;      // racing line distance right of the centerline
            float distance   = 0.0f;      // arc length along the racing line from the start
            float step       = 0.0f;      // racing line length to the next point
            float curvature  = 0.0f;      // positive turns right
        };

        // where a position sits relative to the line
        struct Projection
        {
            size_t index        = 0;
            float distance      = 0.0f; // along the line
            float line_error    = 0.0f; // right of the racing line
            float center_offset = 0.0f; // right of the road centerline
        };

        // edge_margin is the room kept between a car's center and the road edge, nullptr when the entity has no usable spline road
        static std::shared_ptr<RacingLine> Build(Entity* spline_entity, float edge_margin = 1.7f);

        uint64_t GetTrackEntityId() const { return m_track_entity_id; }
        float GetLength() const           { return m_length; }
        size_t GetCount() const           { return m_points.size(); }
        const Point& GetPoint(size_t index) const { return m_points[index]; }

        size_t Wrap(int64_t index) const;
        size_t IndexAt(float distance, float& fraction) const;
        math::Vector3 PositionAt(float distance) const;
        float CurvatureAt(float distance) const;
        math::Vector3 DirectionAt(size_t index) const;

        // hint is the index from the previous frame, the search then only looks a little behind and ahead of it
        Projection Project(const math::Vector3& position, size_t hint, bool full_search) const;

        // cars far from the camera still need the road under them, so its collision stays loaded
        void KeepCollisionLoaded() const;
        void RestoreCollisionStreaming() const;

    private:
        void BuildLine(float edge_margin);

        std::vector<Point> m_points;
        uint64_t m_track_entity_id = 0;
        float m_length             = 0.0f;
    };
}
