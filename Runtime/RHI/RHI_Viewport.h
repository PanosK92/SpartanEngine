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

//= INCLUDES ======================
#include "../Core/Spartan_Object.h"
//=================================

namespace Spartan
{
    class SPARTAN_CLASS RHI_Viewport : public Spartan_Object
    {
    public:
        RHI_Viewport(const float x = 0.0f, const float y = 0.0f, const float width = 0.0f, const float height = 0.0f, const float depth_min = 0.0f, const float depth_max = 1.0f)
        {
            this->x            = x;
            this->y            = y;
            this->width        = width;
            this->height    = height;
            this->depth_min    = depth_min;
            this->depth_max    = depth_max;
        }

        RHI_Viewport(const RHI_Viewport& viewport)
        {
            x            = viewport.x;
            y            = viewport.y;
            width        = viewport.width;
            height        = viewport.height;
            depth_min    = viewport.depth_min;
            depth_max    = viewport.depth_max;
        }

        ~RHI_Viewport() = default;

        bool operator==(const RHI_Viewport& rhs) const
        {
            return 
                x            == rhs.x            && y            == rhs.y && 
                width        == rhs.width        && height        == rhs.height && 
                depth_min    == rhs.depth_min    && depth_max    == rhs.depth_max;
        }

        bool operator!=(const RHI_Viewport& rhs) const
        {
            return !(*this == rhs);
        }

        bool IsDefined() const            
        {
            return
                x            != 0.0f || 
                y            != 0.0f || 
                width        != 0.0f || 
                height        != 0.0f || 
                depth_min    != 0.0f || 
                depth_max    != 0.0f;
        }

        float AspectRatio()    const { return width / height; }

        float x            = 0.0f;
        float y            = 0.0f;
        float width        = 0.0f;
        float height    = 0.0f;
        float depth_min    = 0.0f;
        float depth_max    = 0.0f;

        static const RHI_Viewport Undefined;
    };
}
