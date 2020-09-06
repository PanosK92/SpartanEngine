/*
Copyright(c) 2016-2020 Panos Karabelas

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

#pragma once

//= INCLUDES =====================
#include <memory>
#include "../RHI/RHI_Definition.h"
//================================

namespace Spartan
{
    class Renderer;

    namespace Math
    {
        class Rectangle
        {
        public:
            Rectangle()
            {
                left    = std::numeric_limits<float>::max();
                top     = std::numeric_limits<float>::max();
                right   = std::numeric_limits<float>::lowest();
                bottom  = std::numeric_limits<float>::lowest();
            }

            Rectangle(const float left, const float top, const float right, const float bottom)
            {
                this->left      = left;
                this->top       = top;
                this->right     = right;
                this->bottom    = bottom;
            }

            Rectangle(const Rectangle& rectangle)
            {
                left    = rectangle.left;
                top     = rectangle.top;
                right   = rectangle.right;
                bottom  = rectangle.bottom;
            }

            ~Rectangle() = default;

            bool operator==(const Rectangle& rhs) const
            {
                return
                    left    == rhs.left     &&
                    top     == rhs.top      &&
                    right   == rhs.right    &&
                    bottom  == rhs.bottom;
            }

            bool operator!=(const Rectangle& rhs) const
            {
                return
                    left    != rhs.left     ||
                    top     != rhs.top      ||
                    right   != rhs.right    ||
                    bottom  != rhs.bottom;
            }

            bool IsDefined() const
            {
                return  left    != 0.0f ||
                        top     != 0.0f ||
                        right   != 0.0f ||
                        bottom  != 0.0f;
            }

            float Width() const { return right - left; }
            float Height() const { return bottom - top; }

            // Merge a point.
            void Merge(const Vector2& point)
            {
                left    = Math::Helper::Min(left,     point.x);
                top     = Math::Helper::Min(top,      point.y);
                right   = Math::Helper::Max(right,    point.x);
                bottom  = Math::Helper::Max(bottom,   point.y);
            }

            bool CreateBuffers(Renderer* renderer);
            static int GetIndexCount() { return 6; }
            RHI_IndexBuffer* GetIndexBuffer()   const { return m_indexBuffer.get(); }
            RHI_VertexBuffer* GetVertexBuffer() const { return m_vertexBuffer.get(); }

            float left;
            float top;
            float right;
            float bottom;

            static const Rectangle Zero;

        private:
            std::shared_ptr<RHI_VertexBuffer> m_vertexBuffer;
            std::shared_ptr<RHI_IndexBuffer> m_indexBuffer;
        };
    }
}
