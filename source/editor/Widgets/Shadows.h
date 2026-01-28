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

namespace spartan
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
     * @param shadow_image Pointer to the texture used for the shadow.
     * @param radius The radius of the shadow blur.
     * @param alpha_multiply Multiplier for the shadow's alpha transparency.
     * @param stretch_length Length to stretch the shadow.
     * @param left Indicates if the left shadow should be drawn.
     * @param right Indicates if the right shadow should be drawn.
     * @param top Indicates if the top shadow should be drawn.
     * @param bottom Indicates if the bottom shadow should be drawn.
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
     * @struct DynamicShadowSpec
     * @brief Specifies the properties for rendering a dynamic shadow.
     * @param radius The radius of the shadow blur/spread.
     * @param offset_x The horizontal offset of the shadow.
     * @param offset_y The vertical offset of the shadow.
     * @param alpha The opacity of the shadow.
     * @param color The base color of the shadow.
     * @param corner_rounding The rounding radius for the corners of the shadow.
     * @param segments The number of segments used for rounded corners (quality).
     */
    struct DynamicShadowSpec
    {
        float radius          = 12.0f;                   // shadow blur/spread radius (distance outward)
        float offset_x        = 0.0f;                    // horizontal offset
        float offset_y        = 4.0f;                    // vertical offset (typically down)
        float alpha           = 0.5f;                    // shadow opacity
        ImU32 color           = IM_COL32(0, 0, 0, 255);  // shadow base color
        float corner_rounding = 0.0f;                    // match window rounding
        int segments          = 16;                      // quality of rounded corners
    };

    /**
     * @class Shadow
     * @brief Class for rendering shadows in a UI.
     */
    class Shadow
    {
    public:

        struct PendingShadow
        {
            DynamicShadowSpec spec;
            RectOption rect;
        };

        static std::vector<PendingShadow> pending_shadows;

        // Debug flag for toggling foreground draw list
        inline static bool draw_foreground = false;

        /**
         * @brief Draws all shadows that have been queued for rendering.
         * @note Call this right after ImGui::NewFrame() to draw all collected shadows
         */
        static void FlushPendingShadows();

        /**
         * @brief Queue a shadow to be drawn (call this during widget ticks)
         * @param spec The specifications for the shadow to be drawn.
         * @param rect The rectangle around which the shadow
         * will be drawn.
         */
        static void QueueWindowShadow(const DynamicShadowSpec& spec, const RectOption& rect);

        /**
         * @brief Draws a shadow around a specified rectangle.
         * @param spec The specifications for the shadow to be drawn.
         * @param rectOption The rectangle around which the shadow will be drawn.
         */
        static void DrawShadow(const ShadowSpec& spec, const RectOption& rectOption);

        /**
         * @brief Draws an outer drop shadow around a specified rectangle.
         * @param spec The specifications for the shadow to be drawn.
         * @param rect The rectangle around which the shadow will be drawn.
         */
        static void DrawShadow(const DynamicShadowSpec& spec, const RectOption& rect);
        
        /**
         * @brief Draws an inner shadow within a specified rectangle.
         * @param spec The specifications for
         * the inner shadow to be drawn.
         * @param rectOption The rectangle within which the inner shadow will be drawn.
         */
        static void DrawInnerShadow(const ShadowSpec& spec, const RectOption& rectOption);

        /**
         * @brief Draws an inner shadow within a specified rectangle.
         * @param spec The specifications for the inner shadow to be drawn.
         * @param rect The rectangle within which the inner shadow will be drawn.
         */
        static void DrawInnerShadow(const DynamicShadowSpec& spec, const RectOption& rect);

        /**
         * @brief Draws a shadow around the current ImGui window.
         * @param spec The specifications for the shadow to be drawn.
         */
        static void DrawWindowShadow(const DynamicShadowSpec& spec = DynamicShadowSpec());

    private:

        /**
         * @brief Gets the ImGui texture ID for a given RHI_Texture.
         * @param texture The RHI_Texture for which to get the ImGui texture ID.
         * @return The ImGui texture ID corresponding to the given RHI_Texture.
         */
        static ImTextureID GetTextureID(RHI_Texture* texture);

        /**
         * @brief Draws a single shadow layer with alpha from the specified parameters.
         * @param draw_list The draw list to which the shadow layer will be added.
         * @param min The minimum coordinates of the rectangle.
         * @param max The maximum coordinates of the rectangle.
         * @param rounding The rounding radius for the corners.
         * @param color The color of the shadow.
         * @param expand The amount to expand the shadow beyond the rectangle.
         */
        static void DrawShadowLayer(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, float rounding, ImU32 color, float expand);
    };
}
