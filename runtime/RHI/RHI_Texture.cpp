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

//= INCLUDES ================================
#include "pch.h"
#include "RHI_Texture.h"
#include "../IO/FileStream.h"
#include "../Rendering/Renderer.h"
#include "../Resource/Import/ImageImporter.h"
SP_WARNINGS_OFF
#include "compressonator.h"
SP_WARNINGS_ON
//===========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    namespace amd_compressonator
    {
        static CMP_FORMAT rhi_format_to_compressonator_format(const RHI_Format format)
        {
            if (format == RHI_Format::R8_Unorm)
                return CMP_FORMAT::CMP_FORMAT_R_8;

            if (format == RHI_Format::R16_Unorm)
                return CMP_FORMAT::CMP_FORMAT_R_16;

            if (format == RHI_Format::R16_Float)
                return CMP_FORMAT::CMP_FORMAT_R_16F;

            if (format == RHI_Format::R32_Float)
                return CMP_FORMAT::CMP_FORMAT_R_32F;

            if (format == RHI_Format::R8G8_Unorm)
                return CMP_FORMAT::CMP_FORMAT_RG_8;

            if (format == RHI_Format::R16G16_Float)
                return CMP_FORMAT::CMP_FORMAT_RG_16F;

            if (format == RHI_Format::R32G32_Float)
                return CMP_FORMAT::CMP_FORMAT_RG_32F;

            if (format == RHI_Format::R32G32B32_Float)
                return CMP_FORMAT::CMP_FORMAT_RGB_32F;

            if (format == RHI_Format::R8G8B8A8_Unorm)
                return CMP_FORMAT::CMP_FORMAT_RGBA_8888;

            if (format == RHI_Format::R16G16B16A16_Unorm)
                return CMP_FORMAT::CMP_FORMAT_RGBA_16;

            if (format == RHI_Format::R16G16B16A16_Float)
                return CMP_FORMAT::CMP_FORMAT_RGBA_16F;

            if (format == RHI_Format::R32G32B32A32_Float)
                return CMP_FORMAT::CMP_FORMAT_RGBA_32F;

            if (format == RHI_Format::ASTC)
                return CMP_FORMAT::CMP_FORMAT_ASTC;

            if (format == RHI_Format::BC7)
                return CMP_FORMAT::CMP_FORMAT_BC7;

            SP_ASSERT_MSG(false, "No equivalent format");
            return CMP_FORMAT::CMP_FORMAT_Unknown;
        }

        static void generate_mips(RHI_Texture* texture)
        {
            /*
            for (uint32_t index_array = 0; index_array < texture->GetArrayLength(); index_array++)
            {
                for (uint32_t index_mip = 0; index_mip < texture->GetMipCount() - 1; index_mip++)
                {
                    CMP_MipSet source   = {};
                    source.m_nMipLevels = 1;
                    source.m_nWidth     = texture->GetWidth() >> index_mip;
                    source.m_nHeight    = texture->GetHeight() >> index_mip;
                    source.m_format     = rhi_format_to_compressonator_format(texture->GetFormat());

                    CMP_MipLevel* sourceMip   = new CMP_MipLevel;
                    sourceMip->m_nWidth       = source.m_nWidth;
                    sourceMip->m_nHeight      = source.m_nHeight;
                    sourceMip->m_dwLinearSize = source.m_nWidth * source.m_nHeight * texture->GetChannelCount();
                    sourceMip->m_pbData       = reinterpret_cast<CMP_BYTE*>(texture->GetData()[index_array].mips[index_mip].bytes.data());
                    source.m_pMipLevelTable   = reinterpret_cast<CMP_MipLevelTable*>(sourceMip);

                    CMP_MipSet destination = {};
                    destination.m_nWidth   = source.m_nWidth >> 1;
                    destination.m_nHeight  = source.m_nHeight >> 1;
                    destination.m_format   = source.m_format;

                    CMP_MipLevel* destMip   = new CMP_MipLevel;
                    destMip->m_nWidth       = destination.m_nWidth;
                    destMip->m_nHeight      = destination.m_nHeight;
                    destMip->m_dwLinearSize = destination.m_nWidth * destination.m_nHeight * texture->GetChannelCount();

                    // allocate data for the destination mipmap
                    texture->GetData()[index_array].mips[index_mip + 1].bytes.resize(destMip->m_dwLinearSize);
                    destMip->m_pbData = reinterpret_cast<CMP_BYTE*>(texture->GetData()[index_array].mips[index_mip + 1].bytes.data());
                    destination.m_pMipLevelTable = reinterpret_cast<CMP_MipLevelTable*>(destMip);

                    // use the source to generate the destination mip level
                    CMP_GenerateMIPLevels(&destination, destination.m_nWidth);

                    // clean up for this iteration
                    delete source.m_pMipLevelTable;
                    delete destination.m_pMipLevelTable;
                }
            }
            */
        }

        static void compress(RHI_Texture* texture)
        {
            /*
            KernelOptions options;
            options.height        = texture->GetHeight();
            options.width         = texture->GetWidth();
            options.fquality      = 1.0f;
            options.format        = rhi_format_to_compressonator_format(texture->GetFormat());
            options.srcformat     = CMP_FORMAT_BC7;
            options.encodeWith    = CMP_Compute_type::CMP_GPU_HW;
            options.threads       = 0;
            options.genGPUMipMaps = true;
            options.miplevels     = texture->GetMipCount();

            for (uint32_t index_array = 0; index_array < texture->GetArrayLength(); index_array++)
            {
                CMP_MipSet source                         = {};
                source.m_nMipLevels                       = 1;
                source.m_nWidth                           = texture->GetWidth();
                source.m_nHeight                          = texture->GetHeight();
                source.m_format                           = rhi_format_to_compressonator_format(texture->GetFormat());
                source.m_pMipLevelTable                   = new CMP_MipLevel[1];
                source.m_pMipLevelTable[0].m_nWidth       = source.m_nWidth;
                source.m_pMipLevelTable[0].m_nHeight      = source.m_nHeight;
                source.m_pMipLevelTable[0].m_dwLinearSize = source.m_nWidth * source.m_nHeight * texture->GetChannelCount();
                source.m_pMipLevelTable[0].m_pbData       = reinterpret_cast<CMP_BYTE*>(texture->GetData()[index_array].mips[0].bytes.data());

                CMP_MipSet destination       = {};
                destination.m_nWidth         = source.m_nWidth;
                destination.m_nHeight        = source.m_nHeight;
                destination.m_format         = source.m_format;
                destination.m_pMipLevelTable = new CMP_MipLevel[texture->GetMipCount()];

                for (uint32_t mip_index = 0; mip_index < texture->GetMipCount(); mip_index++)
                {
                    destination.m_pMipLevelTable[mip_index].m_pbData = reinterpret_cast<CMP_BYTE*>(texture->GetData()[index_array].mips[mip_index].bytes.data());
                }

                if (CMP_CompressTexture(&options, source, destination, nullptr) != CMP_OK)
                {
                    SP_LOG_ERROR("Failed to compress texture");
                }

                delete[] source.m_pMipLevelTable;
                delete[] destination.m_pMipLevelTable;
            }
            */
        }
    }

    RHI_Texture::RHI_Texture() : IResource(ResourceType::Texture)
    {
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
        // if a file already exists, get the byte count
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

        // if the existing file has texture data but we don't, don't overwrite them
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

            // write mip info
            file->Write(m_object_size_cpu);
            file->Write(m_array_length);
            file->Write(m_mip_count);

            // write mip data
            for (RHI_Texture_Slice& slice : m_data)
            {
                for (RHI_Texture_Mip& mip : slice.mips)
                {
                    file->Write(mip.bytes);
                }
            }

            // the bytes have been saved, so we can now free some memory
            m_data.clear();
            m_data.shrink_to_fit();
        }

        // write properties
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

        // load from drive
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

                // read mip info
                file->Read(&m_object_size_cpu);
                file->Read(&m_array_length);
                file->Read(&m_mip_count);

                // read mip data
                m_data.resize(m_array_length);
                for (RHI_Texture_Slice& slice : m_data)
                {
                    slice.mips.resize(m_mip_count);
                    for (RHI_Texture_Mip& mip : slice.mips)
                    {
                        file->Read(&mip.bytes);
                    }
                }

                // read properties
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

                // if this is an array, try to find all the textures
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

                // load texture
                for (uint32_t slice_index = 0; slice_index < static_cast<uint32_t>(file_paths.size()); slice_index++)
                {
                    if (!ImageImporter::Load(file_paths[slice_index], slice_index, this))
                    {
                        SP_LOG_ERROR("Failed to load \"%s\".", file_path.c_str());
                        return false;
                    }
                }

                // set resource file path so it can be used by the resource cache.
                SetResourceFilePath(file_path);

                // compress texture
                if (m_flags & RHI_Texture_Compressed)
                {
                    //Compress(RHI_Format::RHI_Format_BC7);
                }
            }
        }

        // prepare for mip generation (if needed).
        if (m_flags & RHI_Texture_Mips)
        {
            // if it's native format, that mip count has already been loaded.
            if (!is_native_format) 
            {
                m_mip_count = static_cast<uint32_t>(log2(max(m_width, m_height))) + 1;
                for (uint32_t i = 1; i < m_mip_count; i++)
                {
                    CreateMip(0);
                }
            }

            // ensure the texture has the appropriate flags so that it can be used to generate mips on the GPU.
            // once the mips have been generated, those flags and the resources associated with them, will be removed.
            m_flags |= RHI_Texture_PerMipViews;
            m_flags |= RHI_Texture_Uav;
        }

        // create GPU resource
        SP_ASSERT_MSG(RHI_CreateResource(), "Failed to create GPU resource");

        // if this was a native texture (means the data is already saved) and the GPU resource
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
            Renderer::AddTextureForMipGeneration(this);
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
                SP_LOG_INFO("Waiting for texture \"%s\" to finish loading...", m_object_name.c_str());
                this_thread::sleep_for(chrono::milliseconds(16));
            }

            // Transition
            RHI_SetLayout(new_layout, cmd_list, mip_index, mip_range);
        }

        // Update layout
        for (uint32_t i = mip_index; i < mip_index + mip_range; i++)
        {
            m_layout[i] = new_layout;
        }
    }
}
