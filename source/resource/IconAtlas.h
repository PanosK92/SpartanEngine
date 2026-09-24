/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ============
#include "ResourceCache.h"
//=======================

namespace spartan
{
    class RHI_Texture;

    // packs every static editor icon into a single gpu texture so the editor
    // binds one atlas instead of dozens of standalone icon textures
    class IconAtlas
    {
    public:
        // cpu decode only, safe to run before the gpu device exists
        static void DecodeSources();
        // packs decoded sources and uploads the atlas, call after DecodeSources
        static void Build();
        static void Shutdown();

        // returns the icon entry, falls back to file when the type is missing
        static const Icon& Get(IconType type);

        // the single atlas texture all icons live in
        static RHI_Texture* GetTexture();
    };
}
