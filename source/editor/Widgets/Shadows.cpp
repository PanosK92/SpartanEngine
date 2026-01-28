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

namespace spartan::ui
{
    ImTextureID Shadow::GetTextureID(RHI_Texture* texture)
    {
        if (!texture || texture->GetResourceState() != ResourceState::PreparedForGpu)
            return nullptr;
    
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

}
