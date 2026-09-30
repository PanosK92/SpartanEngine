/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <string>

class Editor;

namespace spartan
{
    class RHI_Texture;
}

// renders materials, meshes and prefabs through the secondary view into small images cached on disk,
// so the asset browser can show the asset itself instead of a type glyph
class AssetThumbnails
{
public:
    static void Tick(Editor* editor);
    static void Shutdown();

    // true for the files a thumbnail can be rendered for, decided by extension alone
    static bool IsSupported(const std::string& path);

    // the thumbnail once it exists, otherwise nullptr, asking is what queues it, so call it every frame the
    // item is on screen and items that scrolled away stop competing for renders
    static spartan::RHI_Texture* Get(const std::string& path);

    // throws the cached image away and renders a new one, the old image stays up until the new one lands
    static void Regenerate(const std::string& path);
};
