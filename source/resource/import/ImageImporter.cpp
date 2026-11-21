/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =======================
#include "pch.h"
#include <array>
#include <fstream>
#include "ImageImporter.h"
#include "../../rhi/RHI_Texture.h"
#include "../../core/ThreadPool.h"
#include "../../geometry/GeneratedCache.h"
#include "../../world/World.h"
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
                if (!icc_profile->data || icc_profile->size < 132)
                {
                    return false;
                }
        
                auto icc = static_cast<unsigned char*>(icc_profile->data);
        
                // check for "acsp" signature at bytes 36-39
                if (icc[36] != 'a' || icc[37] != 'c' || icc[38] != 's' || icc[39] != 'p')
                {
                    return false;
                }
        
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
                    {
                        return false;
                    } // invalid ICC file, tag record out of bounds
        
                    // check tag signature "desc"
                    if (memcmp(tag, "desc", 4) == 0)
                    {
                        // read tag offset and size (big endian)
                        uint32_t tag_ofs  = (tag[4] << 24) | (tag[5] << 16) | (tag[6] << 8) | tag[7];
                        uint32_t tag_size = (tag[8] << 24) | (tag[9] << 16) | (tag[10] << 8) | tag[11];
        
                        if (tag_ofs + tag_size > static_cast<uint32_t>(icc_profile->size) || tag_size < 12)
                        {
                            return false;
                        } // invalid tag offset or size
        
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

        // same as freeimage's internal SwapRedBlue32(), which the linux shared library does not export
        bool swap_red_blue(FIBITMAP* bitmap)
        {
            if (FreeImage_GetImageType(bitmap) != FIT_BITMAP)
            {
                return false;
            }

            const uint32_t bytes_per_pixel = FreeImage_GetBPP(bitmap) / 8;
            if (bytes_per_pixel != 3 && bytes_per_pixel != 4)
            {
                return false;
            }

            const uint32_t width  = FreeImage_GetWidth(bitmap);
            const uint32_t height = FreeImage_GetHeight(bitmap);
            for (uint32_t y = 0; y < height; y++)
            {
                BYTE* line = FreeImage_GetScanLine(bitmap, y);
                for (uint32_t x = 0; x < width; x++)
                {
                    std::swap(line[x * bytes_per_pixel + 0], line[x * bytes_per_pixel + 2]);
                }
            }

            return true;
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

        // converts to 8 bits, high-color and monochrome sources end up greyscale, palettized sources stay palettized
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
        
            // 32 bit rgb is not a valid color attachment on most gpus, so keep the depth and convert to rgba
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
                    if (!swap_red_blue(bitmap))
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
                            {
                                return true;
                            }
        
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
                            {
                                return true;
                            }
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
                        if (alpha != 0xFFFF)
                        {
                            return true;
                        }
        
                        bits += 4; // move to next pixel (4 words for RGBA16)
                    }
                }
            }
            // Other formats might not have transparency or are treated as fully opaque
        
            return false;
        }


        float half_to_float_scalar(uint16_t half)
        {
            uint32_t mant = half & 0x3FFu;
            int32_t exp   = (half >> 10) & 0x1F;
            uint32_t sign = (half >> 15) & 0x1u;

            uint32_t f;
            if (exp == 0)
            {
                if (mant == 0)
                {
                    f = sign << 31;
                }
                else
                {
                    exp = 1;
                    while ((mant & 0x400u) == 0)
                    {
                        mant <<= 1;
                        exp--;
                    }
                    mant &= 0x3FFu;
                    f = (sign << 31) | (static_cast<uint32_t>(exp + (127 - 15)) << 23) | (mant << 13);
                }
            }
            else if (exp == 0x1F)
            {
                f = (sign << 31) | (0x7F800000) | (mant << 13);
            }
            else
            {
                f = (sign << 31) | (static_cast<uint32_t>(exp + (127 - 15)) << 23) | (mant << 13);
            }

            float value = 0.0f;
            memcpy(&value, &f, sizeof(value));
            return value;
        }

        float half_to_float(uint16_t half)
        {
            static const array<float, 65536> lookup_table = []()
            {
                array<float, 65536> values = {};
                for (uint32_t i = 0; i < static_cast<uint32_t>(values.size()); i++)
                {
                    values[i] = half_to_float_scalar(static_cast<uint16_t>(i));
                }
                return values;
            }();

            return lookup_table[half];
        }

        float saturate_float(float value)
        {
            if (!(value > 0.0f))
            {
                return 0.0f;
            }

            if (value >= 1.0f)
            {
                return 1.0f;
            }

            return value;
        }

        float get_sdr_dither(uint32_t x, uint32_t y)
        {
            static constexpr uint8_t bayer_8x8[64] =
            {
                 0, 48, 12, 60,  3, 51, 15, 63,
                32, 16, 44, 28, 35, 19, 47, 31,
                 8, 56,  4, 52, 11, 59,  7, 55,
                40, 24, 36, 20, 43, 27, 39, 23,
                 2, 50, 14, 62,  1, 49, 13, 61,
                34, 18, 46, 30, 33, 17, 45, 29,
                10, 58,  6, 54,  9, 57,  5, 53,
                42, 26, 38, 22, 41, 25, 37, 21
            };

            return (static_cast<float>(bayer_8x8[(y & 7) * 8 + (x & 7)]) + 0.5f) / 64.0f - 0.5f;
        }

        BYTE float_to_unorm8_dithered(float value, float dither, float strength)
        {
            // Dither quantization between endpoints without lifting true black
            // or pulling clipped white below full scale.
            if (!(value > 0.0f))
                return 0;
            if (value >= 1.0f)
                return 255;

            value = saturate_float(value + dither * (strength / 255.0f));
            return static_cast<BYTE>(value * 255.0f + 0.5f);
        }

        bool save_srgb_png(FIBITMAP* bitmap, const string& file_path)
        {
            // FreeImage's PNG writer does not emit an sRGB chunk. Tag the encoded
            // pixels explicitly so color-managed viewers do not use monitor primaries.
            FIMEMORY* memory = FreeImage_OpenMemory();
            if (!memory)
                return false;

            BYTE* bytes = nullptr;
            DWORD size = 0;
            bool saved = false;
            if (FreeImage_SaveToMemory(FIF_PNG, bitmap, memory, PNG_Z_BEST_SPEED) &&
                FreeImage_AcquireMemory(memory, &bytes, &size) && size >= 33)
            {
                // PNG signature + IHDR = 33 bytes. sRGB intent 0 (perceptual),
                // including its length, type and CRC, must precede image data.
                static constexpr unsigned char srgb_chunk[] =
                {
                    0, 0, 0, 1, 's', 'R', 'G', 'B', 0, 0xae, 0xce, 0x1c, 0xe9
                };
                ofstream file(file_path, ios::binary | ios::trunc);
                file.write(reinterpret_cast<const char*>(bytes), 33);
                file.write(reinterpret_cast<const char*>(srgb_chunk), sizeof(srgb_chunk));
                file.write(reinterpret_cast<const char*>(bytes + 33), size - 33);
                file.close();
                saved = !file.fail();
            }
            FreeImage_CloseMemory(memory);
            return saved;
        }

        // decodes a display encoded rgba frame to linear Rec.709 in scRGB units, 1 is 80 nits
        vector<float> decode_to_scrgb(const void* data, const uint32_t width, const uint32_t height, const uint32_t bits_per_channel, const ImageColorSpace color_space)
        {
            const uint16_t* src_half = static_cast<const uint16_t*>(data);
            const float* src_float   = static_cast<const float*>(data);

            vector<float> converted(static_cast<size_t>(width) * height * 4);
            ThreadPool::ParallelLoop([&](uint32_t row_start, uint32_t row_end)
            {
                for (uint32_t y = row_start; y < row_end; y++)
                {
                    for (uint32_t x = 0; x < width; x++)
                    {
                        const size_t index = (static_cast<size_t>(y) * width + x) * 4;

                        converted[index + 0] = bits_per_channel == 16 ? half_to_float(src_half[index + 0]) : src_float[index + 0];
                        converted[index + 1] = bits_per_channel == 16 ? half_to_float(src_half[index + 1]) : src_float[index + 1];
                        converted[index + 2] = bits_per_channel == 16 ? half_to_float(src_half[index + 2]) : src_float[index + 2];
                        converted[index + 3] = bits_per_channel == 16 ? half_to_float(src_half[index + 3]) : src_float[index + 3];

                        float* rgb = converted.data() + index;
                        if (color_space == ImageColorSpace::Srgb)
                        {
                            for (uint32_t c = 0; c < 3; c++)
                            {
                                rgb[c] = rgb[c] <= 0.04045f ? rgb[c] / 12.92f : pow((rgb[c] + 0.055f) / 1.055f, 2.4f);
                            }
                        }
                        else if (color_space == ImageColorSpace::Hdr10)
                        {
                            // ST.2084 to linear Rec.2020, expressed in scRGB units.
                            for (uint32_t c = 0; c < 3; c++)
                            {
                                const float p = pow(saturate_float(rgb[c]), 1.0f / 78.84375f);
                                rgb[c] = pow(max(p - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * p), 1.0f / 0.1593017578125f) * 125.0f;
                            }
                            const float r = rgb[0], g = rgb[1], b = rgb[2];
                            // Retain negative and >1 values: they carry HDR/wide gamut.
                            rgb[0] =  1.6604910f * r - 0.5876411f * g - 0.0728499f * b;
                            rgb[1] = -0.1245505f * r + 1.1328999f * g - 0.0083494f * b;
                            rgb[2] = -0.0181508f * r - 0.1005789f * g + 1.1187297f * b;
                        }
                    }
                }
            }, height);

            return converted;
        }

        bool save_jpeg_to_memory(FIBITMAP* bitmap, vector<uint8_t>& jpeg)
        {
            FIMEMORY* memory = FreeImage_OpenMemory();
            if (!memory)
            {
                return false;
            }

            // no chroma subsampling, it smears the edges of saturated highlights and a gain map is not chroma at all
            BYTE* bytes      = nullptr;
            DWORD size       = 0;
            const bool saved = FreeImage_SaveToMemory(FIF_JPEG, bitmap, memory, 95 | JPEG_SUBSAMPLING_444 | JPEG_OPTIMIZE) && FreeImage_AcquireMemory(memory, &bytes, &size) && size > 4;
            if (saved)
            {
                jpeg.assign(bytes, bytes + size);
            }
            FreeImage_CloseMemory(memory);
            return saved;
        }

        // where further app segments go, past the soi marker and the jfif header that has to follow it
        size_t get_jpeg_metadata_offset(const vector<uint8_t>& jpeg)
        {
            size_t offset = 2;
            if (jpeg.size() > 6 && jpeg[2] == 0xFF && jpeg[3] == 0xE0)
            {
                offset += 2 + ((static_cast<size_t>(jpeg[4]) << 8) | jpeg[5]);
            }
            return min(offset, jpeg.size());
        }

        void append_big_endian(vector<uint8_t>& bytes, const uint32_t value, const uint32_t byte_count)
        {
            for (uint32_t i = 0; i < byte_count; i++)
            {
                bytes.push_back(static_cast<uint8_t>(value >> ((byte_count - 1 - i) * 8)));
            }
        }

        // an app1 segment holding an xmp packet
        vector<uint8_t> make_jpeg_xmp_segment(const string& xmp)
        {
            static constexpr char xmp_namespace[] = "http://ns.adobe.com/xap/1.0/";

            vector<uint8_t> segment = { 0xFF, 0xE1 };
            append_big_endian(segment, static_cast<uint32_t>(2 + sizeof(xmp_namespace) + xmp.size()), 2);
            segment.insert(segment.end(), xmp_namespace, xmp_namespace + sizeof(xmp_namespace));
            segment.insert(segment.end(), xmp.begin(), xmp.end());
            return segment;
        }

        // marker 2, length 2, signature 4, byte order mark 4, ifd offset 4, tag count 2, three tags 36, next ifd offset 4, two entries 32
        const uint32_t jpeg_mpf_segment_size = 90;

        // an app2 multi picture format segment that indexes a primary image and the one image appended after it
        // segment_offset is where this segment starts in the file, mpf offsets count from its byte order mark
        vector<uint8_t> make_jpeg_mpf_segment(const uint32_t segment_offset, const uint32_t primary_size, const uint32_t secondary_size)
        {
            const uint32_t entry_offset = 50; // from the byte order mark, past the ifd

            vector<uint8_t> segment = { 0xFF, 0xE2 };
            append_big_endian(segment, jpeg_mpf_segment_size - 2, 2);
            segment.insert(segment.end(), { 'M', 'P', 'F', 0 });
            const uint32_t byte_order_offset = segment_offset + static_cast<uint32_t>(segment.size());
            segment.insert(segment.end(), { 'M', 'M', 0, 0x2A });
            append_big_endian(segment, 8, 4); // offset of the first ifd
            append_big_endian(segment, 3, 2); // tag count

            // version
            append_big_endian(segment, 0xB000, 2);
            append_big_endian(segment, 7, 2);
            append_big_endian(segment, 4, 4);
            segment.insert(segment.end(), { '0', '1', '0', '0' });

            // number of images
            append_big_endian(segment, 0xB001, 2);
            append_big_endian(segment, 4, 2);
            append_big_endian(segment, 1, 4);
            append_big_endian(segment, 2, 4);

            // entries, 16 bytes per image
            append_big_endian(segment, 0xB002, 2);
            append_big_endian(segment, 7, 2);
            append_big_endian(segment, 32, 4);
            append_big_endian(segment, entry_offset, 4);

            append_big_endian(segment, 0, 4); // no further ifd

            // the primary image, a baseline jpeg at the start of the file
            append_big_endian(segment, 0x030000, 4);
            append_big_endian(segment, primary_size, 4);
            append_big_endian(segment, 0, 4);
            append_big_endian(segment, 0, 4);

            // the image that follows it
            append_big_endian(segment, 0, 4);
            append_big_endian(segment, secondary_size, 4);
            append_big_endian(segment, primary_size - byte_order_offset, 4);
            append_big_endian(segment, 0, 4);

            SP_ASSERT(segment.size() == jpeg_mpf_segment_size);
            return segment;
        }
    }

    void ImageImporter::Initialize()
    {
        FreeImage_Initialise();
        FreeImage_SetOutputMessage(free_image_error_handler);
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

        // Reuse exactly the decoded/corrected pixels. Hash source bytes rather
        // than timestamps, so edits and identical images in other assets work
        // without relying on paths or the filesystem clock.
        constexpr uint32_t image_flags = RHI_Texture_Greyscale | RHI_Texture_Srgb | RHI_Texture_Transparent;
        generated_cache::Hash image_key;
        filesystem::path image_cache_path;
        if (slice_index == 0 && !World::GetResourceDirectory().empty())
        {
            try
            {
                ifstream source(file_path, ios::binary | ios::ate);
                const auto length = source.tellg();
                if (source && length > 0 && length <= 64 * 1024 * 1024)
                {
                    vector<uint8_t> encoded(static_cast<size_t>(length));
                    source.seekg(0);
                    if (source.read(reinterpret_cast<char*>(encoded.data()), static_cast<streamsize>(encoded.size())))
                    {
                        image_key.Add(uint32_t{1}); // bitmap corrections/resize algorithm
                        image_key.Add(texture->GetWidth()); image_key.Add(texture->GetHeight());
                        image_key.Add(texture->GetFlags() & image_flags);
                        image_key.Add(FileSystem::GetExtensionFromFilePath(file_path));
                        image_key.Add(generated_cache::HashBytes(encoded.data(), encoded.size()));
                        image_cache_path = generated_cache::Path(World::GetResourceDirectory(), "image_pixels", image_key.value);
                        vector<uint32_t> metadata;
                        vector<byte> pixels;
                        if (generated_cache::Load(image_cache_path, image_key.value, metadata, pixels) && metadata.size() == 3 &&
                            metadata[0] > 0 && metadata[0] <= 65536 && metadata[1] > 0 && metadata[1] <= 65536 &&
                            (metadata[2] & ~image_flags) == 0 && uint64_t(metadata[0]) * metadata[1] * 4 == pixels.size())
                        {
                            texture->SetWidth(metadata[0]); texture->SetHeight(metadata[1]);
                            texture->SetBitsPerChannel(8); texture->SetChannelCount(4);
                            texture->SetFormat(RHI_Format::R8G8B8A8_Unorm);
                            texture->SetFlags((texture->GetFlags() & ~image_flags) | metadata[2]);
                            texture->AllocateMip();
                            texture->GetMip(0, 0)->bytes = move(pixels);
                            return;
                        }
                    }
                }
            }
            catch (const exception&) { image_cache_path.clear(); }
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

                const auto& mip_data = dds_file.GetImageData(mip_index, 0);
                memcpy(&mip->bytes[0], mip_data->m_mem, mip->bytes.size());
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
        if (!image_cache_path.empty() && texture->GetFormat() == RHI_Format::R8G8B8A8_Unorm &&
            texture->GetBitsPerChannel() == 8 && texture->GetChannelCount() == 4 &&
            uint64_t(texture->GetWidth()) * texture->GetHeight() * 4 == mip->bytes.size())
        {
            const vector<uint32_t> metadata = {texture->GetWidth(), texture->GetHeight(), texture->GetFlags() & image_flags};
            generated_cache::Save(image_cache_path, image_key.value, metadata, mip->bytes);
        }
    }

    void ImageImporter::Save(const string& file_path, const uint32_t width, const uint32_t height, const uint32_t channel_count, const uint32_t bits_per_channel, void* data, ImageColorSpace color_space)
    {
        if (width == 0 || height == 0 || !data)
        {
            return;
        }

        if (channel_count != 4 || (bits_per_channel != 16 && bits_per_channel != 32))
        {
            SP_LOG_ERROR("EXR export expects RGBA half or full float data");
            return;
        }

        const vector<float> converted = decode_to_scrgb(data, width, height, bits_per_channel, color_space);

        FIBITMAP* bitmap = FreeImage_AllocateT(FIT_RGBAF, width, height, 128);
        if (!bitmap)
        {
            SP_LOG_ERROR("Failed to allocate FreeImage HDR bitmap");
            return;
        }

        for (uint32_t y = 0; y < height; y++)
        {
            float* scanline = reinterpret_cast<float*>(FreeImage_GetScanLine(bitmap, height - 1 - y));
            memcpy(scanline, converted.data() + static_cast<size_t>(y) * width * 4, static_cast<size_t>(width) * 4 * sizeof(float));
        }

        #if defined(EXR_NONE)
        constexpr int exr_save_flags = EXR_NONE;
        #else
        constexpr int exr_save_flags = EXR_DEFAULT;
        #endif

        BOOL saved = FreeImage_Save(FIF_EXR, bitmap, file_path.c_str(), exr_save_flags | (bits_per_channel == 32 ? EXR_FLOAT : 0));
        FreeImage_Unload(bitmap);

        if (!saved)
        {
            SP_LOG_ERROR("Failed to save HDR EXR to %s", file_path.c_str());
        }
    }

    bool ImageImporter::SaveHdrJpeg(const string& file_path, const uint32_t frame_width, const uint32_t frame_height, const uint32_t width, const uint32_t height, const void* sdr_data, const void* hdr_data, const uint32_t hdr_bits_per_channel, const ImageColorSpace hdr_color_space, const float hdr_white_nits)
    {
        if (frame_width == 0 || frame_height == 0 || width == 0 || height == 0 || !sdr_data || !hdr_data || !(hdr_white_nits > 0.0f))
        {
            return false;
        }

        if (hdr_bits_per_channel != 16 && hdr_bits_per_channel != 32)
        {
            SP_LOG_ERROR("HDR JPEG export expects RGBA half or full float data");
            return false;
        }

        // freeimage keeps 24 bit pixels in this order
        static constexpr uint32_t channels[3] = { FI_RGBA_RED, FI_RGBA_GREEN, FI_RGBA_BLUE };

        // the frame is rendered at the output resolution whatever shape the viewport has, so it is resized to the shape it is shown at
        const bool resize = width != frame_width || height != frame_height;
        auto resized = [&](FIBITMAP* bitmap)
        {
            if (!resize || !bitmap)
            {
                return bitmap;
            }

            // a triangle filter, sharper ones ring around highlights that are many times brighter than what surrounds them
            FIBITMAP* scaled = FreeImage_Rescale(bitmap, width, height, FILTER_BILINEAR);
            FreeImage_Unload(bitmap);
            return scaled;
        };

        // the sdr image, what a viewer that knows nothing about gain maps shows
        vector<uint8_t> base_jpeg;
        {
            FIBITMAP* bitmap = FreeImage_Allocate(frame_width, frame_height, 24);
            if (!bitmap)
            {
                SP_LOG_ERROR("Failed to allocate FreeImage SDR bitmap");
                return false;
            }

            const uint16_t* sdr_half = static_cast<const uint16_t*>(sdr_data);
            ThreadPool::ParallelLoop([&](uint32_t row_start, uint32_t row_end)
            {
                for (uint32_t y = row_start; y < row_end; y++)
                {
                    const uint16_t* src_row = sdr_half + static_cast<size_t>(y) * frame_width * 4;
                    BYTE* dst_row           = FreeImage_GetScanLine(bitmap, frame_height - 1 - y);
                    for (uint32_t x = 0; x < frame_width; x++)
                    {
                        for (uint32_t c = 0; c < 3; c++)
                        {
                            dst_row[x * 3 + channels[c]] = static_cast<BYTE>(saturate_float(half_to_float(src_row[x * 4 + c])) * 255.0f + 0.5f);
                        }
                    }
                }
            }, frame_height);

            bitmap = resized(bitmap);
            if (!bitmap)
            {
                SP_LOG_ERROR("Failed to resize the SDR image of %s", file_path.c_str());
                return false;
            }

            const bool encoded = save_jpeg_to_memory(bitmap, base_jpeg);
            FreeImage_Unload(bitmap);
            if (!encoded)
            {
                SP_LOG_ERROR("Failed to encode the SDR image of %s", file_path.c_str());
                return false;
            }
        }

        // the gain map is measured against the base as a viewer decodes it, so what the encode did to the base cancels out
        FIMEMORY* base_memory = FreeImage_OpenMemory(base_jpeg.data(), static_cast<DWORD>(base_jpeg.size()));
        FIBITMAP* base        = base_memory ? FreeImage_LoadFromMemory(FIF_JPEG, base_memory, JPEG_ACCURATE) : nullptr;
        if (base_memory)
        {
            FreeImage_CloseMemory(base_memory);
        }
        if (!base || FreeImage_GetBPP(base) != 24 || FreeImage_GetWidth(base) != width || FreeImage_GetHeight(base) != height)
        {
            if (base)
            {
                FreeImage_Unload(base);
            }
            SP_LOG_ERROR("Failed to decode the SDR image of %s", file_path.c_str());
            return false;
        }

        static const array<float, 256> srgb_to_linear = []()
        {
            array<float, 256> values = {};
            for (uint32_t i = 0; i < static_cast<uint32_t>(values.size()); i++)
            {
                const float encoded = static_cast<float>(i) / 255.0f;
                values[i]           = encoded <= 0.04045f ? encoded / 12.92f : pow((encoded + 0.055f) / 1.055f, 2.4f);
            }
            return values;
        }();

        // per channel, log2 of how much brighter the hdr frame is than the base, both in multiples of sdr white
        // the offsets keep the ratio stable near black, the buffer is reused, the decoded frame goes in and the gain comes out
        const float offset    = 1.0f / 64.0f;
        const float hdr_scale = 80.0f / hdr_white_nits;
        const float hdr_limit = 10000.0f / hdr_white_nits;
        vector<float> gain    = decode_to_scrgb(hdr_data, frame_width, frame_height, hdr_bits_per_channel, hdr_color_space);
        if (resize)
        {
            // resized as light, not as the encoded signal, the rows keep their order both ways since the filter is symmetric
            FIBITMAP* bitmap = FreeImage_AllocateT(FIT_RGBAF, frame_width, frame_height, 128);
            if (bitmap)
            {
                for (uint32_t y = 0; y < frame_height; y++)
                {
                    memcpy(FreeImage_GetScanLine(bitmap, y), gain.data() + static_cast<size_t>(y) * frame_width * 4, static_cast<size_t>(frame_width) * 4 * sizeof(float));
                }
                bitmap = resized(bitmap);
            }
            if (!bitmap)
            {
                FreeImage_Unload(base);
                SP_LOG_ERROR("Failed to resize the HDR image of %s", file_path.c_str());
                return false;
            }

            gain.resize(static_cast<size_t>(width) * height * 4);
            for (uint32_t y = 0; y < height; y++)
            {
                memcpy(gain.data() + static_cast<size_t>(y) * width * 4, FreeImage_GetScanLine(bitmap, y), static_cast<size_t>(width) * 4 * sizeof(float));
            }
            FreeImage_Unload(bitmap);
        }
        float gain_min        = numeric_limits<float>::max();
        float gain_max        = numeric_limits<float>::lowest();
        float hdr_peak        = 0.0f;
        mutex range_mutex;
        ThreadPool::ParallelLoop([&](uint32_t row_start, uint32_t row_end)
        {
            float range_min  = numeric_limits<float>::max();
            float range_max  = numeric_limits<float>::lowest();
            float range_peak = 0.0f;
            for (uint32_t y = row_start; y < row_end; y++)
            {
                const BYTE* base_row = FreeImage_GetScanLine(base, height - 1 - y);
                float* row           = gain.data() + static_cast<size_t>(y) * width * 4;
                for (uint32_t x = 0; x < width; x++)
                {
                    for (uint32_t c = 0; c < 3; c++)
                    {
                        // the base is srgb, colors outside its gamut are clipped to it, and a nan reads as black
                        float hdr         = row[x * 4 + c] * hdr_scale;
                        hdr               = hdr > 0.0f ? min(hdr, hdr_limit) : 0.0f;
                        const float sdr   = srgb_to_linear[base_row[x * 3 + channels[c]]];
                        const float value = log2((hdr + offset) / (sdr + offset));
                        row[x * 4 + c]    = value;
                        range_min         = min(range_min, value);
                        range_max         = max(range_max, value);
                        range_peak        = max(range_peak, hdr);
                    }
                }
            }

            lock_guard<mutex> lock(range_mutex);
            gain_min = min(gain_min, range_min);
            gain_max = max(gain_max, range_max);
            hdr_peak = max(hdr_peak, range_peak);
        }, height);
        FreeImage_Unload(base);

        // a flat map still needs a range to be stored over
        gain_max = max(gain_max, gain_min + 0.01f);

        // a display applies the whole map once it can go as far over sdr white as the frame does, and less of it below that
        const float capacity_max = max(log2(max(hdr_peak, 1.0f)), 0.01f);

        vector<uint8_t> gain_map_jpeg;
        {
            FIBITMAP* bitmap = FreeImage_Allocate(width, height, 24);
            if (!bitmap)
            {
                SP_LOG_ERROR("Failed to allocate FreeImage gain map bitmap");
                return false;
            }

            ThreadPool::ParallelLoop([&](uint32_t row_start, uint32_t row_end)
            {
                for (uint32_t y = row_start; y < row_end; y++)
                {
                    const float* src_row = gain.data() + static_cast<size_t>(y) * width * 4;
                    BYTE* dst_row        = FreeImage_GetScanLine(bitmap, height - 1 - y);
                    for (uint32_t x = 0; x < width; x++)
                    {
                        for (uint32_t c = 0; c < 3; c++)
                        {
                            dst_row[x * 3 + channels[c]] = static_cast<BYTE>(saturate_float((src_row[x * 4 + c] - gain_min) / (gain_max - gain_min)) * 255.0f + 0.5f);
                        }
                    }
                }
            }, height);

            const bool encoded = save_jpeg_to_memory(bitmap, gain_map_jpeg);
            FreeImage_Unload(bitmap);
            if (!encoded)
            {
                SP_LOG_ERROR("Failed to encode the gain map of %s", file_path.c_str());
                return false;
            }
        }

        // the layout and metadata are those of ultra hdr: the base image names the gain map appended after it, through an xmp
        // container directory and a multi picture index, and the gain map carries the hdrgm values that say how to apply it
        auto to_text = [](const float value)
        {
            ostringstream stream;
            stream.imbue(locale::classic());
            stream << fixed << setprecision(6) << value;
            return stream.str();
        };

        const string gain_map_xmp =
            "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Adobe XMP Core 5.1.2\">"
            "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
            "<rdf:Description rdf:about=\"\" xmlns:hdrgm=\"http://ns.adobe.com/hdr-gain-map/1.0/\""
            " hdrgm:Version=\"1.0\""
            " hdrgm:GainMapMin=\"" + to_text(gain_min) + "\""
            " hdrgm:GainMapMax=\"" + to_text(gain_max) + "\""
            " hdrgm:Gamma=\"1.000000\""
            " hdrgm:OffsetSDR=\"" + to_text(offset) + "\""
            " hdrgm:OffsetHDR=\"" + to_text(offset) + "\""
            " hdrgm:HDRCapacityMin=\"0.000000\""
            " hdrgm:HDRCapacityMax=\"" + to_text(capacity_max) + "\""
            " hdrgm:BaseRenditionIsHDR=\"False\"/>"
            "</rdf:RDF>"
            "</x:xmpmeta>";

        const vector<uint8_t> gain_map_xmp_segment = make_jpeg_xmp_segment(gain_map_xmp);
        const size_t gain_map_insert               = get_jpeg_metadata_offset(gain_map_jpeg);
        const size_t gain_map_size                 = gain_map_jpeg.size() + gain_map_xmp_segment.size();

        const string base_xmp =
            "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Adobe XMP Core 5.1.2\">"
            "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
            "<rdf:Description rdf:about=\"\""
            " xmlns:Container=\"http://ns.google.com/photos/1.0/container/\""
            " xmlns:Item=\"http://ns.google.com/photos/1.0/container/item/\""
            " xmlns:hdrgm=\"http://ns.adobe.com/hdr-gain-map/1.0/\""
            " hdrgm:Version=\"1.0\">"
            "<Container:Directory>"
            "<rdf:Seq>"
            "<rdf:li rdf:parseType=\"Resource\"><Container:Item Item:Semantic=\"Primary\" Item:Mime=\"image/jpeg\"/></rdf:li>"
            "<rdf:li rdf:parseType=\"Resource\"><Container:Item Item:Semantic=\"GainMap\" Item:Mime=\"image/jpeg\" Item:Length=\"" + to_string(gain_map_size) + "\"/></rdf:li>"
            "</rdf:Seq>"
            "</Container:Directory>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";

        const vector<uint8_t> base_xmp_segment = make_jpeg_xmp_segment(base_xmp);
        const size_t base_insert               = get_jpeg_metadata_offset(base_jpeg);
        const size_t base_size                 = base_jpeg.size() + base_xmp_segment.size() + jpeg_mpf_segment_size;
        const vector<uint8_t> mpf_segment      = make_jpeg_mpf_segment(static_cast<uint32_t>(base_insert + base_xmp_segment.size()), static_cast<uint32_t>(base_size), static_cast<uint32_t>(gain_map_size));

        ofstream file(file_path, ios::binary | ios::trunc);
        auto write = [&file](const vector<uint8_t>& bytes, const size_t begin, const size_t end)
        {
            file.write(reinterpret_cast<const char*>(bytes.data() + begin), static_cast<streamsize>(end - begin));
        };
        write(base_jpeg, 0, base_insert);
        write(base_xmp_segment, 0, base_xmp_segment.size());
        write(mpf_segment, 0, mpf_segment.size());
        write(base_jpeg, base_insert, base_jpeg.size());
        write(gain_map_jpeg, 0, gain_map_insert);
        write(gain_map_xmp_segment, 0, gain_map_xmp_segment.size());
        write(gain_map_jpeg, gain_map_insert, gain_map_jpeg.size());
        file.close();

        if (file.fail())
        {
            SP_LOG_ERROR("Failed to save HDR JPEG to %s", file_path.c_str());
            return false;
        }
        return true;
    }

    void ImageImporter::SaveSdrRgba8(const string& file_path, const uint32_t width, const uint32_t height, const void* data)
    {
        if (width == 0 || height == 0 || !data)
        {
            return;
        }

        // 32 bit so generated labels and decals keep their alpha
        FIBITMAP* bitmap = FreeImage_Allocate(width, height, 32);
        if (!bitmap)
        {
            SP_LOG_ERROR("Failed to allocate FreeImage SDR bitmap");
            return;
        }

        const uint8_t* source = static_cast<const uint8_t*>(data);
        for (uint32_t y = 0; y < height; y++)
        {
            BYTE* scanline      = FreeImage_GetScanLine(bitmap, height - 1 - y);
            const uint8_t* row  = source + static_cast<size_t>(y) * width * 4;
            for (uint32_t x = 0; x < width; x++)
            {
                // freeimage stores 32 bit bitmaps as bgra
                scanline[x * 4 + 0] = row[x * 4 + 2];
                scanline[x * 4 + 1] = row[x * 4 + 1];
                scanline[x * 4 + 2] = row[x * 4 + 0];
                scanline[x * 4 + 3] = row[x * 4 + 3];
            }
        }

        const bool saved = save_srgb_png(bitmap, file_path);
        FreeImage_Unload(bitmap);

        if (!saved)
        {
            SP_LOG_ERROR("Failed to save SDR PNG to %s", file_path.c_str());
        }
    }

    bool ImageImporter::LoadSdrRgba8(const string& file_path, uint32_t& width, uint32_t& height, vector<uint8_t>& rgba_out)
    {
        width  = 0;
        height = 0;
        rgba_out.clear();

        if (!FileSystem::Exists(file_path))
        {
            return false;
        }

        FREE_IMAGE_FORMAT format = FreeImage_GetFileType(file_path.c_str(), 0);
        if (format == FIF_UNKNOWN)
        {
            format = FreeImage_GetFIFFromFilename(file_path.c_str());
        }
        if (format == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(format))
        {
            return false;
        }

        FIBITMAP* bitmap = FreeImage_Load(format, file_path.c_str());
        if (!bitmap)
        {
            return false;
        }

        bitmap = apply_bitmap_corrections(bitmap);
        if (!bitmap)
        {
            return false;
        }

        if (FreeImage_GetBPP(bitmap) != 32)
        {
            bitmap = convert_to_32bits(bitmap);
            if (!bitmap)
            {
                return false;
            }
            if (FreeImage_GetRedMask(bitmap) == 0xff0000)
            {
                swap_red_blue(bitmap);
            }
        }

        width  = FreeImage_GetWidth(bitmap);
        height = FreeImage_GetHeight(bitmap);
        if (width == 0 || height == 0)
        {
            FreeImage_Unload(bitmap);
            return false;
        }

        rgba_out.resize(static_cast<size_t>(width) * height * 4);
        const uint32_t row_bytes = width * 4;
        for (uint32_t y = 0; y < height; y++)
        {
            memcpy(rgba_out.data() + static_cast<size_t>(y) * row_bytes, FreeImage_GetScanLine(bitmap, y), row_bytes);
        }

        FreeImage_Unload(bitmap);
        return true;
    }

    void ImageImporter::SaveSdr(const string& file_path, const uint32_t width, const uint32_t height, const uint32_t channel_count, const uint32_t bits_per_channel, void* data)
    {
        if (width == 0 || height == 0 || !data)
        {
            return;
        }

        // the half float path below reads two bytes per channel, an 8 bit buffer would be read out of bounds
        if (bits_per_channel == 8)
        {
            if (channel_count != 4)
            {
                SP_LOG_ERROR("SaveSdr expects 4 channels for 8 bit data");
                return;
            }

            SaveSdrRgba8(file_path, width, height, data);
            return;
        }

        if (channel_count != 4 || bits_per_channel != 16)
        {
            SP_LOG_ERROR("SaveSdr expects RGBA half float data");
            return;
        }

        const uint16_t* src_half = static_cast<const uint16_t*>(data);

        FIBITMAP* bitmap = FreeImage_Allocate(width, height, 24);
        if (!bitmap)
        {
            SP_LOG_ERROR("Failed to allocate FreeImage SDR bitmap");
            return;
        }

        vector<BYTE> converted(static_cast<size_t>(width) * height * 3);
        ThreadPool::ParallelLoop([&](uint32_t row_start, uint32_t row_end)
        {
            for (uint32_t y = row_start; y < row_end; y++)
            {
                const uint16_t* src_row = src_half + static_cast<size_t>(y) * width * 4;
                BYTE* dst_row           = converted.data() + static_cast<size_t>(y) * width * 3;

                for (uint32_t x = 0; x < width; x++)
                {
                    const uint32_t src_index = x * 4;
                    const uint32_t dst_index = x * 3;
                    const float r            = half_to_float(src_row[src_index + 0]);
                    const float g            = half_to_float(src_row[src_index + 1]);
                    const float b            = half_to_float(src_row[src_index + 2]);
                    const float peak         = max(max(r, g), b);
                    const float strength     = 1.0f + (1.0f - saturate_float(peak * 8.0f)) * 0.75f;
                    const float dither       = get_sdr_dither(x, y);

                    dst_row[dst_index + 0] = float_to_unorm8_dithered(b, dither, strength);
                    dst_row[dst_index + 1] = float_to_unorm8_dithered(g, dither, strength);
                    dst_row[dst_index + 2] = float_to_unorm8_dithered(r, dither, strength);
                }
            }
        }, height);

        for (uint32_t y = 0; y < height; y++)
        {
            BYTE* scanline = FreeImage_GetScanLine(bitmap, height - 1 - y);
            memcpy(scanline, converted.data() + static_cast<size_t>(y) * width * 3, static_cast<size_t>(width) * 3);
        }

        const bool saved = save_srgb_png(bitmap, file_path);
        FreeImage_Unload(bitmap);

        if (!saved)
        {
            SP_LOG_ERROR("Failed to save SDR PNG to %s", file_path.c_str());
        }
    }
}
