/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES ========================================
#include "pch.h"
#include "RHI_Texture.h"
#include "RHI_Device.h"
#include "../IO/FileStream.h"
#include "../Rendering/Renderer.h"
#include "../Resource/Import/ImageImporterExporter.h"
SP_WARNINGS_OFF
#include "compressonator.h"
SP_WARNINGS_ON
//===================================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    namespace compressonator
    {
        bool registered = false;
        CMP_FORMAT rhi_format_to_compressonator_format(const RHI_Format format)
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

            if (format == RHI_Format::BC7_Unorm)
                return CMP_FORMAT::CMP_FORMAT_BC7;

            SP_ASSERT_MSG(false, "No equivalent format");
            return CMP_FORMAT::CMP_FORMAT_Unknown;
        }

        void setup_cmp_texture(CMP_Texture* texture, uint32_t width, uint32_t height, CMP_FORMAT format, vector<std::byte>& data)
        {
            texture->dwSize   = sizeof(CMP_Texture);
            texture->dwWidth  = width;
            texture->dwHeight = height;
            texture->format   = format;
            texture->pData    = reinterpret_cast<uint8_t*>(data.data());
        }

        void compress(RHI_Texture* texture)
        {
            SP_ASSERT(texture);
            SP_ASSERT(texture->HasData());

            uint32_t width       = texture->GetWidth();
            uint32_t height      = texture->GetHeight();
            CMP_FORMAT srcFormat = rhi_format_to_compressonator_format(texture->GetFormat());

            // source texture setup
            auto& source_data = texture->GetMip(0, 0).bytes;
            CMP_Texture source_texture;
            setup_cmp_texture(&source_texture, width, height, srcFormat, source_data);

            // determine the size of the buffer needed for the destination texture
            CMP_Texture destination_texture;
            destination_texture.dwSize   = sizeof(CMP_Texture);
            destination_texture.dwWidth  = width;
            destination_texture.dwHeight = height;
            destination_texture.format   = CMP_FORMAT_BC1;

            // calculate buffer size using the destination texture setup
            CMP_DWORD buffer_size = CMP_CalculateBufferSize(&destination_texture);
            vector<std::byte> destination_data(buffer_size);

            // setup destination texture with the empty buffer
            setup_cmp_texture(&destination_texture, width, height, destination_texture.format, destination_data);

            CMP_CompressOptions options = {};
            options.dwSize              = sizeof(CMP_CompressOptions);
            options.fquality            = 0.05f; // set for lower quality, faster compression
            options.dwnumThreads        = 1;     // use multiple threads

            // compress
            SP_ASSERT(CMP_ConvertTexture(&source_texture, &destination_texture, &options, nullptr) == CMP_OK);

            // update the texture with the compressed data
            texture->GetMip(0, 0).bytes = destination_data;
        }
    }

    RHI_Texture::RHI_Texture() : IResource(ResourceType::Texture)
    {
        m_layout.fill(RHI_Image_Layout::Max);
        m_rhi_srv_mips.fill(nullptr);
        m_rhi_uav_mips.fill(nullptr);
        m_rhi_rtv.fill(nullptr);
        m_rhi_dsv.fill(nullptr);
        m_rhi_dsv_read_only.fill(nullptr);

        if (!compressonator::registered)
        {
            string version = to_string(AMD_COMPRESS_VERSION_MAJOR) + "." + to_string(AMD_COMPRESS_VERSION_MINOR);
            Settings::RegisterThirdPartyLib("Compressonator", version, "https://github.com/GPUOpen-Tools/compressonator");
            compressonator::registered = true;
        }
    }

    RHI_Texture::~RHI_Texture()
    {
        m_slices.clear();
        m_slices.shrink_to_fit();

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
        m_object_size = 0;
        {
            if (FileSystem::Exists(file_path))
            {
                auto file = make_unique<FileStream>(file_path, FileStream_Read);
                if (file->IsOpen())
                {
                    file->Read(&m_object_size);
                }
            }
        }

        bool append = true;
        auto file = make_unique<FileStream>(file_path, FileStream_Write | FileStream_Append);
        if (!file->IsOpen())
            return false;

        // if the existing file has texture data but we don't, don't overwrite them
        bool dont_overwrite_data = m_object_size != 0 && !HasData();
        if (dont_overwrite_data)
        {
            file->Skip
            (
                sizeof(m_object_size)  + // byte count
                sizeof(m_array_length) + // array length
                sizeof(m_mip_count)    + // mip count
                m_object_size            // bytes
            );
        }
        else
        {
            ComputeMemoryUsage();

            // write mip info
            file->Write(m_object_size);
            file->Write(m_array_length);
            file->Write(m_mip_count);

            // write mip data
            for (RHI_Texture_Slice& slice : m_slices)
            {
                for (RHI_Texture_Mip& mip : slice.mips)
                {
                    file->Write(mip.bytes);
                }
            }

            // the bytes have been saved, so we can now free some memory
            m_slices.clear();
            m_slices.shrink_to_fit();
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
        if (!FileSystem::IsFile(file_path))
        {
            SP_LOG_ERROR("Invalid file path \"%s\".", file_path.c_str());
            return false;
         }

        m_slices.clear();
        m_slices.shrink_to_fit();

        // load from drive
        {
            if (FileSystem::IsEngineTextureFile(file_path))
            {
                auto file = make_unique<FileStream>(file_path, FileStream_Read);
                if (!file->IsOpen())
                {
                    SP_LOG_ERROR("Failed to load \"%s\".", file_path.c_str());
                    return false;
                }

                // read mip info
                file->Read(&m_object_size);
                file->Read(&m_array_length);
                file->Read(&m_mip_count);

                // read mip data
                m_slices.resize(m_array_length);
                for (RHI_Texture_Slice& slice : m_slices)
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
            else if (FileSystem::IsSupportedImageFile(file_path))
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
                    if (!ImageImporterExporter::Load(file_paths[slice_index], slice_index, this))
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
                    //compressonator::compress(this);
                }
            }
        }

        if (m_flags & RHI_Texture_Mips)
        {
            // ensure the texture has the appropriate flags so that it can be used to generate mips on the GPU
            // once the mips have been generated, those flags and the resources associated with them, will be removed
            m_mip_count = static_cast<uint32_t>(log2(Math::Helper::Min<uint32_t>(m_width, m_height)));
            m_flags |= RHI_Texture_PerMipViews;
            m_flags |= RHI_Texture_Uav;
        }

        // create gpu resource
        SP_ASSERT_MSG(RHI_CreateResource(), "Failed to create GPU resource");
        m_is_ready_for_use = true;

        // gpu based mip generation
        if (m_flags & RHI_Texture_Mips)
        {
            Renderer::AddTextureForMipGeneration(this);
        }

        // if this was a native texture (means the data is already saved) and the GPU resource
        // has been created, then clear the data as we don't need it anymore
        if (FileSystem::IsEngineTextureFile(file_path))
        {
            m_slices.clear();
            m_slices.shrink_to_fit();
        }

        ComputeMemoryUsage();

        return true;
    }

    RHI_Texture_Mip& RHI_Texture::CreateMip(const uint32_t array_index)
    {
        // ensure there's room for the new array index
        while (array_index >= m_slices.size())
        {
            m_slices.emplace_back();
        }

        // create the mip in the specified slice
        RHI_Texture_Mip& mip = m_slices[array_index].mips.emplace_back();

        uint32_t mip_index = static_cast<uint32_t>(m_slices[array_index].mips.size()) - 1;
        uint32_t width     = m_width  >> mip_index;
        uint32_t height    = m_height >> mip_index;

        // calculate the size
        size_t size_bytes = CalculateMipSize(width, height, m_format, m_bits_per_channel, m_channel_count);
        mip.bytes.resize(size_bytes);
        mip.bytes.reserve(size_bytes);

        // update texture properties
        if (!m_slices.empty())
        {
            m_array_length = static_cast<uint32_t>(m_slices.size());
            m_mip_count    = static_cast<uint32_t>(m_slices[0].mips.size());
        }

        return mip;
    }

    RHI_Texture_Mip& RHI_Texture::GetMip(const uint32_t array_index, const uint32_t mip_index)
    {
        static RHI_Texture_Mip empty;

        if (array_index >= m_slices.size())
            return empty;

        if (mip_index >= m_slices[array_index].mips.size())
            return empty;

        return m_slices[array_index].mips[mip_index];
    }

    RHI_Texture_Slice& RHI_Texture::GetSlice(const uint32_t array_index)
    {
        static RHI_Texture_Slice empty;

        if (array_index >= m_slices.size())
            return empty;

        return m_slices[array_index];
    }

    void RHI_Texture::ComputeMemoryUsage()
    {
        m_object_size = 0;

        for (uint32_t array_index = 0; array_index < m_array_length; array_index++)
        {
            for (uint32_t mip_index = 0; mip_index < m_mip_count; mip_index++)
            {
                const uint32_t mip_width  = m_width >> mip_index;
                const uint32_t mip_height = m_height >> mip_index;

                m_object_size += CalculateMipSize(mip_width, mip_height, m_format, m_bits_per_channel, m_channel_count);
            }
        }
    }

    void RHI_Texture::SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* cmd_list, uint32_t mip_index /*= all_mips*/, uint32_t mip_range /*= 0*/)
    {
        const bool mip_specified = mip_index != rhi_all_mips;
        const bool ranged        = mip_specified && mip_range != 0;
        mip_index                = mip_specified ? mip_index : 0;
        mip_range                = ranged ? (mip_specified ? m_mip_count - mip_index : m_mip_count) : m_mip_count - mip_index;

        // asserts
        if (mip_specified)
        {
            SP_ASSERT_MSG(HasPerMipViews(), "A mip is specified but the texture has no per mip views");
            SP_ASSERT_MSG(mip_range != 0, "When a mip is specified, the mip_range can't be zero");
        }

        // Check if the layouts are indeed different from the new layout
        // If they are different, then find at which mip the difference starts
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

        // insert memory barrier
        if (cmd_list != nullptr)
        {
            // wait in case this texture loading in another thread
            while (!IsReadyForUse())
            {
                SP_LOG_INFO("Waiting for texture \"%s\" to finish loading...", m_object_name.c_str());
                this_thread::sleep_for(chrono::milliseconds(16));
            }

            // transition
            RHI_SetLayout(new_layout, cmd_list, mip_index, mip_range);
        }

        // update layout
        for (uint32_t i = mip_index; i < mip_index + mip_range; i++)
        {
            m_layout[i] = new_layout;
        }
    }

    void RHI_Texture::SaveAsImage(const string& file_path)
    {
        SP_ASSERT_MSG(m_mapped_data != nullptr, "The texture needs to be mappable");
        ImageImporterExporter::Save(file_path, m_width, m_height, m_channel_count, m_bits_per_channel, m_mapped_data);
        SP_LOG_INFO("Screenshot has been saved");
    }

    bool RHI_Texture::IsCompressedFormat(const RHI_Format format)
    {
        return
            format == RHI_Format::BC1_Unorm ||
            format == RHI_Format::BC3_Unorm ||
            format == RHI_Format::BC5_Unorm ||
            format == RHI_Format::BC7_Unorm;
    }

    size_t RHI_Texture::CalculateMipSize(uint32_t width, uint32_t height, RHI_Format format, uint32_t bits_per_channel, uint32_t channel_count)
    {
        SP_ASSERT(width > 0);
        SP_ASSERT(height > 0);

        if (IsCompressedFormat(format))
        {
            uint32_t block_size;
            switch (format)
            {
            case RHI_Format::BC1_Unorm:
                block_size = 8; // 8 bytes per block
                break;
            case RHI_Format::BC3_Unorm:
            case RHI_Format::BC7_Unorm:
            case RHI_Format::BC5_Unorm:
                block_size = 16; // 16 bytes per block
                break;
            default:
                return 0;
            }

            uint32_t num_blocks_wide = (width + 3) / 4;
            uint32_t num_blocks_high = (height + 3) / 4;
            return num_blocks_wide * num_blocks_high * block_size;
        }
        else
        {
            SP_ASSERT(channel_count > 0);
            SP_ASSERT(bits_per_channel > 0);

            return static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channel_count) * static_cast<size_t>(bits_per_channel / 8);
        }
    }
}
