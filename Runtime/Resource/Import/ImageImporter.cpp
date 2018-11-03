/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "ImageImporter.h"
#include "FreeImagePlus.h"
#include "../../Threading/Threading.h"
#include "../../Core/Settings.h"
#include "../../RHI/RHI_Texture.h"
//====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	ImageImporter::ImageImporter(Context* context)
	{
		m_context = context;
		FreeImage_Initialise(true);

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
			return false;

		if (!FileSystem::FileExists(filePath))
		{
			LOGF_ERROR("ImageImporter::Load: Cant' load image. File path \"%s\" is invalid.", filePath.c_str());
			return false;
		}

		// Get image format
		FREE_IMAGE_FORMAT format = FreeImage_GetFileType(filePath.c_str(), 0);

		// If the format is unknown
		if (format == FIF_UNKNOWN)
		{
			// Try getting the format from the file extension
			LOGF_WARNING("ImageImporter::Load: Failed to determine image format for \"%s\", attempting to detect it from the file's extension...", filePath.c_str());
			format = FreeImage_GetFIFFromFilename(filePath.c_str());

			// If the format is still unknown, give up
			if (!FreeImage_FIFSupportsReading(format))
			{
				LOGF_ERROR("ImageImporter::Load: Failed to detect the image format.");
				return false;
			}

			LOG_INFO("ImageImporter::Load: The image format has been detected succesfully.");
		}

		// Load the image
		FIBITMAP* bitmapOriginal = FreeImage_Load(format, filePath.c_str());

		// Flip it vertically
		FreeImage_FlipVertical(bitmapOriginal);

		// Perform any scaling (if necessary)
		bool userDefineDimensions	= (texture->GetWidth() != 0 && texture->GetHeight() != 0);
		bool dimensionMismatch		= (FreeImage_GetWidth(bitmapOriginal) != texture->GetWidth() && FreeImage_GetHeight(bitmapOriginal) != texture->GetHeight());
		bool scale					= userDefineDimensions && dimensionMismatch;
		FIBITMAP* bitmapScaled		= scale ? FreeImage_Rescale(bitmapOriginal, texture->GetWidth(), texture->GetHeight(), FILTER_LANCZOS3) : bitmapOriginal;

		// Convert it to 32 bits (if necessary)
		FIBITMAP* bitmap32 = FreeImage_GetBPP(bitmapOriginal) != 32 ? FreeImage_ConvertTo32Bits(bitmapScaled) : bitmapScaled;
		
		// Deduce image properties
		unsigned int image_bbp		= 32;
		bool image_transparency		= FreeImage_IsTransparent(bitmap32);
		unsigned int image_width	= FreeImage_GetWidth(bitmap32);
		unsigned int image_height	= FreeImage_GetHeight(bitmap32);
		unsigned int image_channels = ComputeChannelCount(bitmap32, image_bbp);
		bool image_grayscale		= false;
		
		// Fill RGBA vector with the data from the FIBITMAP
		texture->GetData().emplace_back(vector<std::byte>());
		GetBitsFromFIBITMAP(&texture->GetData()[0], bitmap32, image_width, image_height, image_channels, &image_grayscale);

		if (texture->IsUsingMimmaps())
		{
			GenerateMipmapsFromFIBITMAP(bitmap32, texture, image_width, image_height);
		}

		// Free memory 
		FreeImage_Unload(bitmap32);										// unload the 32-bit bitmap
		if (image_bbp != 32)	{ FreeImage_Unload(bitmapScaled); }		// unload the scaled bitmap only if it was converted	
		if (scale)				{ FreeImage_Unload(bitmapOriginal); }	// unload the non 32-bit bitmap only if it was scaled

		// Fill RHI_Texture with image properties
		texture->SetBPP(image_bbp);		
		texture->SetWidth(image_width);
		texture->SetHeight(image_height);
		texture->SetChannels(image_channels);
		texture->SetTransparency(image_transparency);
		texture->SetGrayscale(image_grayscale);

		return true;
	}

	bool ImageImporter::RescaleBits(vector<std::byte>* rgba, unsigned int fromWidth, unsigned int fromHeight, unsigned int channels, unsigned int toWidth, unsigned int toHeight)
	{
		if (rgba->empty())
		{
			LOG_WARNING("ImageImporter::Load: Can't rescale bits. Provided bits are empty.");
			return false;
		}

		unsigned int pitch	= fromWidth * channels;
		FIBITMAP* bitmap	= FreeImage_ConvertFromRawBits((BYTE*)rgba->data(), fromWidth, fromHeight, pitch, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK, FALSE);
		bool result			= GetRescaledBitsFromBitmap(rgba, toWidth, channels, toHeight, bitmap);
		return result;
	}

	unsigned int ImageImporter::ComputeChannelCount(FIBITMAP* bitmap, unsigned int bpp)
	{
		FREE_IMAGE_TYPE imageType = FreeImage_GetImageType(bitmap);
		if (imageType != FIT_BITMAP)
			return 0;

		if (bpp == 8)
			return 1;

		if (bpp == 24)
			return 3;

		if (bpp == 32)
			return 4;

		return 0;
	}

	bool ImageImporter::GetBitsFromFIBITMAP(vector<std::byte>* data, FIBITMAP* bitmap, unsigned int width, unsigned int height, unsigned int channels, bool* isGrayscale)
	{
		int grayPixels	= 0;
		int totalPixels = width * height;

		if (width == 0 || height == 0)
			return false;

		unsigned int bytesPerPixel = FreeImage_GetLine(bitmap) / width;
		data->reserve(width * height * channels);

		// Construct an RGBA array
		for (unsigned int y = 0; y < height; y++)
		{
			auto bytes = (std::byte*)FreeImage_GetScanLine(bitmap, y);
			for (unsigned int x = 0; x < width; x++)
			{
				std::byte& red		= bytes[FI_RGBA_RED];
				std::byte& green	= bytes[FI_RGBA_GREEN];
				std::byte& blue		= bytes[FI_RGBA_BLUE];
				std::byte& alpha	= bytes[FI_RGBA_ALPHA];

				// Grayscale check
				if (red == green && red == blue)
				{
					grayPixels++;
				}

				data->emplace_back(red);
				data->emplace_back(green);
				data->emplace_back(blue);
				data->emplace_back(alpha);

				// jump to next pixel
				bytes += bytesPerPixel;
			}
		}

		(*isGrayscale) = (grayPixels == totalPixels);

		return true;
	}

	bool ImageImporter::GetRescaledBitsFromBitmap(vector<std::byte>* dataOut, unsigned int width, unsigned int height, unsigned int channels, FIBITMAP* bitmap)
	{
		if (!bitmap || width == 0 || height == 0 || channels == 0)
			return false;

		dataOut->clear();
		dataOut->shrink_to_fit();

		// Rescale
		FIBITMAP* bitmapScaled = FreeImage_Rescale(bitmap, width, height, FILTER_LANCZOS3);

		// Extract RGBA data from the FIBITMAP
		bool isGrayscale = false;
		bool result = GetBitsFromFIBITMAP(dataOut, bitmapScaled, width, height, channels, &isGrayscale);

		// Unload the FIBITMAP
		FreeImage_Unload(bitmapScaled);

		return result;
	}

	void ImageImporter::GenerateMipmapsFromFIBITMAP(FIBITMAP* bitmap, RHI_Texture* texture, unsigned int width, unsigned int height)
	{
		if (!texture)
			return;

		// Define a struct that the threads will work with
		struct RescaleJob
		{
			unsigned int width		= 0;
			unsigned int height		= 0;
			unsigned int channels	= 4;
			bool complete			= false;
			vector<std::byte> data;

			RescaleJob(unsigned int width, unsigned int height, bool scaled)
			{
				this->width		= width;
				this->height	= height;
				this->complete	= scaled;
			}
		};
		vector<RescaleJob> rescaleJobs;

		// For each mip level that we need, add a job
		while (width > 1 && height > 1)
		{
			width	= max(width / 2, 1);
			height	= max(height / 2, 1);

			rescaleJobs.emplace_back(width, height, false);
		}

		// Parallelize mipmap generation using multiple
		// threads as FreeImage_Rescale() using FILTER_LANCZOS3 can take a while.
		auto threading = m_context->GetSubsystem<Threading>();
		for (auto& job : rescaleJobs)
		{
			threading->AddTask([this, &job, &bitmap]()
			{
				if (!GetRescaledBitsFromBitmap(&job.data, job.width, job.height, job.channels, bitmap))
				{
					LOGF_INFO("ImageImporter:GenerateMipmapsFromFIBITMAP: Failed to create mip level %dx%d", job.width, job.height);
				}
				job.complete = true;
			});
		}

		// Wait until all mipmaps have been generated
		bool ready = false;
		while (!ready)
		{
			ready = true;
			for (const auto& job : rescaleJobs)
			{
				if (!job.complete)
				{
					ready = false;
				}
			}
		}

		// Now move the mip map data into the texture
		for (const auto& job : rescaleJobs)
		{
			texture->GetData().emplace_back(move(job.data));
		}
	}
}
