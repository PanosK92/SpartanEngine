/*
Copyright(c) 2015-2026 Panos Karabelas & Thomas Ray

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
#include "RHI/RHI_Texture.h"
#include "../ImGui/Source/imgui.h"
#include "../ImGui/Source/imgui_internal.h"
//=================
// FWD DECLARATIONS =

//===================

namespace spartan::ui
{
    /**
     * @struct RectOption
     * @brief Represents a rectangular area with minimum and maximum coordinates.
     *
     * This struct is used to define the bounds of a rectangle in 2D space.
     *
     * Members:
     * - minRect: The minimum coordinates (top-left corner) of the rectangle.
     * - maxRect: The maximum coordinates (bottom-right corner) of the rectangle.
     * Constructors:
     * - RectOption(): Default constructor that initializes minRect and maxRect to (0, 0).
     * - RectOption(const ImVec2& min, const ImVec2& max): Constructor that initializes minRect and maxRect with specified values.
     * - RectOption(const ImRect& rect): Constructor that initializes minRect and maxRect using an ImRect object.
     */
    struct RectOption
    {
        ImVec2 minRect;
        ImVec2 maxRect;
    
        RectOption() : minRect(0, 0), maxRect(0, 0) {}
        RectOption(const ImVec2& min, const ImVec2& max) : minRect(min), maxRect(max) {}
        RectOption(const ImRect& rect) : minRect(rect.Min), maxRect(rect.Max) {}
    };

    /**
     * @struct ShadowSpec
     * @brief Specifies the properties for rendering a shadow.
     *
     * Members:
     * - RHI_Texture shadow_image: Pointer to the texture used for the shadow.
     * - float radius: The radius of the shadow blur.
     * - float alpha_multiply: Multiplier for the shadow's alpha transparency.
     * - float stretch_length: Length to stretch the shadow.
     * - bool: left shadow should be drawn.
     * - bool: right shadow should be drawn.
     * - bool: top shadow should be drawn.
     * - bool: bottom shadow should be drawn.
     */
    struct ShadowSpec
    {
        RHI_Texture* shadow_image = nullptr;
        float radius         = 0;
        float alpha_multiply = 1.0f;
        float stretch_length = 10.0f;
        bool left            = true;
        bool right           = true;
        bool top             = true;
        bool bottom          = true;
    };

    /**
     * @class Shadow
     * @brief Class for rendering shadows in a UI.
     */
    class Shadow
    {
    public:
        /**
         * @brief Draws a shadow around a specified rectangle.
         * @param spec The specifications for the shadow to be drawn.
         * @param rectOption The rectangle around which the shadow will be drawn.
         */
        static void DrawShadow(const ShadowSpec& spec, const RectOption& rectOption);

        /**
         * @brief Draws an inner shadow within a specified rectangle.
         * @param spec The specifications for
         * the inner shadow to be drawn.
         * @param rectOption The rectangle within which the inner shadow will be drawn.

         */
        static void DrawInnerShadow(const ShadowSpec& spec, const RectOption& rectOption);

    private:
        static ImTextureID GetTextureID(RHI_Texture* texture);
    };
}
