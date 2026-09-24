/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ====
#include <string>
//===============

namespace spartan
{
    class RHI_Texture;

    enum class ImageColorSpace
    {
        LinearRec709,
        Srgb,
        Hdr10
    };

    class ImageImporter
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void Load(const std::string& file_path, const uint32_t slice_index, RHI_Texture* texture);
        // EXR output is linear Rec.709; HDR10 is converted to scRGB (1 = 80 nits).
        static void Save(const std::string& file_path, const uint32_t width, const uint32_t height, const uint32_t channel_count, const uint32_t bits_per_channel, void* data, ImageColorSpace color_space = ImageColorSpace::LinearRec709);
        static void SaveSdr(const std::string& file_path, const uint32_t width, const uint32_t height, const uint32_t channel_count, const uint32_t bits_per_channel, void* data);
        // writes 8 bit rgba straight to png, SaveSdr above expects half float data
        static void SaveSdrRgba8(const std::string& file_path, const uint32_t width, const uint32_t height, const void* data);
    };
}
