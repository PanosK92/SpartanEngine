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
        // a jpeg with a gain map (ultra hdr): every viewer opens it as the sdr image, hdr aware ones (chrome, edge, android) apply the
        // map and show the hdr frame. sdr_data is the srgb encoded rgba half float tonemap of the frame, hdr_data the same frame
        // display encoded as rgba half or full float, both frame_width by frame_height, hdr_white_nits the luminance that frame
        // puts sdr white at, width and height the size the image is saved at
        static bool SaveHdrJpeg(const std::string& file_path, const uint32_t frame_width, const uint32_t frame_height, const uint32_t width, const uint32_t height, const void* sdr_data, const void* hdr_data, const uint32_t hdr_bits_per_channel, ImageColorSpace hdr_color_space, const float hdr_white_nits);
        static void SaveSdr(const std::string& file_path, const uint32_t width, const uint32_t height, const uint32_t channel_count, const uint32_t bits_per_channel, void* data);
        // writes 8 bit rgba straight to png, SaveSdr above expects half float data
        static void SaveSdrRgba8(const std::string& file_path, const uint32_t width, const uint32_t height, const void* data);
    };
}
