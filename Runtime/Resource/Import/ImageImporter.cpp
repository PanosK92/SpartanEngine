/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "../../RHI/RHI_Texture.h"
#include "../../Math/MathHelper.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace _ImagImporter
{
	FREE_IMAGE_FILTER rescaleFilter = FILTER_LANCZOS3;

	// A struct that rescaling threads will work with
	struct RescaleJob
	{
		unsigned int width		= 0;
		unsigned int height		= 0;
		unsigned int channels	= 0;
		vector<byte>* data		= nullptr;
		bool done				= false;

		RescaleJob(unsigned int width, unsigned int height, unsigned int channels)
		{
			this->width		= width;
			this->height	= height;
			this->channels	= channels;
		}
	};
}

namespace Directus
{
	ImageImporter::ImageImporter(Context* context)
	{
		// Initialize
		m_context = context;
		FreeImage_Initialise();

		// Register error handler
		auto FreeImageErrorHandler = [](FREE_IMAGE_FORMAT fif, const char* message)
		{
			const char* text	= (message != nullptr) ? message : "Unknown error";
			const char* format	= (fif != FIF_UNKNOWN) ? FreeImage_GetFormatFromFIF(fif) : "Unknown";
			
			LOGF_ERROR("%s, Format: %s", text, format);
		};
		FreeImage_SetOutputMessage(FreeImageErrorHandler);

		// Get version
		Settings::Get().m_versionFreeImage = FreeImage_GetVersion();
	}

	ImageImporter::~ImageImporter()
	{
		FreeImage_DeInitialise();
	}

	bool ImageImporter::Load(const string& filePath, RHI_Texture* texture)
	{
		if (!texture)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (!FileSystem::FileExists(filePath))
		{
			LOGF_ERROR("Path \"%s\" is invalid.", filePath.c_str());
			return false;
		}

		// Acquire image format
		FREE_IMAGE_FORMAT format	= FreeImage_GetFileType(filePath.c_str(), 0);
		format						= (format == FIF_UNKNOWN) ? FreeImage_GetFIFFromFilename(filePath.c_str()) : format;  // If the format is unknown, try to get it from the the filename	
		if (!FreeImage_FIFSupportsReading(format)) // If the format is still unknown, give up
		{
			LOGF_ERROR("Unknown or unsupported format.");
			return false;
		}

		// Load the image
		FIBITMAP* bitmap = FreeImage_Load(format, filePath.c_str());
	
		// Perform some fix ups
		bitmap = ApplyBitmapCorrections(bitmap);
		if (!bitmap)
			return false;

		// Perform any scaling (if necessary)
		bool userDefineDimensions	= (texture->GetWidth() != 0 && texture->GetHeight() != 0);
		bool dimensionMismatch		= (FreeImage_GetWidth(bitmap) != texture->GetWidth() && FreeImage_GetHeight(bitmap) != texture->GetHeight());
		bool scale					= userDefineDimensions && dimensionMismatch;
		bitmap						= scale ? _FreeImage_Rescale(bitmap, texture->GetWidth(), texture->GetHeight()) : bitmap;

		// Deduce image properties	
		bool image_transparency				= FreeImage_IsTransparent(bitmap);
		unsigned int image_width			= FreeImage_GetWidth(bitmap);
		unsigned int image_height			= FreeImage_GetHeight(bitmap);
		unsigned int image_bpp				= FreeImage_GetBPP(bitmap);
		unsigned int image_byesPerChannel	= ComputeBitsPerChannel(bitmap);
		unsigned int image_channels			= ComputeChannelCount(bitmap);
		RHI_Format image_format				= ComputeTextureFormat(image_bpp, image_channels);
		bool image_grayscale				= IsVisuallyGrayscale(bitmap);

		// Fill RGBA vector with the data from the FIBITMAP
		auto mip = texture->Data_AddMipLevel();
		GetBitsFromFIBITMAP(mip, bitmap, image_width, image_height, image_channels);

		// If the texture requires mip-maps, generate them
		if (texture->GetNeedsMipChain())
		{
			GenerateMipmaps(bitmap, texture, image_width, image_height, image_channels);
		}

		// Free memory 
		FreeImage_Unload(bitmap);

		// Fill RHI_Texture with image properties
		texture->SetBPP(image_bpp);
		texture->SetBPC(image_byesPerChannel);
		texture->SetWidth(image_width);
		texture->SetHeight(image_height);
		texture->SetChannels(image_channels);
		texture->SetTransparency(image_transparency);
		texture->SetFormat(image_format);
		texture->SetGrayscale(image_grayscale);

		return true;
	}

