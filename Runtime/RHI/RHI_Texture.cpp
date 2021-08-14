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

//= INCLUDES ================================
#include "Spartan.h"
#include "RHI_Texture.h"
#include "RHI_Device.h"
#include "RHI_Implementation.h"
#include "../IO/FileStream.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/Import/ImageImporter.h"
#include "compressonator.h"
//===========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    static CMP_FORMAT rhi_format_amd_format(const RHI_Format format)
    {
        CMP_FORMAT format_amd = CMP_FORMAT::CMP_FORMAT_Unknown;

        switch (format)
        {
            case RHI_Format::RHI_Format_R8G8B8A8_Unorm:
                format_amd = CMP_FORMAT::CMP_FORMAT_RGBA_8888;

            // Compressed
            case RHI_Format::RHI_Format_BC7:
                format_amd = CMP_FORMAT::CMP_FORMAT_BC7;
        }

        SP_ASSERT(format_amd != CMP_FORMAT::CMP_FORMAT_Unknown);

        return format_amd;
    }

    RHI_Texture::RHI_Texture(Context* context) : IResource(context, ResourceType::Texture)
    {
        SP_ASSERT(context != nullptr);

        Renderer* renderer = m_context->GetSubsystem<Renderer>();
        SP_ASSERT(renderer != nullptr);

        m_rhi_device = context->GetSubsystem<Renderer>()->GetRhiDevice();
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_rhi_device->GetContextRhi()->device != nullptr);

        m_layout.fill(RHI_Image_Layout::Undefined);
    }

    RHI_Texture::~RHI_Texture()
    {
        m_data.clear();
        m_data.shrink_to_fit();

        DestroyResourceGpu();
    }

    bool RHI_Texture::SaveToFile(const string& file_path)
    {
        // If a file already exists, get the byte count
        uint32_t byte_count = 0;
        {
            if (FileSystem::Exists(file_path))
            {
                auto file = make_unique<FileStream>(file_path, FileStream_Read);
                if (file->IsOpen())
                {
                    file->Read(&byte_count);
                }
            }
        }

        bool append = true;
        auto file = make_unique<FileStream>(file_path, FileStream_Write | FileStream_Append);
        if (!file->IsOpen())
            return false;

        // If the existing file has texture data but we don't, don't overwrite them
        bool dont_overwrite_data = byte_count != 0 && !HasData();
        if (dont_overwrite_data)
        {
            file->Skip
            (
                sizeof(uint32_t) +    // byte count
                sizeof(uint32_t) +    // array size
                sizeof(uint32_t) +    // mip count
                byte_count            // bytes
            );
        }
        else
        {
            byte_count = GetByteCount();

            // Write data
            file->Write(byte_count);
            file->Write(m_array_length);
            file->Write(m_mip_count);
            for (RHI_Texture_Slice& slice : m_data)
            {
                for (RHI_Texture_Mip& mip : slice.mips)
                {
                    file->Write(mip.bytes);
                }
            }

            // The bytes have been saved, so we can now free some memory
            m_data.clear();
            m_data.shrink_to_fit();
        }

        // Write properties
        file->Write(m_bits_per_channel);
        file->Write(m_width);
        file->Write(m_height);
        file->Write(static_cast<uint32_t>(m_format));
        file->Write(m_channel_count);
        file->Write(m_flags);
        file->Write(GetObjectId());
        file->Write(GetResourceFilePath());

        return true;
    }

    bool RHI_Texture::LoadFromFile(const string& path)
    {
        // Validate file path
        if (!FileSystem::IsFile(path))
        {
            LOG_ERROR("\"%s\" is not a valid file path.", path.c_str());
            return false;
        }

        m_data.clear();
        m_data.shrink_to_fit();
        m_load_state = LoadState::Started;

        // Load from disk
        bool texture_data_loaded = false;
        if (FileSystem::IsEngineTextureFile(path)) // engine format (binary)
        {
            texture_data_loaded = LoadFromFile_NativeFormat(path);
        }
        else if (FileSystem::IsSupportedImageFile(path)) // foreign format (most known image formats)
        {
            texture_data_loaded = LoadFromFile_ForeignFormat(path);
        }

        // Ensure that we have the data
        if (!texture_data_loaded)
        {
            LOG_ERROR("Failed to load \"%s\".", path.c_str());
            m_load_state = LoadState::Failed;
            return false;
        }

        // Create GPU resource
        if (!CreateResourceGpu())
        {
            LOG_ERROR("Failed to create shader resource for \"%s\".", GetResourceFilePathNative().c_str());
            m_load_state = LoadState::Failed;
            return false;
        }

        // Only clear texture bytes if that's an engine texture, if not, it's not serialized yet.
        if (FileSystem::IsEngineTextureFile(path))
        {
            m_data.clear();
            m_data.shrink_to_fit();
        }

        // Compute memory usage
        {
            m_object_size_cpu = 0;
            m_object_size_gpu = 0;
            for (uint32_t array_index = 0; array_index < m_array_length; array_index++)
            {
                for (uint32_t mip_index = 0; mip_index < m_mip_count; mip_index++)
                {
                    const uint32_t mip_width    = m_width >> mip_index;
                    const uint32_t mip_height   = m_height >> mip_index;

                    if (HasData())
                    {
                        m_object_size_cpu += m_data[array_index].mips[mip_index].bytes.size() * sizeof(std::byte);
                    }
                    m_object_size_gpu += mip_width * mip_height * (m_bits_per_channel / 8);
                }
            }
        }

        m_load_state = LoadState::Completed;

        return true;
    }

    RHI_Texture_Mip& RHI_Texture::CreateMip(const uint32_t array_index)
    {
        // Grow data if needed
        while (array_index >= m_data.size())
        {
            m_data.emplace_back();
        }

        // Create mip
        RHI_Texture_Mip& mip = m_data[array_index].mips.emplace_back();

        // Update array index and mip count
        if (!m_data.empty())
        {
            m_array_length    = static_cast<uint32_t>(m_data.size());
            m_mip_count     = m_data[0].GetMipCount();
        }

        return mip;
    }

    RHI_Texture_Mip& RHI_Texture::GetMip(const uint32_t array_index, const uint32_t mip_index)
    {
        static RHI_Texture_Mip empty;

        if (array_index >= m_data.size())
            return empty;

        if (mip_index >= m_data[array_index].mips.size())
            return empty;

        return m_data[array_index].mips[mip_index];
    }

    RHI_Texture_Slice& RHI_Texture::GetSlice(const uint32_t array_index)
    {
        static RHI_Texture_Slice empty;

        if (array_index >= m_data.size())
            return empty;

        return m_data[array_index];
    }

    bool RHI_Texture::LoadFromFile_ForeignFormat(const string& file_path)
    {
        vector<string> file_paths = { file_path };

        // If this is an array, try to find all the textures
        if (m_resource_type == ResourceType::Texture2dArray)
        {
            string file_path_extension    = FileSystem::GetExtensionFromFilePath(file_path);
            string file_path_no_extension = FileSystem::GetFilePathWithoutExtension(file_path);
            string file_path_no_digit     = file_path_no_extension.substr(0, file_path_no_extension.size() - 1);

            uint32_t index = 1;
            string file_path_guess = file_path_no_digit + to_string(index) + file_path_extension;
            while (FileSystem::Exists(file_path_guess))
            {
                file_paths.emplace_back(file_path_guess);
                file_path_guess = file_path_no_digit + to_string(++index) + file_path_extension;
            }
        }

        // Load texture
        ImageImporter* image_importer = m_context->GetSubsystem<ResourceCache>()->GetImageImporter();
        for (uint32_t slice_index = 0; slice_index < static_cast<uint32_t>(file_paths.size()); slice_index++)
        {
            if (!image_importer->Load(file_paths[slice_index], slice_index, this))
                return false;
        }

        // Set resource file path so it can be used by the resource cache
        SetResourceFilePath(file_path);

        // Compress texture
        if (m_flags & RHI_Texture_Loading_Compress)
        {
            //Compress(RHI_Format::RHI_Format_BC7);
        }

        return true;
    }

    bool RHI_Texture::Compress(const RHI_Format format)
    {
        bool has_alpha_channel = GetTransparency();

        for (uint32_t index_array = 0; index_array < m_array_length; index_array++)
        { 
            for (uint32_t index_mip = 0; index_mip < m_mip_count; index_mip++)
            {
                uint32_t width            = m_width >> index_mip;
                uint32_t height           = m_height >> index_mip;
                uint32_t src_pitch        = width * m_channel_count * (m_bits_per_channel / 8); // Line width in bytes
                uint32_t dest_pitch       = src_pitch;
                RHI_Texture_Mip& src_data = GetMip(index_array, index_mip);

                 // Source
                CMP_Texture src_texture = {};
                src_texture.dwSize      = sizeof(src_texture);
                src_texture.format      = rhi_format_amd_format(m_format);
                src_texture.dwWidth     = width;
                src_texture.dwHeight    = height;
                src_texture.dwPitch     = src_pitch;
                src_texture.dwDataSize  = CMP_CalculateBufferSize(&src_texture);
                src_texture.pData       = reinterpret_cast<CMP_BYTE*>(&src_data.bytes[0]);

                vector<std::byte> dst_data;
                dst_data.reserve(src_texture.dwDataSize);
                dst_data.resize(src_texture.dwDataSize);

                // Destination
                CMP_Texture dst_texture = {};
                dst_texture.dwSize      = sizeof(dst_texture);
                dst_texture.dwWidth     = width;
                dst_texture.dwHeight    = height;
                dst_texture.dwPitch     = dest_pitch;
                dst_texture.format      = rhi_format_amd_format(format);
                dst_texture.dwDataSize  = CMP_CalculateBufferSize(&dst_texture);
                dst_texture.pData       = reinterpret_cast<CMP_BYTE*>(&dst_data[0]);

                // Alpha threshold
                CMP_BYTE alpha_threshold = has_alpha_channel ? 128 : 0;

                // Compression quality
                float compression_quality = 0.05f; // Default (per AMD)

                // Compression speed
                CMP_Speed compression_speed = CMP_Speed::CMP_Speed_Normal; // ignored for BC6H and BC7
                if (dst_texture.format == CMP_FORMAT_BC1 && alpha_threshold != 0)
                {
                    // For BC1, if the compression speed is not set to normal, the alpha threshold will be ignored
                    compression_speed = CMP_Speed::CMP_Speed_Normal;
                }
                
                // Compression
                CMP_CompressOptions options = {};
                options.dwSize              = sizeof(options);
                //options.bDXT1UseAlpha     = has1BitAlphaChannel; // Encode single-bit alpha data. Only valid when compressing to DXT1 & BC1.
                options.nAlphaThreshold     = alpha_threshold;     // The alpha threshold to use when compressing to DXT1 & BC1 with bDXT1UseAlpha.
                options.nCompressionSpeed   = compression_speed;   // The trade-off between compression speed & quality. This value is ignored for BC6H and BC7 (for BC7 the compression speed depends on fquaility value).
                options.fquality            = compression_quality; // Quality of encoding. This value ranges between 0.0 and 1.0. Default set to 1.0f (in tpacinfo.cpp).
                options.dwnumThreads        = 0;                   // Number of threads to initialize for encoding (0 auto, 128 max).
                options.nEncodeWith         = CMP_HPC;             // Use CPU High Performance Compute Encoder
                
                // Convert the source texture to the destination texture (this can be compression, decompression or converting between two uncompressed formats)
                if (CMP_ConvertTexture(&src_texture, &dst_texture, &options, nullptr) != CMP_OK)
                {
                    LOG_ERROR("Failed to compress texture.");
                    return false;
                }
            }
        }

        m_format = RHI_Format::RHI_Format_BC7;
        return true;
    }

    bool RHI_Texture::LoadFromFile_NativeFormat(const string& file_path)
    {
        auto file = make_unique<FileStream>(file_path, FileStream_Read);
        if (!file->IsOpen())
            return false;

        m_data.clear();
        m_data.shrink_to_fit();

        // Read data
        uint32_t byte_count = file->ReadAs<uint32_t>();
        m_array_length      = file->ReadAs<uint32_t>();
        m_mip_count         = file->ReadAs<uint32_t>();
        m_data.resize(m_array_length);
        for (RHI_Texture_Slice& slice : m_data)
        {
            slice.mips.resize(m_mip_count);
            for (RHI_Texture_Mip& mip : slice.mips)
            {
                file->Read(&mip.bytes);
            }
        }

        // Read properties
        file->Read(&m_bits_per_channel);
        file->Read(&m_width);
        file->Read(&m_height);
        file->Read(reinterpret_cast<uint32_t*>(&m_format));
        file->Read(&m_channel_count);
        file->Read(&m_flags);
        SetObjectId(file->ReadAs<uint32_t>());
        SetResourceFilePath(file->ReadAs<string>());

        return true;
    }

    uint32_t RHI_Texture::GetChannelCountFromFormat(const RHI_Format format)
    {
        switch (format)
        {
            case RHI_Format_R8_Unorm:               return 1;
            case RHI_Format_R16_Uint:               return 1;
            case RHI_Format_R16_Float:              return 1;
            case RHI_Format_R32_Uint:               return 1;
            case RHI_Format_R32_Float:              return 1;
            case RHI_Format_R8G8_Unorm:             return 2;
            case RHI_Format_R16G16_Float:           return 2;
            case RHI_Format_R32G32_Float:           return 2;
            case RHI_Format_R11G11B10_Float:        return 3;
            case RHI_Format_R16G16B16A16_Snorm:     return 3;
            case RHI_Format_R32G32B32_Float:        return 3;
            case RHI_Format_R8G8B8A8_Unorm:         return 4;
            case RHI_Format_R10G10B10A2_Unorm:      return 4;
            case RHI_Format_R16G16B16A16_Float:     return 4;
            case RHI_Format_R32G32B32A32_Float:     return 4;
            case RHI_Format_D32_Float:              return 1;
            case RHI_Format_D32_Float_S8X24_Uint:   return 2;
            default:                                return 0;
        }
    }

    uint32_t RHI_Texture::GetByteCount()
    {
        uint32_t byte_count = 0;

        for (const RHI_Texture_Slice& slice : m_data)
        {
            for (const RHI_Texture_Mip& mip : slice.mips)
            {
                byte_count += static_cast<uint32_t>(mip.bytes.size());
            }
        }

        return byte_count;
    }
}
