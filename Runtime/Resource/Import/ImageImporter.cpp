/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Spartan.h"
#include "ImageImporter.h"
#define FREEIMAGE_LIB
#include <FreeImage.h>
#include <Utilities.h>
#include "../../Threading/Threading.h"
#include "../../RHI/RHI_Texture2D.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    static FREE_IMAGE_FILTER filter_downsample = FREE_IMAGE_FILTER::FILTER_BOX;
    
    // A struct that rescaling threads will work with
    struct RescaleJob
    {
        uint32_t width         = 0;
        uint32_t height        = 0;
        uint32_t channel_count = 0;
        RHI_Texture_Mip* mip   = nullptr;
        bool done              = false;
        
        RescaleJob(const uint32_t width, const uint32_t height, const uint32_t channel_count)
        {
            this->width         = width;
            this->height        = height;
            this->channel_count = channel_count;
        }
    };
    
    static uint32_t get_bytes_per_channel(FIBITMAP* bitmap)
    {
        SP_ASSERT(bitmap != nullptr);
    
        const auto type = FreeImage_GetImageType(bitmap);
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
    
        return size;
    }

    static uint32_t get_channel_count(FIBITMAP* bitmap)
    {
        SP_ASSERT(bitmap != nullptr);
    
        const uint32_t bytes_per_pixel  = FreeImage_GetLine(bitmap) / FreeImage_GetWidth(bitmap);
        const uint32_t channel_count    = bytes_per_pixel / get_bytes_per_channel(bitmap);
    
        return channel_count;
    }

    static RHI_Format get_rhi_format(const uint32_t bytes_per_channel, const uint32_t channel_count)
    {
        const uint32_t bits_per_channel = bytes_per_channel * 8;
    
        if (channel_count == 1)
        {
            if (bits_per_channel == 8)  return RHI_Format_R8_Unorm;
        }
        else if (channel_count == 2)
        {
            if (bits_per_channel == 8)  return RHI_Format_R8G8_Unorm;
        }
        else if (channel_count == 3)
        {
            if (bits_per_channel == 32) return RHI_Format_R32G32B32A32_Float;
        }
        else if (channel_count == 4)
        {
            if (bits_per_channel == 8)  return RHI_Format_R8G8B8A8_Unorm;
            if (bits_per_channel == 16) return RHI_Format_R16G16B16A16_Float;
            if (bits_per_channel == 32) return RHI_Format_R32G32B32A32_Float;
        }
    
        LOG_ERROR("Could not deduce format");
        return RHI_Format_Undefined;
    }

    static FIBITMAP* convert_to_32bits(FIBITMAP* bitmap)
    {
        SP_ASSERT(bitmap != nullptr);

        const auto previous_bitmap = bitmap;
        bitmap = FreeImage_ConvertTo32Bits(previous_bitmap);
        if (!bitmap)
        {
            LOG_ERROR("Failed (%d bpp, %d channels).", FreeImage_GetBPP(previous_bitmap), get_channel_count(previous_bitmap));
            return nullptr;
        }

        FreeImage_Unload(previous_bitmap);
        return bitmap;
    }

    static FIBITMAP* rescale(FIBITMAP* bitmap, const uint32_t width, const uint32_t height)
    {
        SP_ASSERT(bitmap != nullptr);
        SP_ASSERT(width != 0);
        SP_ASSERT(height != 0);

        FIBITMAP* previous_bitmap = bitmap;
        bitmap = FreeImage_Rescale(previous_bitmap, width, height, filter_downsample);

        if (!bitmap)
        {
            LOG_ERROR("Failed");
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
    
        // Convert it to 32 bits (if lower)
        if (FreeImage_GetBPP(bitmap) < 32)
        {
            bitmap = convert_to_32bits(bitmap);
        }
    
        // Most GPUs can't use a 32 bit RGB texture as a color attachment.
        // Vulkan tells you, your GPU doesn't support it.
        // D3D11 seems to be doing some sort of emulation under the hood while throwing some warnings regarding sampling it.
        // So to prevent that, we maintain the 32 bits and convert to an RGBA format.
        const uint32_t image_bits_per_channel = get_bytes_per_channel(bitmap) * 8;
        const uint32_t image_channels         = get_channel_count(bitmap);
        const bool is_r32g32b32_float         = image_channels == 3 && image_bits_per_channel == 32;
        if (is_r32g32b32_float)
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
                    LOG_ERROR("Failed to swap red with blue channel");
                }
            }
        }
    
        // Flip it vertically
        FreeImage_FlipVertical(bitmap);
    
        return bitmap;
    }

    static void get_bits_from_bitmap(RHI_Texture_Mip* mip, FIBITMAP* bitmap, const uint32_t width, const uint32_t height, const uint32_t channel_count)
    {
        // Validate
        SP_ASSERT(mip != nullptr);
        SP_ASSERT(bitmap != nullptr);
        SP_ASSERT(width != 0);
        SP_ASSERT(height != 0);
        SP_ASSERT(channel_count != 0);

        // Compute expected data size and reserve enough memory
        const size_t size_bytes = width * height * channel_count * get_bytes_per_channel(bitmap);
        if (size_bytes != mip->bytes.size())
        {
            mip->bytes.clear();
            mip->bytes.reserve(size_bytes);
            mip->bytes.resize(size_bytes);
        }

        // Copy the data over to our vector
        BYTE* bits = FreeImage_GetBits(bitmap);
        memcpy(&mip->bytes[0], bits, size_bytes);
    }

    static void generate_mips(Context* context, FIBITMAP* bitmap, RHI_Texture* texture, uint32_t width, uint32_t height, uint32_t channels, const uint32_t slice_index)
    {
        SP_ASSERT(texture != nullptr);

        // Create a RescaleJob for every mip that we need
        vector<RescaleJob> jobs;
        while (width > 1 && height > 1)
        {
            width  = Math::Helper::Max(width / 2, 1U);
            height = Math::Helper::Max(height / 2, 1U);
            jobs.emplace_back(width, height, channels);

            // Resize the RHI_Texture vector accordingly
            const auto size = width * height * channels;
            RHI_Texture_Mip& mip = texture->CreateMip(slice_index);
            mip.bytes.reserve(size);
            mip.bytes.resize(size);
        }

        // Pass data pointers (now that the RHI_Texture mip vector has been constructed)
        for (uint32_t i = 0; i < jobs.size(); i++)
        {
            // reminder: i + 1 because the 0 mip is the default image size
            jobs[i].mip = &texture->GetMip(0, i + 1);
        }

        // Parallelize mipmap generation using multiple threads (because FreeImage_Rescale() is expensive)
        Threading* threading = context->GetSubsystem<Threading>();
        for (auto& job : jobs)
        {
            threading->AddTask([&job, &bitmap]()
            {
                FIBITMAP* bitmap_scaled = FreeImage_Rescale(bitmap, job.width, job.height, filter_downsample);
                get_bits_from_bitmap(job.mip, bitmap_scaled, job.width, job.height, job.channel_count);
                FreeImage_Unload(bitmap_scaled);
                job.done = true;
            });
        }

        // Wait until all mips have been generated
        auto ready = false;
        while (!ready)
        {
            ready = true;
            for (const auto& job : jobs)
            {
                if (!job.done)
                {
                    ready = false;
                }
            }

            this_thread::sleep_for(chrono::milliseconds(16));
        }
    }

    ImageImporter::ImageImporter(Context* context)
    {
        // Initialize
        m_context = context;
        FreeImage_Initialise();

        // Register error handler
        const auto free_image_error_handler = [](const FREE_IMAGE_FORMAT fif, const char* message)
        {
            const auto text     = (message != nullptr) ? message : "Unknown error";
            const auto format   = (fif != FIF_UNKNOWN) ? FreeImage_GetFormatFromFIF(fif) : "Unknown";
            
            LOG_ERROR("%s, Format: %s", text, format);
        };
        FreeImage_SetOutputMessage(free_image_error_handler);

        // Get version
        m_context->GetSubsystem<Settings>()->RegisterThirdPartyLib("FreeImage", FreeImage_GetVersion(), "http://freeimage.sourceforge.net/download.html");
    }

    ImageImporter::~ImageImporter()
    {
        FreeImage_DeInitialise();
    }

    bool ImageImporter::Load(const string& file_path, const uint32_t slice_index, RHI_Texture* texture)
    {
        SP_ASSERT(texture != nullptr);

        if (!FileSystem::Exists(file_path))
        {
            LOG_ERROR("Path \"%s\" is invalid.", file_path.c_str());
            return false;
        }

        // Acquire image format
        FREE_IMAGE_FORMAT format = FreeImage_GetFileType(file_path.c_str(), 0);
        format                   = (format == FIF_UNKNOWN) ? FreeImage_GetFIFFromFilename(file_path.c_str()) : format;  // If the format is unknown, try to work it out from the file path
        if (!FreeImage_FIFSupportsReading(format)) // If the format is still unknown, give up
        {
            LOG_ERROR("Unsupported format");
            return false;
        }

        // Load the image
        auto bitmap = FreeImage_Load(format, file_path.c_str());
        if (!bitmap)
        {
            LOG_ERROR("Failed to load \"%s\"", file_path.c_str());
            return false;
        }

        // Deduce image properties. Important that this is done here, before ApplyBitmapCorrections(), as after that, results for grayscale seem to be always false
        const bool image_is_transparent = FreeImage_IsTransparent(bitmap);
        const bool image_is_grayscale   = FreeImage_GetColorType(bitmap) == FREE_IMAGE_COLOR_TYPE::FIC_MINISBLACK;

        // Perform some fix ups
        bitmap = apply_bitmap_corrections(bitmap);
        if (!bitmap)
        {
            LOG_ERROR("Failed to apply bitmap corrections");
            return false;
        }

        // Deduce image properties
        const uint32_t bytes_per_channel = get_bytes_per_channel(bitmap);
        const uint32_t channel_count     = get_channel_count(bitmap);
        const RHI_Format image_format    = get_rhi_format(bytes_per_channel, channel_count);

        // Perform any scaling (if necessary)
        const bool user_define_dimensions = (texture->GetWidth() != 0 && texture->GetHeight() != 0);
        const bool dimension_mismatch     = (FreeImage_GetWidth(bitmap) != texture->GetWidth() && FreeImage_GetHeight(bitmap) != texture->GetHeight());
        const bool scale                  = user_define_dimensions && dimension_mismatch;
        bitmap                            = scale ? rescale(bitmap, texture->GetWidth(), texture->GetHeight()) : bitmap;

        // Deduce image properties
        const unsigned int image_width  = FreeImage_GetWidth(bitmap);
        const unsigned int image_height = FreeImage_GetHeight(bitmap);

        // Fill RGBA vector with the data from the FIBITMAP
        RHI_Texture_Mip& mip = texture->CreateMip(slice_index);
        get_bits_from_bitmap(&mip, bitmap, image_width, image_height, channel_count);

        // If the texture requires mips, generate them
        if (texture->GetFlags() & RHI_Texture_GenerateMipsWhenLoading)
        {
            generate_mips(m_context, bitmap, texture, image_width, image_height, channel_count, slice_index);
        }

        // Free memory 
        FreeImage_Unload(bitmap);

        // Fill RHI_Texture with image properties
        texture->SetBitsPerChannel(bytes_per_channel * 8);
        texture->SetWidth(image_width);
        texture->SetHeight(image_height);
        texture->SetChannelCount(channel_count);
        texture->SetTransparency(image_is_transparent);
        texture->SetFormat(image_format);
        texture->SetGrayscale(image_is_grayscale);

        return true;
    }
}
