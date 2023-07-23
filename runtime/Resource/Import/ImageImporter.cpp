/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =========================
#include "pch.h"
#include "ImageImporter.h"
#define FREEIMAGE_LIB
#include <FreeImage.h>
#include <Utilities.h>
#include "../../Core/ThreadPool.h"
#include "../../RHI/RHI_Texture2D.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    static bool get_is_srgb(FIBITMAP* bitmap)
    {
        if (FIICCPROFILE* icc_profile = FreeImage_GetICCProfile(bitmap))
        {
            int i, tag_count, tag_ofs, tag_size;
            unsigned char* icc, * tag, * icc_end;
            char tag_data[256];

            if (!icc_profile->data)
                return false;

            icc = static_cast<unsigned char*>(icc_profile->data);
            if (icc[36] != 'a' || icc[37] != 'c' || icc[38] != 's' || icc[39] != 'p')
                return false; // not an ICC file

            icc_end = icc + icc_profile->size;
            tag_count = icc[128 + 0] * 0x1000000 + icc[128 + 1] * 0x10000 + icc[128 + 2] * 0x100 + icc[128 + 3];

            // search for 'desc' tag
            for (i = 0; i < tag_count; i++)
            {
                tag = icc + 128 + 4 + i * 12;
                if (tag > icc_end)
                    return false; // invalid ICC file

                // check for a desc flag
                if (memcmp(tag, "desc", 4) == 0)
                {
                    tag_ofs = tag[4] * 0x1000000 + tag[5] * 0x10000 + tag[6] * 0x100 + tag[7];
                    tag_size = tag[8] * 0x1000000 + tag[9] * 0x10000 + tag[10] * 0x100 + tag[11];

                    if (static_cast<uint32_t>(tag_ofs + tag_size) > icc_profile->size)
                        return false; // invalid ICC file

                    strncpy(tag_data, (char*)(icc + tag_ofs + 12), min(255, tag_size - 12));
                    if (strcmp(tag_data, "sRGB IEC61966-2.1") == 0 || strcmp(tag_data, "sRGB IEC61966-2-1") == 0 || strcmp(tag_data, "sRGB IEC61966") == 0 || strcmp(tag_data, "* wsRGB") == 0)
                        return true;

                    return false;
                }
            }

            return false;
        }

        return false;
    }

    static uint32_t get_bits_per_channel(FIBITMAP* bitmap)
    {
        SP_ASSERT(bitmap != nullptr);
    
        const FREE_IMAGE_TYPE type = FreeImage_GetImageType(bitmap);
        uint32_t size = 0;
    
        if (type == FIT_BITMAP)
        {
            size = sizeof(BYTE);
        }
        else if (type == FIT_UINT16 || type == FIT_RGB16 || type == FIT_RGBA16)
        {
            size = sizeof(WORD);
        }
        else if (type == FIT_FLOAT || type == FIT_RGBF || type == FIT_RGBAF)
        {
            size = sizeof(float);
        }

        SP_ASSERT(size != 0);

        return size * 8;
    }

    static uint32_t get_channel_count(FIBITMAP* bitmap)
    {
        SP_ASSERT(bitmap != nullptr);

        const uint32_t bits_per_pixel   = FreeImage_GetBPP(bitmap);
        const uint32_t bits_per_channel = get_bits_per_channel(bitmap);
        const uint32_t channel_count    = bits_per_pixel / bits_per_channel;

        SP_ASSERT(channel_count != 0);
    
        return channel_count;
    }

    static RHI_Format get_rhi_format(const uint32_t bits_per_channel, const uint32_t channel_count)
    {
        SP_ASSERT(bits_per_channel != 0);
        SP_ASSERT(channel_count != 0);

        RHI_Format format = RHI_Format::Undefined;

        if (channel_count == 1)
        {
            if (bits_per_channel == 8)
            {
                format = RHI_Format::R8_Unorm;
            }
            else if (bits_per_channel == 16)
            {
                format = RHI_Format::R16_Unorm;
            }
        }
        else if (channel_count == 2)
        {
            if (bits_per_channel == 8)
            {
                format = RHI_Format::R8G8_Unorm;
            }
        }
        else if (channel_count == 3)
        {
            if (bits_per_channel == 32)
            {
                format = RHI_Format::R32G32B32_Float;
            }
        }
        else if (channel_count == 4)
        {
            if (bits_per_channel == 8)
            {
                format = RHI_Format::R8G8B8A8_Unorm;
            }
            else if (bits_per_channel == 16)
            {
                format = RHI_Format::R16G16B16A16_Unorm;
            }
            else if (bits_per_channel == 32)
            {
                format = RHI_Format::R32G32B32A32_Float;
            }
        }

        SP_ASSERT(format != RHI_Format::Undefined);

        return format;
    }

    // Converts a bitmap to 8 bits. If the bitmap was a high-color bitmap (16, 24 or 32-bit) or
    // if it was a monochrome or greyscale bitmap (1 or 4-bit), the end result will be a greyscale
    // bitmap, otherwise (1 or 4-bit palletised bitmaps) it will be a palletised bitmap.
    static FIBITMAP* convert_to_8bits(FIBITMAP* bitmap)
    {
        SP_ASSERT(bitmap != nullptr);

        FIBITMAP* previous_bitmap = bitmap;
        bitmap = FreeImage_ConvertTo8Bits(previous_bitmap);
        FreeImage_Unload(previous_bitmap);

        SP_ASSERT(bitmap != nullptr);

        return bitmap;
    }

    static FIBITMAP* convert_to_32bits(FIBITMAP* bitmap)
    {
        SP_ASSERT(bitmap != nullptr);

        FIBITMAP* previous_bitmap = bitmap;
        bitmap = FreeImage_ConvertTo32Bits(previous_bitmap);
        FreeImage_Unload(previous_bitmap);

        SP_ASSERT(bitmap != nullptr);

        return bitmap;
    }

    static FIBITMAP* rescale(FIBITMAP* bitmap, const uint32_t width, const uint32_t height)
    {
        SP_ASSERT(bitmap != nullptr);
        SP_ASSERT(width != 0);
        SP_ASSERT(height != 0);

        FIBITMAP* previous_bitmap = bitmap;
        bitmap = FreeImage_Rescale(previous_bitmap, width, height, FREE_IMAGE_FILTER::FILTER_LANCZOS3);

        if (!bitmap)
        {
            SP_LOG_ERROR("Failed");
            return previous_bitmap;
        }

        FreeImage_Unload(previous_bitmap);
        return bitmap;
    }

    static FIBITMAP* apply_bitmap_corrections(FIBITMAP* bitmap)
    {
        SP_ASSERT(bitmap != nullptr);
    
        // Convert to a standard bitmap. FIT_UINT16 and FIT_RGBA16 are processed without errors
        // but show up empty in the editor. For now, we convert everything to a standard bitmap.
        const FREE_IMAGE_TYPE type = FreeImage_GetImageType(bitmap);
        if (type != FIT_BITMAP)
        {
            // FreeImage can't convert FIT_RGBF
            if (type != FIT_RGBF)
            {
                const auto previous_bitmap = bitmap;
                bitmap = FreeImage_ConvertToType(bitmap, FIT_BITMAP);
                FreeImage_Unload(previous_bitmap);
            }
        }

        // Textures with few colors (typically less than 8 bits) and/or a palette color type, get converted to an R8G8B8A8.
        // This is because get_channel_count() returns a single channel, and from there many issues start to occur.
        if (FreeImage_GetColorsUsed(bitmap) <= 256 && FreeImage_GetColorType(bitmap) != FIC_RGB)
        {
            bitmap = convert_to_32bits(bitmap);
        }

        // Textures with 3 channels and 8 bit per channel get converted to an R8G8B8A8 format.
        // This is because there is no such RHI_FORMAT format.
        if (get_channel_count(bitmap) == 3 && get_bits_per_channel(bitmap) == 8)
        {
            bitmap = convert_to_32bits(bitmap);
        }
    
        // Most GPUs can't use a 32 bit RGB texture as a color attachment.
        // Vulkan tells you, your GPU doesn't support it.
        // So to prevent that, we maintain the 32 bits and convert to an RGBA format.
        if (get_channel_count(bitmap) == 3 && get_bits_per_channel(bitmap) == 32)
        {
            FIBITMAP* previous_bitmap = bitmap;
            bitmap = FreeImage_ConvertToRGBAF(bitmap);
            FreeImage_Unload(previous_bitmap);
        }
    
        // Convert BGR to RGB (if needed)
        if (FreeImage_GetBPP(bitmap) == 32)
        {
            if (FreeImage_GetRedMask(bitmap) == 0xff0000 && get_channel_count(bitmap) >= 2)
            {
                if (!SwapRedBlue32(bitmap))
                {
                    SP_LOG_ERROR("Failed to swap red with blue channel");
                }
            }
        }
    
        // Flip it vertically
        FreeImage_FlipVertical(bitmap);
    
        return bitmap;
    }

    static void get_bits_from_bitmap(RHI_Texture_Mip* mip, FIBITMAP* bitmap, const uint32_t width, const uint32_t height, const uint32_t channel_count, const uint32_t bits_per_channel)
    {
        // Validate
        SP_ASSERT(mip != nullptr);
        SP_ASSERT(bitmap != nullptr);
        SP_ASSERT(width != 0);
        SP_ASSERT(height != 0);
        SP_ASSERT(channel_count != 0);

        // Compute expected data size and reserve enough memory
        const size_t size_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channel_count) * static_cast<size_t>(bits_per_channel / 8);
        if (size_bytes != mip->bytes.size())
        {
            mip->bytes.clear();
            mip->bytes.reserve(size_bytes);
            mip->bytes.resize(size_bytes);
        }

        // Copy the data over to our vector
        BYTE* bytes = FreeImage_GetBits(bitmap);
        memcpy(&mip->bytes[0], bytes, size_bytes);
    }

    void ImageImporter::Initialize()
    {
        FreeImage_Initialise();

        // Register error handler
        const auto free_image_error_handler = [](const FREE_IMAGE_FORMAT fif, const char* message)
        {
            const auto text     = (message != nullptr) ? message : "Unknown error";
            const auto format   = (fif != FIF_UNKNOWN) ? FreeImage_GetFormatFromFIF(fif) : "Unknown";
            
            SP_LOG_ERROR("%s, Format: %s", text, format);
        };
        FreeImage_SetOutputMessage(free_image_error_handler);

        // Get version
        Settings::RegisterThirdPartyLib("FreeImage", FreeImage_GetVersion(), "https://freeimage.sourceforge.io/");
    }

    void ImageImporter::Shutdown()
    {
        FreeImage_DeInitialise();
    }

    bool ImageImporter::Load(const string& file_path, const uint32_t slice_index, RHI_Texture* texture)
    {
        SP_ASSERT(texture != nullptr);

        if (!FileSystem::Exists(file_path))
        {
            SP_LOG_ERROR("Path \"%s\" is invalid.", file_path.c_str());
            return false;
        }

        // Acquire image format
        FREE_IMAGE_FORMAT format = FIF_UNKNOWN;
        {
            format = FreeImage_GetFileType(file_path.c_str(), 0);

            // If the format is unknown, try to work it out from the file path
            if (format == FIF_UNKNOWN)
            {
                format = FreeImage_GetFIFFromFilename(file_path.c_str());
            }

            // If the format is still unknown, give up
            if (!FreeImage_FIFSupportsReading(format)) 
            {
                SP_LOG_ERROR("Unsupported format");
                return false;
            }
        }

        // Load the image
        FIBITMAP* bitmap = FreeImage_Load(format, file_path.c_str());
        if (!bitmap)
        {
            SP_LOG_ERROR("Failed to load \"%s\"", file_path.c_str());
            return false;
        }

        // Deduce image properties. Important that this is done here, before ApplyBitmapCorrections(), as after that, results for grayscale seem to be always false
        const bool is_transparent = FreeImage_IsTransparent(bitmap);
        const bool is_greyscale   = FreeImage_GetColorType(bitmap) == FREE_IMAGE_COLOR_TYPE::FIC_MINISBLACK;
        const bool is_srgb        = get_is_srgb(bitmap);

        // Perform some corrections
        bitmap = apply_bitmap_corrections(bitmap);
        if (!bitmap)
        {
            SP_LOG_ERROR("Failed to apply bitmap corrections");
            return false;
        }

        // Deduce image properties
        const uint32_t bits_per_channel   = get_bits_per_channel(bitmap);
        const uint32_t channel_count      = get_channel_count(bitmap);
        const RHI_Format image_format     = get_rhi_format(bits_per_channel, channel_count);
        const bool user_define_dimensions = (texture->GetWidth() != 0 && texture->GetHeight() != 0);
        const bool dimension_mismatch     = (FreeImage_GetWidth(bitmap) != texture->GetWidth() && FreeImage_GetHeight(bitmap) != texture->GetHeight());
        const bool scale                  = user_define_dimensions && dimension_mismatch;
        bitmap                            = scale ? rescale(bitmap, texture->GetWidth(), texture->GetHeight()) : bitmap;
        const unsigned int width          = FreeImage_GetWidth(bitmap);
        const unsigned int height         = FreeImage_GetHeight(bitmap);

        // Fill RGBA vector with the data from the FIBITMAP
        RHI_Texture_Mip& mip = texture->CreateMip(slice_index);
        get_bits_from_bitmap(&mip, bitmap, width, height, channel_count, bits_per_channel);

        // Free memory 
        FreeImage_Unload(bitmap);

        // Fill RHI_Texture with image properties
        {
            SP_ASSERT(bits_per_channel != 0);
            SP_ASSERT(channel_count != 0);
            SP_ASSERT(width != 0);
            SP_ASSERT(height != 0);

            texture->SetBitsPerChannel(bits_per_channel);
            texture->SetWidth(width);
            texture->SetHeight(height);
            texture->SetChannelCount(channel_count);
            texture->SetFormat(image_format);

            uint32_t flags = texture->GetFlags();

            flags |= is_transparent ? RHI_Texture_Transparent : 0;
            flags |= is_greyscale   ? RHI_Texture_Greyscale : 0;
            flags |= is_srgb        ? RHI_Texture_Srgb : 0;

            texture->SetFlags(flags);
        }

        return true;
    }
}
