/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =======
#include <memory>
#include <cfloat>
#include "Vector2.h"
//==================

namespace spartan
{
    namespace math
    {
        class Rectangle
        {
        public:
            Rectangle() = default;

            Rectangle(const float x, const float y, const float width, const float height)
            {
                this->x      = x;
                this->y      = y;
                this->width  = width;
                this->height = height;
            }

            Rectangle(const Rectangle& rectangle)
            {
                x      = rectangle.x;
                y      = rectangle.y;
                width  = rectangle.width;
                height = rectangle.height;
            }

            ~Rectangle() = default;

            bool operator==(const Rectangle& rhs) const
            {
                return x == rhs.x && y == rhs.y &&
                       width == rhs.width && height == rhs.height;
            }

            bool operator!=(const Rectangle& rhs) const
            {
                return !(*this == rhs);
            }

            bool IsDefined() const
            {
                return width > 0.0f && height > 0.0f;
            }

            void Merge(const Vector2& point)
            {
                float min_x = x;
                float min_y = y;
                float max_x = x + width;
                float max_y = y + height;

                min_x = std::min(min_x, point.x);
                min_y = std::min(min_y, point.y);
                max_x = std::max(max_x, point.x);
                max_y = std::max(max_y, point.y);

                x      = min_x;
                y      = min_y;
                width  = max_x - min_x;
                height = max_y - min_y;
            }

            bool Intersects(const Rectangle& other) const
            {
                return !(x + width  < other.x ||
                         other.x + other.width < x ||
                         y + height < other.y ||
                         other.y + other.height < y);
            }

            bool Contains(const Rectangle& other) const
            {
                return x <= other.x &&
                       y <= other.y &&
                       x + width  >= other.x + other.width &&
                       y + height >= other.y + other.height;
            }

            // top-left + size
            float x      = 0.0f;
            float y      = 0.0f;
            float width  = 0.0f;
            float height = 0.0f;

            static const Rectangle Zero;
        };
    }
}
