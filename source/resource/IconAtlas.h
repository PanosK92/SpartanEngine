/*
Copyright(c) 2015-2026 Panos Karabelas

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
