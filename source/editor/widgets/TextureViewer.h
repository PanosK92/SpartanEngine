/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include "Widget.h"
//=================

// when editing this, make sure that the bit shifts in common_buffer.hlsl are also updated
enum TextureViewOptions
{
    Visualise_Pack         = 1U << 0,
    Visualise_GammaCorrect = 1U << 1,
    Visualise_Boost        = 1U << 2,
    Visualise_Abs          = 1U << 3,
    Visualise_Channel_R    = 1U << 4,
    Visualise_Channel_G    = 1U << 5,
    Visualise_Channel_B    = 1U << 6,
    Visualise_Channel_A    = 1U << 7,
    Visualise_Sample_Point = 1U << 8,
};

class TextureViewer : public Widget
{
public:
    TextureViewer(Editor* editor);

    void OnTick() override;
    void OnVisible() override;
    void OnTickVisible() override;

    static uint32_t GetVisualisationFlags();
    static int GetMipLevel();
    static int GetArrayLevel();
    static uint64_t GetVisualisedTextureId();
};