	bool ImageImporter::GetBitsFromFIBITMAP(vector<byte>* data, FIBITMAP* bitmap, unsigned int width, unsigned int height, unsigned int channels)
	{
		if (!data || width == 0 || height == 0 || channels == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Compute expected data size and reserve enough memory
		unsigned int size = width * height * channels *  (ComputeBitsPerChannel(bitmap) / 8);
		if (size != data->size())
		{
			data->clear();
			data->reserve(size);
			data->resize(size);
		}

		// Copy the data over to our vector
		auto bits = FreeImage_GetBits(bitmap);
		memcpy(&(*data)[0], bits, size);

		return true;
	}

	void ImageImporter::GenerateMipmaps(FIBITMAP* bitmap, RHI_Texture* texture, unsigned int width, unsigned int height, unsigned int channels)
	{
		if (!texture)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}
	
		// Create a RescaleJob for every mip that we need
		vector<_ImagImporter::RescaleJob> jobs;
		while (width > 1 && height > 1)
		{
			width	= Math::Helper::Max(width / 2, (unsigned int)1);
			height	= Math::Helper::Max(height / 2, (unsigned int)1);
			jobs.emplace_back(width, height, channels);
			
			// Resize the RHI_Texture vector accordingly
			unsigned int size = width * height * channels;
			vector<byte>* mip = texture->Data_AddMipLevel();
			mip->reserve(size);
			mip->resize(size);
		}

		// Pass data pointers (now that the RHI_Texture mip vector has been constructed)
		for (unsigned int i = 0; i < jobs.size(); i++)
		{
			// reminder: i + 1 because the 0 mip is the default image size
			jobs[i].data = texture->Data_GetMipLevel(i + 1);
		}

		// Parallelize mipmap generation using multiple threads (because FreeImage_Rescale() using FILTER_LANCZOS3 is expensive)
		auto threading = m_context->GetSubsystem<Threading>();
		for (auto& job : jobs)
		{
			threading->AddTask([this, &job, &bitmap]()
			{
				FIBITMAP* bitmapScaled = FreeImage_Rescale(bitmap, job.width, job.height, _ImagImporter::rescaleFilter);
				if (!GetBitsFromFIBITMAP(job.data, bitmapScaled, job.width, job.height, job.channels))
				{
					LOGF_ERROR("Failed to create mip level %dx%d", job.width, job.height);
				}
				FreeImage_Unload(bitmapScaled);
				job.done = true;
			});
		}

		// Wait until all mipmaps have been generated
		bool ready = false;
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

	unsigned int ImageImporter::ComputeChannelCount(FIBITMAP* bitmap)
	{	
		if (!bitmap)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return 0;
		}

		// Compute the number of bytes per pixel
		unsigned int bytespp = FreeImage_GetLine(bitmap) / FreeImage_GetWidth(bitmap);

		// Compute the number of samples per pixel
		unsigned int channels = bytespp / (ComputeBitsPerChannel(bitmap) / 8);

		return channels;
	}

	unsigned int ImageImporter::ComputeBitsPerChannel(FIBITMAP* bitmap)
	{
		if (!bitmap)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return 0;
		}

		FREE_IMAGE_TYPE type	= FreeImage_GetImageType(bitmap);
		unsigned int size		= 0;

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

