/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "pch.h"
#include "RHI_Texture.h"
#include "RHI_Device.h"
#include "RHI_Implementation.h"
#include "../IO/FileStream.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Profiling/Profiler.h"
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
                break;

            // Compressed
            case RHI_Format::RHI_Format_BC7:
                format_amd = CMP_FORMAT::CMP_FORMAT_BC7;
                break;

            case RHI_Format::RHI_Format_ASTC:
                format_amd = CMP_FORMAT::CMP_FORMAT_ASTC;
                break;
        }

        SP_ASSERT(format_amd != CMP_FORMAT::CMP_FORMAT_Unknown);

        return format_amd;
    }

    RHI_Texture::RHI_Texture(Context* context) : IResource(context, ResourceType::Texture)
    {
        SP_ASSERT(context != nullptr);

        Renderer* renderer = context->GetSystem<Renderer>();
        SP_ASSERT(renderer != nullptr);

        m_rhi_device = renderer->GetRhiDevice();
        SP_ASSERT(m_rhi_device != nullptr);
        SP_ASSERT(m_rhi_device->GetRhiContext()->device != nullptr);

        m_layout.fill(RHI_Image_Layout::Undefined);
        m_rhi_srv_mips.fill(nullptr);
        m_rhi_uav_mips.fill(nullptr);
        m_rhi_rtv.fill(nullptr);
        m_rhi_dsv.fill(nullptr);
        m_rhi_dsv_read_only.fill(nullptr);
    }

    RHI_Texture::~RHI_Texture()
    {
        m_data.clear();
        m_data.shrink_to_fit();

        if (m_rhi_resource != nullptr)
        { 
            bool destroy_main     = true;
            bool destroy_per_view = true;
            RHI_DestroyResource(destroy_main, destroy_per_view);
        }
    }

    bool RHI_Texture::SaveToFile(const string& file_path)
    {
        // If a file already exists, get the byte count
        m_object_size_cpu = 0;
        {
            if (FileSystem::Exists(file_path))
            {
                auto file = make_unique<FileStream>(file_path, FileStream_Read);
                if (file->IsOpen())
                {
                    file->Read(&m_object_size_cpu);
                }
            }
        }

        bool append = true;
        auto file = make_unique<FileStream>(file_path, FileStream_Write | FileStream_Append);
        if (!file->IsOpen())
            return false;

        // If the existing file has texture data but we don't, don't overwrite them
        bool dont_overwrite_data = m_object_size_cpu != 0 && !HasData();
        if (dont_overwrite_data)
        {
            file->Skip
            (
                sizeof(m_object_size_cpu) + // byte count
                sizeof(m_array_length)    + // array length
                sizeof(m_mip_count)       + // mip count
                m_object_size_cpu           // bytes
            );
        }
        else
        {
            ComputeMemoryUsage();

            // Write mip info
            file->Write(m_object_size_cpu);
            file->Write(m_array_length);
            file->Write(m_mip_count);

            // Write mip data
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
        file->Write(m_width);
        file->Write(m_height);
        file->Write(m_channel_count);
        file->Write(m_bits_per_channel);
        file->Write(static_cast<uint32_t>(m_format));
        file->Write(m_flags);
        file->Write(GetObjectId());
        file->Write(GetResourceFilePath());

        return true;
    }

    bool RHI_Texture::LoadFromFile(const string& file_path)
    {
        SP_ASSERT_MSG(!file_path.empty(), "A file path is required");

        m_data.clear();
        m_data.shrink_to_fit();

        // Load from drive
        bool is_native_format  = FileSystem::IsEngineTextureFile(file_path);
        bool is_foreign_format = FileSystem::IsSupportedImageFile(file_path);
        {
            if (is_native_format)
            {
                auto file = make_unique<FileStream>(file_path, FileStream_Read);
                if (!file->IsOpen())
                {
                    SP_LOG_ERROR("Failed to load \"%s\".", file_path.c_str());
                    return false;
                }

                m_data.clear();
                m_data.shrink_to_fit();

                // Read mip info
                file->Read(&m_object_size_cpu);
                file->Read(&m_array_length);
                file->Read(&m_mip_count);

                // Read mip data
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
                file->Read(&m_width);
                file->Read(&m_height);
                file->Read(&m_channel_count);
                file->Read(&m_bits_per_channel);
                file->Read(reinterpret_cast<uint32_t*>(&m_format));
                file->Read(&m_flags);
                SetObjectId(file->ReadAs<uint64_t>());
                SetResourceFilePath(file->ReadAs<string>());
            }
            else if (is_foreign_format) // foreign format (most known image formats)
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
                ImageImporter* image_importer = ResourceCache::GetImageImporter();
                for (uint32_t slice_index = 0; slice_index < static_cast<uint32_t>(file_paths.size()); slice_index++)
                {
                    if (!image_importer->Load(file_paths[slice_index], slice_index, this))
                    {
                        SP_LOG_ERROR("Failed to load \"%s\".", file_path.c_str());
                        return false;
                    }
                }

                // Set resource file path so it can be used by the resource cache.
                SetResourceFilePath(file_path);

                // Compress texture
                if (m_flags & RHI_Texture_Compressed)
                {
                    //Compress(RHI_Format::RHI_Format_BC7);
                }
            }
        }

        // Assign texture name to the object name since Vulkan is using this for it's validation layer.
        if (m_name.empty())
        {
            m_name = GetResourceName();
        }

        // Prepare for mip generation (if needed).
        if (m_flags & RHI_Texture_Mips)
        {
            // if it's native format, that mip count has already been loaded.
            if (!is_native_format) 
            {
                // Deduce how many mips are required to scale down any dimension to 1px.
                uint32_t width              = m_width;
                uint32_t height             = m_height;
                uint32_t smallest_dimension = 1;
                while (width > smallest_dimension && height > smallest_dimension)
                {
                    width /= 2;
                    height /= 2;
                    CreateMip(0);
                }
            }

            // Ensure the texture has the appropriate flags so that it can be used to generate mips on the GPU.
            // Once the mips have been generated, those flags and the resources associated with them, will be removed.
            m_flags |= RHI_Texture_PerMipViews;
            m_flags |= RHI_Texture_Uav;
        }

        // Create GPU resource
        SP_ASSERT_MSG(RHI_CreateResource(), "Failed to create GPU resource");

        // If this was a native texture (means the data is already saved) and the GPU resource
        // has been created, then clear the data as we don't need them anymore.
        if (is_native_format)
        {
            m_data.clear();
            m_data.shrink_to_fit();
        }

        ComputeMemoryUsage();

        m_is_ready_for_use = true;

        // Request GPU based mip generation (if needed)
        if (m_flags & RHI_Texture_Mips)
        {
            m_context->GetSystem<Renderer>()->RequestTextureMipGeneration(this);
        }

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

        // Allocate memory even if there are no initial data.
        // This is to prevent APIs from failing to create a texture with mips that don't point to any mip memory.
        // This memory will be either overwritten from initial data or cleared after the mips are generated on the GPU.
        uint32_t mip_index      = m_data[array_index].GetMipCount() - 1;
        uint32_t width          = m_width >> mip_index;
        uint32_t height         = m_height >> mip_index;
        const size_t size_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(m_channel_count) * static_cast<size_t>(m_bits_per_channel / 8);
        mip.bytes.resize(size_bytes);
        mip.bytes.reserve(mip.bytes.size());

        // Update array index and mip count
        if (!m_data.empty())
        {
            m_array_length = static_cast<uint32_t>(m_data.size());
            m_mip_count    = m_data[0].GetMipCount();
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

    void RHI_Texture::SetFlag(const uint32_t flag, bool enabled /*= true*/)
    {
        if (enabled)
        {
            m_flags |= flag;
        }
        else
        {
            m_flags &= ~flag;
        }
    }
    
    bool RHI_Texture::Compress(const RHI_Format format)
    {
        bool success = true;

        for (uint32_t index_array = 0; index_array < m_array_length; index_array++)
        { 
            for (uint32_t index_mip = 0; index_mip < m_mip_count; index_mip++)
            {
                uint32_t width            = m_width >> index_mip;
                uint32_t height           = m_height >> index_mip;
                uint32_t src_pitch        = width * m_channel_count * (m_bits_per_channel / 8); // in bytes
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

                // Create a scratch buffer to hold the compressed data.
                vector<std::byte> dst_data;
                dst_data.reserve(src_texture.dwDataSize);
                dst_data.resize(src_texture.dwDataSize);

                // Destination
                CMP_Texture dst_texture = {};
                dst_texture.dwSize      = sizeof(dst_texture);
                dst_texture.dwWidth     = src_texture.dwWidth;
                dst_texture.dwHeight    = src_texture.dwHeight;
                dst_texture.dwPitch     = src_texture.dwPitch;
                dst_texture.format      = rhi_format_amd_format(format);
                dst_texture.dwDataSize  = CMP_CalculateBufferSize(&dst_texture);
                dst_texture.pData       = reinterpret_cast<CMP_BYTE*>(&dst_data[0]);

                // Alpha threshold
                CMP_BYTE alpha_threshold = IsTransparent() ? 128 : 0;

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
                options.nCompressionSpeed   = compression_speed;   // The trade-off between compression speed & quality. This value is ignored for BC6H and BC7 (for BC7 the compression speed depends on fquality value).
                options.fquality            = compression_quality; // Quality of encoding. This value ranges between 0.0 and 1.0. Default set to 1.0f (in tpacinfo.cpp).
                options.dwnumThreads        = 0;                   // Number of threads to initialize for encoding (0 auto, 128 max).
                options.nEncodeWith         = CMP_HPC;             // Use CPU High Performance Compute Encoder
                
                // Convert the source texture to the destination texture (this can be compression, decompression or converting between two uncompressed formats)
                if (CMP_ConvertTexture(&src_texture, &dst_texture, &options, nullptr) == CMP_OK)
                {
                    // Copy compressed data
                    m_data[index_array].mips[index_mip].bytes = dst_data;

                    // Assign new format
                    m_format = format;
                }
                else
                {
                    SP_LOG_ERROR("Failed to compress slice %d, mip %d.", index_array, index_mip);
                    success = false;
                    continue;

                }
            }
        }

        return success;
    }

    void RHI_Texture::ComputeMemoryUsage()
    {
        m_object_size_cpu = 0;
        m_object_size_gpu = 0;

        for (uint32_t array_index = 0; array_index < m_array_length; array_index++)
        {
            for (uint32_t mip_index = 0; mip_index < m_mip_count; mip_index++)
            {
                const uint32_t mip_width  = m_width >> mip_index;
                const uint32_t mip_height = m_height >> mip_index;

                if (array_index < m_data.size())
                {
                    if (mip_index < m_data[array_index].mips.size())
                    {
                        m_object_size_cpu += m_data[array_index].mips[mip_index].bytes.size();
                    }
                }
                m_object_size_gpu += mip_width * mip_height * m_channel_count * (m_bits_per_channel / 8);
            }
        }
    }

    void RHI_Texture::SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* cmd_list, uint32_t mip_index /*= all_mips*/, uint32_t mip_range /*= 0*/)
    {
        const bool mip_specified = mip_index != rhi_all_mips;
        const bool ranged        = mip_specified && mip_range != 0;
        mip_index                = mip_specified ? mip_index : 0;
        mip_range                = ranged ? (mip_specified ? m_mip_count - mip_index : m_mip_count) : m_mip_count - mip_index;

        // Asserts
        if (mip_specified)
        {
            SP_ASSERT_MSG(HasPerMipViews(), "A mip is specified but the texture has no per mip views");
            SP_ASSERT_MSG(mip_range != 0, "When a mip is specified, the mip_range can't be zero");
        }

        // Check if the layouts are indeed different from the new layout.
        // If they are different, then find at which mip the difference starts.
        bool transition_required = false;
        for (uint32_t i = mip_index; i < mip_index + mip_range; i++)
        {
            if (m_layout[i] != new_layout)
            {
                mip_index           = i;
                mip_range           = ranged ? (mip_specified ? m_mip_count - mip_index : m_mip_count) : m_mip_count - mip_index;
                transition_required = true;
                break;
            }
        }

        if (!transition_required)
            return;

        // Insert memory barrier
        if (cmd_list != nullptr)
        {
            // Wait in case this texture loading in another thread.
            while (!IsReadyForUse())
            {
                SP_LOG_INFO("Waiting for texture \"%s\" to finish loading...", m_name.c_str());
                this_thread::sleep_for(chrono::milliseconds(16));
            }

            // Transition
            RHI_SetLayout(new_layout, cmd_list, mip_index, mip_range);
            m_context->GetSystem<Profiler>()->m_rhi_pipeline_barriers++;
        }

        // Update layout
        for (uint32_t i = mip_index; i < mip_index + mip_range; i++)
        {
            m_layout[i] = new_layout;
        }
    }
}
