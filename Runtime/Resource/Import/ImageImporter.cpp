/*
Copyright(c) 2016-2020 Panos Karabelas

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

#define FREEIMAGE_LIB

//= INCLUDES =========================
#include "ImageImporter.h"
#include <FreeImage.h>
#include <Utilities.h>
#include "../../Threading/Threading.h"
#include "../../Core/Settings.h"
#include "../../Math/MathHelper.h"
#include "../../RHI/RHI_Texture2D.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace _ImagImporter
{
	static FREE_IMAGE_FILTER rescale_filter = FILTER_LANCZOS3;

	// A struct that rescaling threads will work with
	struct RescaleJob
	{
		uint32_t width		    = 0;
		uint32_t height		    = 0;
		uint32_t channels	    = 0;
		vector<std::byte>* data	= nullptr;
		bool done			    = false;

		RescaleJob(const uint32_t width, const uint32_t height, const uint32_t channels)
		{
			this->width		= width;
			this->height	= height;
			this->channels	= channels;
		}
	};
}

namespace Spartan
{
	ImageImporter::ImageImporter(Context* context)
	{
		// Initialize
		m_context = context;
		FreeImage_Initialise();

		// Register error handler
		const auto free_image_error_handler = [](const FREE_IMAGE_FORMAT fif, const char* message)
		{
			const auto text		= (message != nullptr) ? message : "Unknown error";
			const auto format	= (fif != FIF_UNKNOWN) ? FreeImage_GetFormatFromFIF(fif) : "Unknown";
			
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

	bool ImageImporter::Load(const string& file_path, RHI_Texture* texture, const bool generate_mipmaps /*= true*/)
	{
		if (!texture)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (!FileSystem::Exists(file_path))
		{
			LOG_ERROR("Path \"%s\" is invalid.", file_path.c_str());
			return false;
		}

		// Acquire image format
		auto format	= FreeImage_GetFileType(file_path.c_str(), 0);
		format		= (format == FIF_UNKNOWN) ? FreeImage_GetFIFFromFilename(file_path.c_str()) : format;  // If the format is unknown, try to get it from the the filename	
		if (!FreeImage_FIFSupportsReading(format)) // If the format is still unknown, give up
		{
			LOG_ERROR("Unsupported format");
			return false;
		}

		// Load the image
		auto bitmap = FreeImage_Load(format, file_path.c_str());
        if (!bitmap)
        {
            LOG_ERROR("Failed to load");
            return false;
        }

        // Deduce image properties. Important that this is done here, before ApplyBitmapCorrections(), as after that, results for grayscale seemed to be always false
        const bool image_is_transparent = FreeImage_IsTransparent(bitmap);
        const bool image_is_grayscale   = FreeImage_GetColorType(bitmap) == FIC_MINISBLACK;

		// Perform some fix ups
		bitmap = ApplyBitmapCorrections(bitmap);
        if (!bitmap)
        {
            LOG_ERROR("Failed to apply bitmap corrections");
            return false;
        }

        // Deduce image properties
        const unsigned int image_bpp            = FreeImage_GetBPP(bitmap);
        const uint32_t image_bytes_per_channel  = ComputeBitsPerChannel(bitmap) * 8;
        const uint32_t image_channels           = ComputeChannelCount(bitmap);
        const RHI_Format image_format           = ComputeTextureFormat(image_bytes_per_channel, image_channels);

		// Perform any scaling (if necessary)
		const auto user_define_dimensions	= (texture->GetWidth() != 0 && texture->GetHeight() != 0);
		const auto dimension_mismatch		= (FreeImage_GetWidth(bitmap) != texture->GetWidth() && FreeImage_GetHeight(bitmap) != texture->GetHeight());
		const auto scale					= user_define_dimensions && dimension_mismatch;
		bitmap								= scale ? _FreeImage_Rescale(bitmap, texture->GetWidth(), texture->GetHeight()) : bitmap;

		// Deduce image properties
		const unsigned int image_width	= FreeImage_GetWidth(bitmap);
		const unsigned int image_height = FreeImage_GetHeight(bitmap);

		// Fill RGBA vector with the data from the FIBITMAP
		const auto mip = texture->AddMipmap();
		GetBitsFromFibitmap(mip, bitmap, image_width, image_height, image_channels);

		// If the texture supports mipmaps, generate them
		if (generate_mipmaps)
		{
			GenerateMipmaps(bitmap, texture, image_width, image_height, image_channels);
		}

		// Free memory 
		FreeImage_Unload(bitmap);

		// Fill RHI_Texture with image properties
		texture->SetBpp(image_bpp);
		texture->SetBpc(image_bytes_per_channel);
		texture->SetWidth(image_width);
		texture->SetHeight(image_height);
		texture->SetChannels(image_channels);
		texture->SetTransparency(image_is_transparent);
		texture->SetFormat(image_format);
		texture->SetGrayscale(image_is_grayscale);

		return true;
	}

	bool ImageImporter::GetBitsFromFibitmap(vector<std::byte>* data, FIBITMAP* bitmap, const uint32_t width, const uint32_t height, const uint32_t channels)
	{
		if (!data || width == 0 || height == 0 || channels == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Compute expected data size and reserve enough memory
		const auto size = width * height * channels * ComputeBitsPerChannel(bitmap);
		if (size != data->size())
		{
			data->clear();
			data->reserve(size);
			data->resize(size);
		}

		// Copy the data over to our vector
		const auto bits = FreeImage_GetBits(bitmap);
		memcpy(&(*data)[0], bits, size);

		return true;
	}

	void ImageImporter::GenerateMipmaps(FIBITMAP* bitmap, RHI_Texture* texture, uint32_t width, uint32_t height, uint32_t channels)
	{
		if (!texture)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}
	
		// Create a RescaleJob for every mip that we need
		vector<_ImagImporter::RescaleJob> jobs;
		while (width > 1 || height > 1)
		{
			width	= Math::Max(width / 2, static_cast<uint32_t>(1));
			height	= Math::Max(height / 2, static_cast<uint32_t>(1));
			jobs.emplace_back(width, height, channels);
			
			// Resize the RHI_Texture vector accordingly
			const auto size = width * height * channels;
			auto mip		= texture->AddMipmap();
			mip->reserve(size);
			mip->resize(size);
		}

		// Pass data pointers (now that the RHI_Texture mip vector has been constructed)
		for (uint32_t i = 0; i < jobs.size(); i++)
		{
			// reminder: i + 1 because the 0 mip is the default image size
			jobs[i].data = texture->GetData(i + 1);
		}

		// Parallelize mipmap generation using multiple threads (because FreeImage_Rescale() using FILTER_LANCZOS3 is expensive)
		auto threading = m_context->GetSubsystem<Threading>();
		for (auto& job : jobs)
		{
			threading->AddTask([this, &job, &bitmap]()
			{
				const auto bitmap_scaled = FreeImage_Rescale(bitmap, job.width, job.height, _ImagImporter::rescale_filter);
				if (!GetBitsFromFibitmap(job.data, bitmap_scaled, job.width, job.height, job.channels))
				{
					LOG_ERROR("Failed to create mip level %dx%d", job.width, job.height);
				}
				FreeImage_Unload(bitmap_scaled);
				job.done = true;
			});
		}

		// Wait until all mipmaps have been generated
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
		}
	}

	uint32_t ImageImporter::ComputeChannelCount(FIBITMAP* bitmap) const
	{	
		if (!bitmap)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return 0;
		}

		const auto bytes_per_pixel = FreeImage_GetLine(bitmap) / FreeImage_GetWidth(bitmap);
		const auto channels = bytes_per_pixel / ComputeBitsPerChannel(bitmap);

		return channels;
	}

	uint32_t ImageImporter::ComputeBitsPerChannel(FIBITMAP* bitmap) const
	{
		if (!bitmap)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return 0;
		}

		const auto type	= FreeImage_GetImageType(bitmap);
		uint32_t size	= 0;

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

	RHI_Format ImageImporter::ComputeTextureFormat(const uint32_t bytes_per_channel, const uint32_t channels) const
	{
        if (channels == 1)
        {
            if (bytes_per_channel == 8)	return RHI_Format_R8_Unorm;
        }
        else if (channels == 2)
        {
            if (bytes_per_channel == 8)	return RHI_Format_R8G8_Unorm;
        }
		else if (channels == 3)
		{
			if (bytes_per_channel == 32)	return RHI_Format_R32G32B32_Float;
		}
		else if (channels == 4)
		{
			if (bytes_per_channel == 8)     return RHI_Format_R8G8B8A8_Unorm;
			if (bytes_per_channel == 16)	return RHI_Format_R16G16B16A16_Float;
			if (bytes_per_channel == 32)    return RHI_Format_R32G32B32A32_Float;
		}
		
		LOG_ERROR_INVALID_PARAMETER();
		return RHI_Format_R8_Unorm;
	}

	FIBITMAP* ImageImporter::ApplyBitmapCorrections(FIBITMAP* bitmap) const
	{
		if (!bitmap)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return nullptr;
		}

        // Convert to a standard bitmap. FIT_UINT16 and FIT_RGBA16 are processed without errors
        // but show up empty in the editor. For now, we convert everything to a standard bitmap.
        FREE_IMAGE_TYPE type = FreeImage_GetImageType(bitmap);
        if (type != FIT_BITMAP)
        {
            // FreeImage can't convert that
            if (type != FIT_RGBF)
            {
                auto previous_bitmap = bitmap;
                bitmap = FreeImage_ConvertToType(bitmap, FIT_BITMAP);
                FreeImage_Unload(previous_bitmap);
            }
        }

		// Convert it to 32 bits (if lower)
		if (FreeImage_GetBPP(bitmap) < 32)
		{
			bitmap = _FreeImage_ConvertTo32Bits(bitmap);
		}

		// Convert BGR to RGB (if needed)
		if (FreeImage_GetBPP(bitmap) == 32)
		{
            if (FreeImage_GetRedMask(bitmap) == 0xff0000 && ComputeChannelCount(bitmap) >= 2)
            {
                const bool swapped = SwapRedBlue32(bitmap);
                if (!swapped)
                {
                    LOG_ERROR("Failed to swap red with blue channel");
                }
            }
		}
   
		// Flip it vertically
		FreeImage_FlipVertical(bitmap);

		return bitmap;
	}

	FIBITMAP* ImageImporter::_FreeImage_ConvertTo32Bits(FIBITMAP* bitmap) const
	{
		if (!bitmap)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return nullptr;
		}

		const auto previous_bitmap	= bitmap;
		bitmap						= FreeImage_ConvertTo32Bits(previous_bitmap);
		if (!bitmap)
        {
            LOG_ERROR("Failed (%d bpp, %d channels).", FreeImage_GetBPP(previous_bitmap), ComputeChannelCount(previous_bitmap));
			return nullptr;
		}

		FreeImage_Unload(previous_bitmap);
		return bitmap;
	}

	FIBITMAP* ImageImporter::_FreeImage_Rescale(FIBITMAP* bitmap, const uint32_t width, const uint32_t height) const
	{
		if (!bitmap || width == 0 || height == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return nullptr;
		}

		const auto previous_bitmap	= bitmap;
		bitmap						= FreeImage_Rescale(previous_bitmap, width, height, _ImagImporter::rescale_filter);
		if (!bitmap)
		{
			LOG_ERROR("Failed");
			return previous_bitmap;
		}

		FreeImage_Unload(previous_bitmap);
		return bitmap;
	}
}