		return size * 8;
	}

	RHI_Format ImageImporter::ComputeTextureFormat(unsigned int bpp, unsigned int channels)
	{
		if (channels == 3)
		{
			if (bpp == 96) return Format_R32G32B32_FLOAT;
		}
		else if (channels == 4)
		{
			if (bpp == 32)	return Format_R8G8B8A8_UNORM;
			if (bpp == 64)	return Format_R16G16B16A16_FLOAT;
			if (bpp == 128) return Format_R32G32B32A32_FLOAT;
		}
		
		LOG_ERROR_INVALID_PARAMETER();
		return Format_R8_UNORM;
	}

	bool ImageImporter::IsVisuallyGrayscale(FIBITMAP* bitmap)
	{
		if (!bitmap)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		switch (FreeImage_GetBPP(bitmap))
		{
			case 1:
			case 4:
			case 8: 
			{
				unsigned ncolors = FreeImage_GetColorsUsed(bitmap);
				RGBQUAD *rgb = FreeImage_GetPalette(bitmap);
				for (unsigned i = 0; i < ncolors; i++) 
				{
					if ((rgb->rgbRed != rgb->rgbGreen) || (rgb->rgbRed != rgb->rgbBlue)) 
					{
						return false;
					}
				}
				return true;
			}
			default: 
			{
				return (FreeImage_GetColorType(bitmap) == FIC_MINISBLACK);
			}
		}
	}

	FIBITMAP* ImageImporter::ApplyBitmapCorrections(FIBITMAP* bitmap)
	{
		if (!bitmap)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return nullptr;
		}

		// Converting a 1 channel, 16-bit texture to a 32-bit texture, seems to fail.
		// BUt converting it down to a 8-bit texture, then up to a 32-bit one, seems to work. FreeImage bug?
		unsigned int channels = ComputeChannelCount(bitmap);
		if (channels == 1)
		{
			int bpp	= ComputeBitsPerChannel(bitmap);
			if (bpp == 16)
			{
				FIBITMAP* previousBitmap = bitmap;
				bitmap = FreeImage_ConvertTo8Bits(bitmap);
				FreeImage_Unload(previousBitmap);
			}
		}

		// Convert it to 32 bits (if lower)
		if (FreeImage_GetBPP(bitmap) < 32)
		{
			bitmap = _FreeImage_ConvertTo32Bits(bitmap);
		}

		// Swap red with blue channel (if needed)
		if (FreeImage_GetBPP(bitmap) == 32)
		{
			if (FreeImage_GetRedMask(bitmap) == 0xff0000 && ComputeChannelCount(bitmap) >= 2)
			{
				bool swapped = SwapRedBlue32(bitmap);
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

	FIBITMAP* ImageImporter::_FreeImage_ConvertTo32Bits(FIBITMAP* bitmap)
	{
		if (!bitmap)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return nullptr;
		}

		FIBITMAP* previousBitmap	= bitmap;
		bitmap						= FreeImage_ConvertTo32Bits(previousBitmap);
		if (!bitmap)
		{
			LOGF_ERROR("Failed (%d bpp, %d channels).", FreeImage_GetBPP(previousBitmap), ComputeChannelCount(previousBitmap));
			return nullptr;
		}

		FreeImage_Unload(previousBitmap);
		return bitmap;
	}

	FIBITMAP* ImageImporter::_FreeImage_Rescale(FIBITMAP* bitmap, unsigned int width, unsigned int height)
	{
		if (!bitmap || width == 0 || height == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return nullptr;
		}

		FIBITMAP* previousBitmap	= bitmap;
		bitmap						= FreeImage_Rescale(previousBitmap, width, height, _ImagImporter::rescaleFilter);
		if (!bitmap)
		{
			LOG_ERROR("Failed");
			return previousBitmap;
		}

		FreeImage_Unload(previousBitmap);
		return bitmap;
	}
}
