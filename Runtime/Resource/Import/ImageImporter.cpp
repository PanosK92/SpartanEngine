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
#include <future>
#include <functional>
#include "../../Logging/Log.h"
#include "../../Core/Context.h"
#include "../../Threading/Threading.h"
#include "../../Graphics/Texture.h"
#include "../../Core/Settings.h"
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

		// Log version
		Settings::g_versionFreeImage = FreeImage_GetVersion();
		LOG_INFO("ImageImporter: FreeImage " + Settings::g_versionFreeImage);
	}

	ImageImporter::~ImageImporter()
	{
		FreeImage_DeInitialise();
	}

	void ImageImporter::LoadAsync(const string& filePath, Texture* texInfo)
	{
		m_context->GetSubsystem<Threading>()->AddTask([this, &filePath, &texInfo]()
		{
			Load(filePath, texInfo);
		});
	}

	bool ImageImporter::Load(const string& filePath, Texture* texture)
	{
		if (!texture)
			return false;

		if (filePath.empty() || filePath == NOT_ASSIGNED)
		{
			LOG_WARNING("ImageImporter: Can't load image. No file path has been provided.");
			return false;
		}

		if (!FileSystem::FileExists(filePath))
		{
			LOG_WARNING("ImageImporter: Cant' load image. File path \"" + filePath + "\" is invalid.");
			return false;
		}

		// Get image format
		FREE_IMAGE_FORMAT format = FreeImage_GetFileType(filePath.c_str(), 0);

		// If the format is unknown
		if (format == FIF_UNKNOWN)
		{
			// Try getting the format from the file extension
			LOG_WARNING("ImageImporter: Failed to determine image format for \"" + filePath + "\", attempting to detect it from the file's extension...");
			format = FreeImage_GetFIFFromFilename(filePath.c_str());

			// If the format is still unknown, give up
			if (!FreeImage_FIFSupportsReading(format))
			{
				LOG_WARNING("ImageImporter: Failed to detect the image format.");
				return false;
			}

			LOG_WARNING("ImageImporter: The image format has been detected succesfully.");
		}

		// Get image format, format == -1 means the file was not found
		// but I am checking against it also, just in case.
		if (format == -1 || format == FIF_UNKNOWN)
		{
			return false;
		}

		// Load the image as a FIBITMAP*
		FIBITMAP* bitmapOriginal = FreeImage_Load(format, filePath.c_str());

		// Flip it vertically
		FreeImage_FlipVertical(bitmapOriginal);

		// Perform any scaling (if necessary)
		bool userDefineDimensions = (texture->GetWidth() != 0 && texture->GetHeight() != 0);
		bool dimensionMismatch = (FreeImage_GetWidth(bitmapOriginal) != texture->GetWidth() && FreeImage_GetHeight(bitmapOriginal) != texture->GetHeight());
		bool scale = userDefineDimensions && dimensionMismatch;
		FIBITMAP* bitmapScaled = scale ? FreeImage_Rescale(bitmapOriginal, texture->GetWidth(), texture->GetHeight(), FILTER_LANCZOS3) : bitmapOriginal;

		// Convert it to 32 bits (if neccessery)
		FIBITMAP* bitmap32 = FreeImage_GetBPP(bitmapOriginal) != 32 ? FreeImage_ConvertTo32Bits(bitmapScaled) : bitmapScaled;
		texture->SetBPP(32);

		// Store some useful data	
		texture->SetTransparency(bool(FreeImage_IsTransparent(bitmap32)));
		texture->SetWidth(FreeImage_GetWidth(bitmap32));
		texture->SetHeight(FreeImage_GetHeight(bitmap32));
		texture->SetChannels(ComputeChannelCount(bitmap32, texture->GetBPP()));

		// Fill RGBA vector with the data from the FIBITMAP
		texture->GetRGBA().emplace_back(vector<std::byte>());
		GetBitsFromFIBITMAP(&texture->GetRGBA()[0], bitmap32);

		// Check if the image is grayscale
		texture->SetGrayscale(GrayscaleCheck(texture->GetRGBA()[0], texture->GetWidth(), texture->GetHeight()));

		if (texture->IsUsingMimmaps())
		{
			GenerateMipmapsFromFIBITMAP(bitmap32, texture);
		}

		//= Free memory =====================================
		// unload the 32-bit bitmap
		FreeImage_Unload(bitmap32);

		// unload the scaled bitmap only if it was converted
		if (texture->GetBPP() != 32)
		{
			FreeImage_Unload(bitmapScaled);
		}

		// unload the non 32-bit bitmap only if it was scaled
		if (scale)
		{
			FreeImage_Unload(bitmapOriginal);
		}
		//====================================================

		return true;
	}

	bool ImageImporter::RescaleBits(vector<std::byte>* rgba, unsigned int fromWidth, unsigned int fromHeight, unsigned int toWidth, unsigned int toHeight)
	{
		if (rgba->empty())
		{
			LOG_WARNING("ImageImporter: Can't rescale bits. Provided bits are empty.");
			return false;
		}

		unsigned int pitch = fromWidth * 4;
		FIBITMAP* bitmap = FreeImage_ConvertFromRawBits((BYTE*)rgba->data(), fromWidth, fromHeight, pitch, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK, FALSE);
		bool result = GetRescaledBitsFromBitmap(rgba, toWidth, toHeight, bitmap);
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

	bool ImageImporter::GetBitsFromFIBITMAP(vector<std::byte>* bitsRGBA, FIBITMAP* bitmap)
	{
		unsigned int width = FreeImage_GetWidth(bitmap);
		unsigned int height = FreeImage_GetHeight(bitmap);

		if (width == 0 || height == 0)
			return false;

		unsigned int bytesPerPixel = FreeImage_GetLine(bitmap) / width;
		bitsRGBA->reserve(4 * width * height);

		// Construct an RGBA array
		for (unsigned int y = 0; y < height; y++)
		{
			auto bytes = (std::byte*)FreeImage_GetScanLine(bitmap, y);
			for (unsigned int x = 0; x < width; x++)
			{
				bitsRGBA->emplace_back(bytes[FI_RGBA_RED]);
				bitsRGBA->emplace_back(bytes[FI_RGBA_GREEN]);
				bitsRGBA->emplace_back(bytes[FI_RGBA_BLUE]);
				bitsRGBA->emplace_back(bytes[FI_RGBA_ALPHA]);

				// jump to next pixel
				bytes += bytesPerPixel;
			}
		}

		return true;
	}

	bool ImageImporter::GetRescaledBitsFromBitmap(vector<std::byte>* bytesOutRGBA, int width, int height, FIBITMAP* bitmap)
	{
		if (!bitmap || width == 0 || height == 0)
			return false;

		bytesOutRGBA->clear();
		bytesOutRGBA->shrink_to_fit();

		// Rescale
		FIBITMAP* bitmapScaled = FreeImage_Rescale(bitmap, width, height, FILTER_LANCZOS3);

		// Extract RGBA data from the FIBITMAP
		bool result = GetBitsFromFIBITMAP(bytesOutRGBA, bitmapScaled);

		// Unload the FIBITMAP
		FreeImage_Unload(bitmapScaled);

		return result;
	}

	void ImageImporter::GenerateMipmapsFromFIBITMAP(FIBITMAP* bitmap, Texture* texture)
	{
		if (!texture)
			return;

		// First mip is full size, we won't do anything special for it
		int width = texture->GetWidth();
		int height = texture->GetHeight();

		// Define a struct that the threads will work with
		struct RescaleJob
		{
			int width = 0;
			int height = 0;
			bool complete = false;
			vector<std::byte> rgba;

			RescaleJob(int width, int height, bool scaled)
			{
				this->width = width;
				this->height = height;
				this->complete = scaled;
			}
		};
		vector<RescaleJob> rescaleJobs;

		// For each mip level that we need, add a job
		while (width > 1 && height > 1)
		{
			width = max(width / 2, 1);
			height = max(height / 2, 1);

			rescaleJobs.emplace_back(width, height, false);
		}

		// Parallelize mipmap generation using multiple
		// threads as FreeImage_Rescale() using FILTER_LANCZOS3 can take a while.
		Threading* threading = m_context->GetSubsystem<Threading>();
		for (auto& job : rescaleJobs)
		{
			threading->AddTask([this, &job, &texture, &bitmap]()
			{
				if (!GetRescaledBitsFromBitmap(&job.rgba, job.width, job.height, bitmap))
				{
					string mipSize = "(" + to_string(job.width) + "x" + to_string(job.height) + ")";
					LOG_INFO("ImageImporter: Failed to create mip level " + mipSize + ".");
				}
				job.complete = true;
			});
		}

		// Wait until all mimaps have been generated
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
			texture->GetRGBA().emplace_back(move(job.rgba));
		}
	}

	bool ImageImporter::GrayscaleCheck(const vector<std::byte>& bitsRGBA, int width, int height)
	{
		if (bitsRGBA.empty())
			return false;

		int grayPixels = 0;
		int totalPixels = width * height;
		int channels = 4;

		for (int i = 0; i < height; i++)
		{
			for (int j = 0; j < width; j++)
			{
				std::byte red	= bitsRGBA[(i * width + j) * channels + 0];
				std::byte green	= bitsRGBA[(i * width + j) * channels + 1];
				std::byte blue	= bitsRGBA[(i * width + j) * channels + 2];

				if (red == green && red == blue)
				{
					grayPixels++;
				}
			}
		}

		return grayPixels == totalPixels;
	}
}
