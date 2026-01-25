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

//= INCLUDES =======================
#include "pch.h"
#include "ImageImporter.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Core/ThreadPool.h"
SP_WARNINGS_OFF
#define FREEIMAGE_LIB
#include <FreeImage/FreeImage.h>
#include <FreeImage/Utilities.h>
#define TINYDDSLOADER_IMPLEMENTATION
#include "tinyddsloader.h"
SP_WARNINGS_ON
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        bool get_is_srgb(FIBITMAP* bitmap)
        {
            if (FIICCPROFILE* icc_profile = FreeImage_GetICCProfile(bitmap))
            {
                if (!icc_profile->data || icc_profile->size < 132) // minimal size check for header + tag count
                    return false;
        
                auto icc = static_cast<unsigned char*>(icc_profile->data);
        
                // check for "acsp" signature at bytes 36-39
                if (icc[36] != 'a' || icc[37] != 'c' || icc[38] != 's' || icc[39] != 'p')
                    return false;
        
                unsigned char* icc_end = icc + icc_profile->size;
        
                // read tag_count (big endian)
                uint32_t tag_count = (icc[128] << 24) | (icc[129] << 16) | (icc[130] << 8) | icc[131];
        
                constexpr int header_size = 128;
                constexpr int tag_record_size = 12;
                char tag_data[256] = {};
        
                for (uint32_t i = 0; i < tag_count; ++i)
                {
                    unsigned char* tag = icc + header_size + 4 + i * tag_record_size;
        
                    if (tag + tag_record_size > icc_end)
                        return false; // invalid ICC file, tag record out of bounds
        
                    // check tag signature "desc"
                    if (memcmp(tag, "desc", 4) == 0)
                    {
                        // read tag offset and size (big endian)
                        uint32_t tag_ofs  = (tag[4] << 24) | (tag[5] << 16) | (tag[6] << 8) | tag[7];
                        uint32_t tag_size = (tag[8] << 24) | (tag[9] << 16) | (tag[10] << 8) | tag[11];
        
                        if (tag_ofs + tag_size > static_cast<uint32_t>(icc_profile->size) || tag_size < 12)
                            return false; // invalid tag offset or size
        
                        // copy the string data after the 12-byte header of the 'desc' tag, up to 255 chars safely
                        uint32_t string_len = min(255u, tag_size - 12);
                        strncpy_s(tag_data, sizeof(tag_data), reinterpret_cast<char*>(icc + tag_ofs + 12), string_len);
                        tag_data[string_len] = '\0'; // ensure null termination
        
                        // check for sRGB profile names
                        if (strcmp(tag_data, "sRGB IEC61966-2.1") == 0 ||
                            strcmp(tag_data, "sRGB IEC61966-2-1") == 0 ||
                            strcmp(tag_data, "sRGB IEC61966") == 0 ||
                            strcmp(tag_data, "* wsRGB") == 0)
                        {
                            return true;
                        }
        
                        return false;
                    }
                }
            }
        
            return false;
        }

        uint32_t get_bits_per_channel(FIBITMAP* bitmap)
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

        uint32_t get_channel_count(FIBITMAP* bitmap)
        {
            SP_ASSERT(bitmap != nullptr);

            const uint32_t bits_per_pixel   = FreeImage_GetBPP(bitmap);
            const uint32_t bits_per_channel = get_bits_per_channel(bitmap);
            const uint32_t channel_count    = bits_per_pixel / bits_per_channel;

            SP_ASSERT(channel_count != 0);
        
            return channel_count;
        }

        RHI_Format get_rhi_format(const uint32_t bits_per_channel, const uint32_t channel_count)
        {
            SP_ASSERT(bits_per_channel != 0);
            SP_ASSERT(channel_count != 0);

            RHI_Format format = RHI_Format::Max;

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

            SP_ASSERT(format != RHI_Format::Max);

            return format;
        }

        // converts a bitmap to 8 bits. If the bitmap was a high-color bitmap (16, 24 or 32-bit) or
        // if it was a monochrome or greyscale bitmap (1 or 4-bit), the end result will be a greyscale
        // bitmap, otherwise (1 or 4-bit palletized bitmaps) it will be a palletized bitmap
        FIBITMAP* convert_to_8bits(FIBITMAP* bitmap)
        {
            SP_ASSERT(bitmap != nullptr);

            FIBITMAP* previous_bitmap = bitmap;
            bitmap = FreeImage_ConvertTo8Bits(previous_bitmap);
            FreeImage_Unload(previous_bitmap);

            SP_ASSERT(bitmap != nullptr);

            return bitmap;
        }

        FIBITMAP* convert_to_32bits(FIBITMAP* bitmap)
        {
            SP_ASSERT(bitmap != nullptr);

            FIBITMAP* previous_bitmap = bitmap;
            bitmap = FreeImage_ConvertTo32Bits(previous_bitmap);
            FreeImage_Unload(previous_bitmap);

            SP_ASSERT(bitmap != nullptr);

            return bitmap;
        }

        FIBITMAP* rescale(FIBITMAP* bitmap, const uint32_t width, const uint32_t height)
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

        FIBITMAP* apply_bitmap_corrections(FIBITMAP* bitmap)
        {
            SP_ASSERT(bitmap != nullptr);
        
            // convert to a standard bitmap. FIT_UINT16 and FIT_RGBA16 are processed without errors
            // but show up empty in the editor. For now, we convert everything to a standard bitmap
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

            // textures with few colors (typically less than 8 bits) and/or a palette color type, get converted to an R8G8B8A8
            // this is because get_channel_count() returns a single channel, and from there many issues start to occur
            if (FreeImage_GetColorsUsed(bitmap) <= 256 && FreeImage_GetColorType(bitmap) != FIC_RGB)
            {
                bitmap = convert_to_32bits(bitmap);
            }

            // textures with 3 channels and 8 bit per channel get converted to an R8G8B8A8 format
            // this is because there is no such RHI_FORMAT format
            if (get_channel_count(bitmap) == 3 && get_bits_per_channel(bitmap) == 8)
            {
                bitmap = convert_to_32bits(bitmap);
            }
        
            // most GPUs can't use a 32 bit RGB texture as a color attachment
            // vulkan tells you, your GPU doesn't support it
            // So to prevent that, we maintain the 32 bits and convert to an RGBA format
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

            // FreeImage loads images upside down, so flip it
            FreeImage_FlipVertical(bitmap);
        
            return bitmap;
        }

        void free_image_error_handler(const FREE_IMAGE_FORMAT fif, const char* message)
        {
            const auto text   = (message != nullptr) ? message : "Unknown error";
            const auto format = (fif != FIF_UNKNOWN) ? FreeImage_GetFormatFromFIF(fif) : "Unknown";

            SP_LOG_ERROR("%s, Format: %s", text, format);
        };

        bool has_transparent_pixels(FIBITMAP* bitmap)
        {
            // Check if the bitmap supports alpha channel
            FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(bitmap);
            int bpp = FreeImage_GetBPP(bitmap);
        
            if (image_type == FIT_BITMAP && (bpp == 32 || bpp == 24)) 
            {
                if (bpp == 32) // Direct check for alpha channel
                {
                    for (unsigned y = 0; y < FreeImage_GetHeight(bitmap); ++y)
                    {
                        BYTE* bits = FreeImage_GetScanLine(bitmap, y);
                        for (unsigned x = 0; x < FreeImage_GetWidth(bitmap); ++x)
                        {
                            BYTE alpha = bits[FI_RGBA_ALPHA];
                            if (alpha != 255)
                                return true;
        
                            bits += 4; // move to the next pixel (4 bytes per pixel for 32-bit)
                        }
                    }
                }
                else if (bpp == 24) // For 24-bit, we need to check if there's a transparency mask
                {
                    // If there's a transparency mask, we'll check it
                    if (FreeImage_GetTransparencyCount(bitmap) > 0)
                    {
                        for (unsigned i = 0; i < FreeImage_GetTransparencyCount(bitmap); ++i)
                        {
                            if (FreeImage_GetTransparencyTable(bitmap)[i] != 255)
                                return true;
                        }
                    }
                    // If no transparency mask, 24-bit images are assumed to be fully opaque
                }
            }
            else if (image_type == FIT_RGBA16) // 16-bit per channel, including alpha
            {
                // For 64-bit or other formats with explicit alpha
                for (unsigned y = 0; y < FreeImage_GetHeight(bitmap); ++y)
                {
                    WORD* bits = (WORD*)FreeImage_GetScanLine(bitmap, y);
                    for (unsigned x = 0; x < FreeImage_GetWidth(bitmap); ++x)
                    {
                        WORD alpha = bits[3]; // Assuming RGBA order, alpha at index 3
                        if (alpha != 0xFFFF) // 16-bit alpha, 0xFFFF is fully opaque
                            return true;
        
                        bits += 4; // move to next pixel (4 words for RGBA16)
                    }
                }
            }
            // Other formats might not have transparency or are treated as fully opaque
        
            return false;
        }


        float half_to_float(uint16_t half)
        {
            uint32_t mant = half & 0x3FFu;
            uint32_t exp  = (half >> 10) & 0x1Fu;
            uint32_t sign = (half >> 15) & 0x1u;

            uint32_t f;
            if (exp == 0)
            {
                f = (sign << 31) | (mant ? ((127 - 15) << 23) | (mant << 13) : 0); // denormals as zero for simplicity
            }
            else if (exp == 0x1F)
            {
                f = (sign << 31) | (0x7F800000) | (mant << 13); // inf/nan
            }
            else
            {
                f = (sign << 31) | ((exp + (127 - 15)) << 23) | (mant << 13);
            }

            return *reinterpret_cast<float*>(&f); // assumes ieee 754 and little-endian
        }

        // attempt to reverse the srgb gamma if data is already gamma-encoded
        float srgb_to_linear(float srgb)
        {
            if (srgb <= 0.04045f)
                return srgb / 12.92f;
            return powf((srgb + 0.055f) / 1.055f, 2.4f);
        }

        // attempt to reverse any existing tonemapping (approximate inverse reinhard)
        float inverse_tonemap(float x)
        {
            // inverse of x/(x+1) is x/(1-x), clamped to avoid division issues
            x = min(x, 0.999f);
            return x / (1.0f - x);
        }

        // aces tonemapping (same as shader implementation)
        void aces_tonemap(float& r, float& g, float& b)
        {
            // srgb => xyz => d65_2_d60 => ap1 => rrt_sat
            float ir = r * 0.59719f + g * 0.35458f + b * 0.04823f;
            float ig = r * 0.07600f + g * 0.90834f + b * 0.01566f;
            float ib = r * 0.02840f + g * 0.13383f + b * 0.83777f;

            // rrt and odt fit
            float ar = ir * (ir + 0.0245786f) - 0.000090537f;
            float ag = ig * (ig + 0.0245786f) - 0.000090537f;
            float ab = ib * (ib + 0.0245786f) - 0.000090537f;
            float br = ir * (0.983729f * ir + 0.4329510f) + 0.238081f;
            float bg = ig * (0.983729f * ig + 0.4329510f) + 0.238081f;
            float bb = ib * (0.983729f * ib + 0.4329510f) + 0.238081f;
            ir = ar / br;
            ig = ag / bg;
            ib = ab / bb;

            // odt_sat => xyz => d60_2_d65 => srgb
            r =  ir *  1.60475f + ig * -0.53108f + ib * -0.07367f;
            g =  ir * -0.10208f + ig *  1.10813f + ib * -0.00605f;
            b =  ir * -0.00327f + ig * -0.07276f + ib *  1.07602f;

            // clamp
            r = max(0.0f, min(1.0f, r));
            g = max(0.0f, min(1.0f, g));
            b = max(0.0f, min(1.0f, b));
        }

        // linear to srgb gamma correction
        float linear_to_srgb(float linear)
        {
            if (linear <= 0.0031308f)
                return linear * 12.92f;
            return 1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
        }

        // decode pq/st.2084 to linear (inverse of linear_to_hdr10 in output.hlsl)
        void pq_to_linear(float& r, float& g, float& b)
        {
            // inverse rec.2020 to rec.709 matrix
            static const float m[3][3] = {
                {  1.6605f, -0.5876f, -0.0728f },
                { -0.1246f,  1.1329f, -0.0083f },
                { -0.0182f, -0.1006f,  1.1187f }
            };

            // inverse pq (st.2084) constants
            const float m1 = 0.1593017578125f;
            const float m2 = 78.84375f;
            const float c1 = 0.8359375f;
            const float c2 = 18.8515625f;
            const float c3 = 18.6875f;
            const float pq_max_nits = 10000.0f;
            const float sdr_white_nits = 203.0f;

            // decode pq curve
            auto decode_pq = [&](float v) -> float {
                v = max(v, 0.0f);
                float vp = powf(v, 1.0f / m2);
                float num = max(vp - c1, 0.0f);
                float den = c2 - c3 * vp;
                float linear = powf(num / max(den, 0.0001f), 1.0f / m1);
                return linear * pq_max_nits / sdr_white_nits; // normalize to sdr range
            };

            float lr = decode_pq(r);
            float lg = decode_pq(g);
            float lb = decode_pq(b);

            // convert rec.2020 back to rec.709/srgb
            r = m[0][0] * lr + m[0][1] * lg + m[0][2] * lb;
            g = m[1][0] * lr + m[1][1] * lg + m[1][2] * lb;
            b = m[2][0] * lr + m[2][1] * lg + m[2][2] * lb;

            r = max(r, 0.0f);
            g = max(g, 0.0f);
            b = max(b, 0.0f);
        }
    }

    void ImageImporter::Initialize()
    {
        FreeImage_Initialise();
        FreeImage_SetOutputMessage(free_image_error_handler);
        Settings::RegisterThirdPartyLib("FreeImage", FreeImage_GetVersion(), "https://freeimage.sourceforge.io/");
    }

    void ImageImporter::Shutdown()
    {
        FreeImage_DeInitialise();
    }

    void ImageImporter::Load(const string& file_path, const uint32_t slice_index, RHI_Texture* texture)
    {
        SP_ASSERT(texture != nullptr);

        if (!FileSystem::Exists(file_path))
        {
            SP_LOG_ERROR("Path \"%s\" is invalid.", file_path.c_str());
            return;
        }

        // acquire image format
        FREE_IMAGE_FORMAT format = FIF_UNKNOWN;
        {
            format = FreeImage_GetFileType(file_path.c_str(), 0);

            // if the format is unknown, try to work it out from the file path
            if (format == FIF_UNKNOWN)
            {
                format = FreeImage_GetFIFFromFilename(file_path.c_str());
            }

            // if the format is still unknown, give up
            if (!FreeImage_FIFSupportsReading(format)) 
            {
                SP_LOG_ERROR("Unsupported format");
                return;
            }
        }

        uint32_t texture_flags = texture->GetFlags();

        // freeimage partially supports dds, they are certain configurations that it can't load
        // So in the case of a dds format in general, we don't rely on freeimage
        if (format == FIF_DDS)
        {
            // load
            tinyddsloader::DDSFile dds_file;
            auto result = dds_file.Load(file_path.c_str());
            if (result != tinyddsloader::Success)
            {
                SP_LOG_ERROR("Failed to load DSS file");
                return;
            }

            // get format
            auto format_dxgi = dds_file.GetFormat();
            RHI_Format format = RHI_Format::Max;
            if (format_dxgi == tinyddsloader::DDSFile::DXGIFormat::BC1_UNorm)
            {
                format = RHI_Format::BC1_Unorm;
            }
            else if (format_dxgi == tinyddsloader::DDSFile::DXGIFormat::BC3_UNorm)
            {
                format = RHI_Format::BC3_Unorm;
            }
            else if (format_dxgi == tinyddsloader::DDSFile::DXGIFormat::BC5_UNorm)
            {
                format = RHI_Format::BC5_Unorm;
            }
            else if (format_dxgi == tinyddsloader::DDSFile::DXGIFormat::BC7_UNorm)
            {
                format = RHI_Format::BC7_Unorm;
            }
            SP_ASSERT(format != RHI_Format::Max);

            // set properties
            texture->SetWidth(dds_file.GetWidth());
            texture->SetHeight(dds_file.GetHeight());
            texture->SetFormat(format);

            // set data
            for (uint32_t mip_index = 0; mip_index < dds_file.GetMipCount(); mip_index++)
            {
                texture->AllocateMip();
                RHI_Texture_Mip* mip = texture->GetMip(0, mip_index);

                const auto& data = dds_file.GetImageData(mip_index, 0);
                memcpy(&mip->bytes[0], data->m_mem, mip->bytes.size());
            }

            return;
        }

        // load
        FIBITMAP* bitmap = FreeImage_Load(format, file_path.c_str());
        if (!bitmap)
        {
            SP_LOG_ERROR("Failed to load \"%s\"", file_path.c_str());
            return;
        }
        
        // deduce certain properties
        // done before ApplyBitmapCorrections(), as after that, results for grayscale seem to be always false
        texture_flags |= (FreeImage_GetColorType(bitmap) == FREE_IMAGE_COLOR_TYPE::FIC_MINISBLACK) ? RHI_Texture_Greyscale : 0;
        texture_flags |= get_is_srgb(bitmap) ? RHI_Texture_Srgb : 0;
        texture->SetFlags(texture_flags);

        // perform some corrections
        bitmap = apply_bitmap_corrections(bitmap);
        if (!bitmap)
        {
            SP_LOG_ERROR("Failed to apply bitmap corrections");
            return;
        }

        // scale if needed
        const bool user_define_dimensions = (texture->GetWidth() != 0 && texture->GetHeight() != 0);
        const bool dimension_mismatch     = (FreeImage_GetWidth(bitmap) != texture->GetWidth() && FreeImage_GetHeight(bitmap) != texture->GetHeight());
        const bool scale                  = user_define_dimensions && dimension_mismatch;
        bitmap                            = scale ? rescale(bitmap, texture->GetWidth(), texture->GetHeight()) : bitmap;

        // set properties
        texture->SetBitsPerChannel(get_bits_per_channel(bitmap));
        texture->SetWidth(FreeImage_GetWidth(bitmap));
        texture->SetHeight(FreeImage_GetHeight(bitmap));
        texture->SetChannelCount(get_channel_count(bitmap));
        texture->SetFormat(get_rhi_format(texture->GetBitsPerChannel(), texture->GetChannelCount()));
        texture->SetFlag(RHI_Texture_Transparent, has_transparent_pixels(bitmap));

        // copy data over
        texture->AllocateMip();
        RHI_Texture_Mip* mip = texture->GetMip(0, 0);
        BYTE* bytes          = FreeImage_GetBits(bitmap);
        size_t bytes_size    = FreeImage_GetPitch(bitmap) * FreeImage_GetHeight(bitmap);
        mip->bytes.resize(bytes_size);
        memcpy(&mip->bytes[0], bytes, bytes_size);

        FreeImage_Unload(bitmap);
    }

    void ImageImporter::Save(const string& file_path, const uint32_t width, const uint32_t height, const uint32_t channel_count, const uint32_t bits_per_channel, void* data)
    {
        // assume input is half float RGBA16F (from Vulkan)
        const uint16_t* src_half = static_cast<const uint16_t*>(data);
        uint32_t pixel_count     = width * height;

        // convert half -> float
        std::vector<float> converted(pixel_count * 4);
        for (uint32_t i = 0; i < pixel_count; i++)
        {
            converted[i * 4 + 0] = half_to_float(src_half[i * 4 + 0]);
            converted[i * 4 + 1] = half_to_float(src_half[i * 4 + 1]);
            converted[i * 4 + 2] = half_to_float(src_half[i * 4 + 2]);
            converted[i * 4 + 3] = half_to_float(src_half[i * 4 + 3]);
        }

        // allocate HDR bitmap (RGBAF = 128 bits per pixel float)
        FIBITMAP* bitmap = FreeImage_AllocateT(FIT_RGBAF, width, height, 128);
        if (!bitmap)
        {
            SP_LOG_ERROR("Failed to allocate FreeImage HDR bitmap");
            return;
        }

        // copy data row by row (FreeImage stores bottom-up by default)
        for (uint32_t y = 0; y < height; y++)
        {
            float* scanline = reinterpret_cast<float*>(FreeImage_GetScanLine(bitmap, height - 1 - y));
            memcpy(scanline, &converted[y * width * 4], width * 4 * sizeof(float));
        }

        // save as OpenEXR
        BOOL saved = FreeImage_Save(FIF_EXR, bitmap, file_path.c_str(), EXR_DEFAULT);
        FreeImage_Unload(bitmap);

        if (!saved)
        {
            SP_LOG_ERROR("Failed to save HDR EXR to %s", file_path.c_str());
        }
    }

    void ImageImporter::SaveSdr(const string& file_path, const uint32_t width, const uint32_t height, const uint32_t channel_count, const uint32_t bits_per_channel, void* data, bool is_hdr)
    {
        const uint16_t* src_half = static_cast<const uint16_t*>(data);

        // allocate 24-bit RGB bitmap (8 bits per channel)
        FIBITMAP* bitmap = FreeImage_Allocate(width, height, 24);
        if (!bitmap)
        {
            SP_LOG_ERROR("Failed to allocate FreeImage SDR bitmap");
            return;
        }

        for (uint32_t y = 0; y < height; y++)
        {
            BYTE* scanline = FreeImage_GetScanLine(bitmap, height - 1 - y);
            for (uint32_t x = 0; x < width; x++)
            {
                uint32_t src_idx = (y * width + x) * 4;

                // convert half to float
                float r = half_to_float(src_half[src_idx + 0]);
                float g = half_to_float(src_half[src_idx + 1]);
                float b = half_to_float(src_half[src_idx + 2]);

                if (is_hdr)
                {
                    // hdr mode: frame_output contains pq-encoded data (already tonemapped)
                    // decode pq to linear, then apply srgb gamma (no additional tonemapping needed)
                    pq_to_linear(r, g, b);
                    r = linear_to_srgb(r);
                    g = linear_to_srgb(g);
                    b = linear_to_srgb(b);
                }
                // sdr mode: frame_output is already srgb, save directly

                // convert to 8-bit (FreeImage uses BGR order)
                uint32_t dst_idx = x * 3;
                scanline[dst_idx + 0] = static_cast<BYTE>(min(max(b, 0.0f), 1.0f) * 255.0f + 0.5f);
                scanline[dst_idx + 1] = static_cast<BYTE>(min(max(g, 0.0f), 1.0f) * 255.0f + 0.5f);
                scanline[dst_idx + 2] = static_cast<BYTE>(min(max(r, 0.0f), 1.0f) * 255.0f + 0.5f);
            }
        }

        // save as PNG (lossless, universal format)
        BOOL saved = FreeImage_Save(FIF_PNG, bitmap, file_path.c_str(), PNG_Z_BEST_COMPRESSION);
        FreeImage_Unload(bitmap);

        if (!saved)
        {
            SP_LOG_ERROR("Failed to save SDR PNG to %s", file_path.c_str());
        }
    }
}
