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

#include "ui/TexturePreview.h"

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
