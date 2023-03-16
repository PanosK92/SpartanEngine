/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ======
#include "Widget.h"
//=================

//= FWD DECLARATIONS =
namespace Spartan
{
    class Renderer;
}
//====================

class TextureViewer : public Widget
{
public:
    TextureViewer(Editor* editor);

    void TickVisible() override;

private:
    uint32_t m_texture_index      = 1;
    bool m_magnifying_glass       = false;
    bool m_channel_r              = true;
    bool m_channel_g              = true;
    bool m_channel_b              = true;
    bool m_channel_a              = true;
    bool m_gamma_correct          = false;
    bool m_pack                   = false;
    bool m_boost                  = false;
    bool m_abs                    = false;
    bool m_point_sampling         = false;
};
