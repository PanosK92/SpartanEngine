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

            // (x, y, width, height)
            [[nodiscard]] __m128 Simd() const { return simd::load4(&x); }

            bool operator==(const Rectangle& rhs) const
            {
                return simd::equal_mask(Simd(), rhs.Simd()) == 0xF;
            }

            bool operator!=(const Rectangle& rhs) const
            {
                return !(*this == rhs);
            }

            bool IsDefined() const
            {
                return (_mm_movemask_ps(_mm_cmpgt_ps(Simd(), _mm_setzero_ps())) & 0xC) == 0xC;
            }

            void Merge(const Vector2& point)
            {
                // work on (min_x, min_y, max_x, max_y), the point goes first so a nan component is skipped
                const __m128 rect    = Simd();
                const __m128 corners = _mm_add_ps(rect, _mm_movelh_ps(_mm_setzero_ps(), rect));
                const __m128 p       = _mm_movelh_ps(point.Simd(), point.Simd());
                const __m128 merged  = _mm_blend_ps(_mm_min_ps(p, corners), _mm_max_ps(p, corners), 0xC);
                simd::store4(&x, _mm_sub_ps(merged, _mm_movelh_ps(_mm_setzero_ps(), merged)));
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
