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

//= INCLUDES =====================

#include "pch.h"
#include "Shadows.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    std::vector<Shadow::PendingShadow> Shadow::pending_shadows;

    void Shadow::QueueWindowShadow(const DynamicShadowSpec& spec, const RectOption& rect)
    {
        pending_shadows.push_back({.spec = spec, .rect = rect});
    }

    void Shadow::FlushPendingShadows()
    {
        // Use foreground draw list so shadows appear on top of the main docked content
        // Background draw list is hidden by the main editor window's opaque background
        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        for (const auto& shadow : pending_shadows)
        {
            const auto& spec = shadow.spec;
            const auto& rect = shadow.rect;

            ImVec2 shadowMin(rect.minRect.x + spec.offset_x, rect.minRect.y + spec.offset_y);
            ImVec2 shadowMax(rect.maxRect.x + spec.offset_x, rect.maxRect.y + spec.offset_y);

            int r = (spec.color >> 0) & 0xFF;
            int g = (spec.color >> 8) & 0xFF;
            int b = (spec.color >> 16) & 0xFF;

            const int layers = ImMax(static_cast<int>(spec.radius), 16);

            // Draw from outermost to innermost
            for (int i = layers; i >= 0; --i)
            {
                float t = static_cast<float>(i) / static_cast<float>(layers);

                // Smooth gaussian falloff
                float alpha_factor = expf(-t * t * 3.0f);
                int alpha          = static_cast<int>(spec.alpha * alpha_factor * 255.0f * 0.12f);
                alpha              = ImClamp(alpha, 0, 255);

                if (alpha == 0) continue;

                ImU32 layer_color = IM_COL32(r, g, b, alpha);
                float expand      = spec.radius * t;

                DrawShadowLayer(drawList, shadowMin, shadowMax, spec.corner_rounding, layer_color, expand);
            }
        }

        pending_shadows.clear();
    }

    void Shadow::DrawShadowLayer(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, float rounding, ImU32 color, float expand)
    {
        ImVec2 shadow_min(min.x - expand, min.y - expand);
        ImVec2 shadow_max(max.x + expand, max.y + expand);
    
        // Scale rounding proportionally to the expanded size
        float scaled_rounding = rounding > 0 ? rounding + expand : 0;
    
        draw_list->AddRectFilled(shadow_min, shadow_max, color, scaled_rounding);
    }

    ImTextureID Shadow::GetTextureID(RHI_Texture* texture)
    {
        if (!texture || texture->GetResourceState() != ResourceState::PreparedForGpu)
            return 0;
    
        return reinterpret_cast<ImTextureID>(texture);
    }

    void Shadow::DrawShadow(const ShadowSpec& spec, const RectOption& rectOption)
    {
        // TODO: Replace with Spartan assert
        if (!spec.shadow_image)
            return;

        ImTextureID textureID = GetTextureID(spec.shadow_image);

        // TODO: Replace with Spartan assert
        if (!textureID)
            return;

        const float widthOffset     = spec.stretch_length;
        const float alphaTop        = std::min(0.25f * spec.alpha_multiply, 1.0f);
        const float alphaSides      = std::min(0.30f * spec.alpha_multiply, 1.0f);
        const float alphaBottom     = std::min(0.60f * spec.alpha_multiply, 1.0f);
        const auto& p1  = rectOption.minRect;
        const auto& p2  = rectOption.maxRect;

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // top shadow
        if (spec.top)
        {
            drawList->AddImage(
                textureID,
                ImVec2(p1.x - widthOffset, p1.y - spec.radius),
                ImVec2(p2.x + widthOffset, p1.y),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImColor(0.0f, 0.0f, 0.0f, alphaTop));
        }

        // bottom shadow
        if (spec.bottom)
        {
            drawList->AddImage(
                textureID,
                ImVec2(p1.x - widthOffset, p2.y),
                ImVec2(p2.x + widthOffset, p2.y + spec.radius),
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f),
                ImColor(0.0f, 0.0f, 0.0f, alphaBottom));
        }

        // left shadow
        if (spec.left)
        {
            drawList->AddImageQuad(
                textureID,
                ImVec2(p1.x - spec.radius, p1.y - widthOffset),
                ImVec2(p1.x, p1.y - widthOffset),
                ImVec2(p1.x, p2.y + widthOffset),
                ImVec2(p1.x - spec.radius, p2.y + widthOffset),
                ImVec2(0.0f, 0.0f),
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 1.0f),
                ImVec2(1.0f, 0.0f),
                ImColor(0.0f, 0.0f, 0.0f, alphaSides));
        }

        // right shadow
        if (spec.right)
        {
            drawList->AddImageQuad(
                textureID,
                ImVec2(p2.x, p1.y - widthOffset),
                ImVec2(p2.x + spec.radius, p1.y - widthOffset),
                ImVec2(p2.x + spec.radius, p2.y + widthOffset),
                ImVec2(p2.x, p2.y + widthOffset),
                ImVec2(0.0f, 1.0f),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImColor(0.0f, 0.0f, 0.0f, alphaSides));
        }
    }

    void Shadow::DrawInnerShadow(const ShadowSpec& spec, const RectOption& rectOption)
    {
        // TODO: Replace with Spartan assert
        if (!spec.shadow_image)
            return;

        const float widthOffset = spec.stretch_length;
        const float alphaTop    = spec.alpha_multiply;  // std::min(0.25f * alphaMultiplier, 1.0f);
        const float alphaSides  = spec.alpha_multiply;  // std::min(0.30f * alphaMultiplier, 1.0f);
        const float alphaBottom = spec.alpha_multiply;  // std::min(0.60f * alphaMultiplier, 1.0f);

        // inner shadow draws inward from the rect edges
        const auto p1  = ImVec2(rectOption.minRect.x + spec.radius, rectOption.minRect.y + spec.radius);
        const auto p2  = ImVec2(rectOption.maxRect.x - spec.radius, rectOption.maxRect.y - spec.radius);
        auto* drawList = ImGui::GetWindowDrawList();

        ImTextureID textureID = GetTextureID(spec.shadow_image);

        // top inner shadow (flipped UV to fade inward)
        if (spec.top)
        {
            drawList->AddImage(
                textureID,
                ImVec2(p1.x - widthOffset, p1.y - spec.radius),
                ImVec2(p2.x + widthOffset, p1.y),
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f),
                ImColor(0.0f, 0.0f, 0.0f, alphaTop));
        }

        // bottom inner shadow
        if (spec.bottom)
        {
            drawList->AddImage(
                textureID,
                ImVec2(p1.x - widthOffset, p2.y),
                ImVec2(p2.x + widthOffset, p2.y + spec.radius),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImColor(0.0f, 0.0f, 0.0f, alphaBottom));
        }

        // left inner shadow
        if (spec.left)
        {
            drawList->AddImageQuad(
                textureID,
                ImVec2(p1.x - spec.radius, p1.y - widthOffset),
                ImVec2(p1.x, p1.y - widthOffset),
                ImVec2(p1.x, p2.y + widthOffset),
                ImVec2(p1.x - spec.radius, p2.y + widthOffset),
                ImVec2(0.0f, 1.0f),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImColor(0.0f, 0.0f, 0.0f, alphaSides));
        }

        // right inner shadow
        if (spec.right)
        {
            drawList->AddImageQuad(
                textureID, ImVec2(p2.x, p1.y - widthOffset),
                ImVec2(p2.x + spec.radius, p1.y - widthOffset),
                ImVec2(p2.x + spec.radius, p2.y + widthOffset),
                ImVec2(p2.x, p2.y + widthOffset),
                ImVec2(0.0f, 0.0f),
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 1.0f),
                ImVec2(1.0f, 0.0f),
                ImColor(0.0f, 0.0f, 0.0f, alphaSides));
        }
    }

    void Shadow::DrawShadow(const DynamicShadowSpec& spec, const RectOption& rect)
    {
        ImDrawList* drawList = draw_foreground ? ImGui::GetForegroundDrawList() : ImGui::GetBackgroundDrawList();

        // Apply offset to the shadow position
        ImVec2 shadowMin(rect.minRect.x + spec.offset_x, rect.minRect.y + spec.offset_y);
        ImVec2 shadowMax(rect.maxRect.x + spec.offset_x, rect.maxRect.y + spec.offset_y);

        // Extract base color RGB
        int r = (spec.color >> 0) & 0xFF;
        int g = (spec.color >> 8) & 0xFF;
        int b = (spec.color >> 16) & 0xFF;

        // More layers = smoother gradient (use at least 16 for smooth appearance)
        const int layers = ImMax(static_cast<int>(spec.radius), 16);
        if (layers <= 0) return;

        // Draw from outermost (largest, most transparent) to innermost
        for (int i = layers; i >= 0; --i)
        {
            float t = static_cast<float>(i) / static_cast<float>(layers);

            // Gaussian-like falloff for natural shadow appearance
            float alpha_factor = expf(-t * t * 4.0f);  // e^(-4t²) gives smooth falloff

            // Scale alpha by user spec, ensure visible but not oversaturated
            int alpha = static_cast<int>(spec.alpha * alpha_factor * 255.0f * 0.15f);
            alpha     = ImClamp(alpha, 0, 255);

            // Skip fully transparent layers
            if (alpha == 0) continue;

            ImU32 layer_color = IM_COL32(r, g, b, alpha);
            float expand      = spec.radius * t;

            DrawShadowLayer(drawList, shadowMin, shadowMax, spec.corner_rounding, layer_color, expand);
        }
    }

    void Shadow::DrawInnerShadow(const DynamicShadowSpec& spec, const RectOption& rect)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Extract base color RGB
        int r = (spec.color >> 0) & 0xFF;
        int g = (spec.color >> 8) & 0xFF;
        int b = (spec.color >> 16) & 0xFF;

        const float radius = spec.radius;
        const int layers   = static_cast<int>(radius);
        if (layers <= 0) return;

        // Save current clip rect and expand it slightly
        drawList->PushClipRect(rect.minRect, rect.maxRect, true);

        // Draw inward gradient using shrinking rects from the edges
        for (int i = 0; i < layers; ++i)
        {
            float t            = static_cast<float>(i) / static_cast<float>(layers);
            float alphaFactor = (1.0f - t);
            int alpha          = static_cast<int>(spec.alpha * alphaFactor * 255.0f / static_cast<float>(layers));
            alpha              = (alpha > 255) ? 255 : alpha;

            ImU32 layerColor = IM_COL32(r, g, b, alpha);
            float inset       = static_cast<float>(i);

            // Top edge
            drawList->AddRectFilled(
                ImVec2(rect.minRect.x, rect.minRect.y + inset),
                ImVec2(rect.maxRect.x, rect.minRect.y + inset + 1.0f),
                layerColor);

            // Bottom edge
            drawList->AddRectFilled(
                ImVec2(rect.minRect.x, rect.maxRect.y - inset - 1.0f),
                ImVec2(rect.maxRect.x, rect.maxRect.y - inset),
                layerColor);

            // Left edge
            drawList->AddRectFilled(
                ImVec2(rect.minRect.x + inset, rect.minRect.y),
                ImVec2(rect.minRect.x + inset + 1.0f, rect.maxRect.y),
                layerColor);

            // Right edge
            drawList->AddRectFilled(
                ImVec2(rect.maxRect.x - inset - 1.0f, rect.minRect.y),
                ImVec2(rect.maxRect.x - inset, rect.maxRect.y),
                layerColor);
        }

        drawList->PopClipRect();
    }

    void Shadow::DrawWindowShadow(const DynamicShadowSpec& spec)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        ImVec2 windowPos  = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();

        // Window bounds - the area we DON'T want the shadow to cover
        ImVec2 windowMin = windowPos;
        ImVec2 windowMax = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);

        DynamicShadowSpec adjustedSpec = spec;
        adjustedSpec.corner_rounding   = ImGui::GetStyle().WindowRounding;

        // Shadow position with offset
        ImVec2 shadow_min(windowMin.x + adjustedSpec.offset_x, windowMin.y + adjustedSpec.offset_y);
        ImVec2 shadow_max(windowMax.x + adjustedSpec.offset_x, windowMax.y + adjustedSpec.offset_y);

        int r = (adjustedSpec.color >> 0) & 0xFF;
        int g = (adjustedSpec.color >> 8) & 0xFF;
        int b = (adjustedSpec.color >> 16) & 0xFF;

        const int layers = ImMax(static_cast<int>(adjustedSpec.radius), 16);
        if (layers <= 0) return;

        // Draw each layer, clipped to 4 regions around the window (excluding window interior)
        for (int i = layers; i >= 0; --i)
        {
            float t = static_cast<float>(i) / static_cast<float>(layers);
            float alpha_factor = expf(-t * t * 3.0f);
            int alpha = static_cast<int>(adjustedSpec.alpha * alpha_factor * 255.0f * 0.12f);
            alpha = ImClamp(alpha, 0, 255);

            if (alpha == 0) continue;

            ImU32 layer_color = IM_COL32(r, g, b, alpha);
            float expand = adjustedSpec.radius * t;

            ImVec2 layer_min(shadow_min.x - expand, shadow_min.y - expand);
            ImVec2 layer_max(shadow_max.x + expand, shadow_max.y + expand);

            float scaled_rounding = adjustedSpec.corner_rounding > 0 ? adjustedSpec.corner_rounding + expand : 0;

            // Draw shadow in 4 clipped regions around the window

            // Top region (above the window)
            drawList->PushClipRect(layer_min, ImVec2(layer_max.x, windowMin.y), true);
            drawList->AddRectFilled(layer_min, layer_max, layer_color, scaled_rounding);
            drawList->PopClipRect();

            // Bottom region (below the window)
            drawList->PushClipRect(ImVec2(layer_min.x, windowMax.y), layer_max, true);
            drawList->AddRectFilled(layer_min, layer_max, layer_color, scaled_rounding);
            drawList->PopClipRect();

            // Left region (left of window, between top and bottom)
            drawList->PushClipRect(ImVec2(layer_min.x, windowMin.y), ImVec2(windowMin.x, windowMax.y), true);
            drawList->AddRectFilled(layer_min, layer_max, layer_color, scaled_rounding);
            drawList->PopClipRect();

            // Right region (right of window, between top and bottom)
            drawList->PushClipRect(ImVec2(windowMax.x, windowMin.y), ImVec2(layer_max.x, windowMax.y), true);
            drawList->AddRectFilled(layer_min, layer_max, layer_color, scaled_rounding);
            drawList->PopClipRect();
        }
    }

}
