/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES =============
#include "pch.h"
#include "GpuMemory.h"
#include "../rhi/RHI_Buffer.h"
//========================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        mutex mutex_blocks;
        unordered_map<void*, GpuMemoryBlock> blocks;
        uint64_t allocated_bytes = 0;

        void copy_name(char* dst, size_t dst_size, const char* src)
        {
            if (!src || dst_size == 0)
            {
                if (dst_size > 0)
                {
                    dst[0] = '\0';
                }
                return;
            }

            size_t len = strlen(src);
            if (len >= dst_size)
            {
                len = dst_size - 1;
            }
            memcpy(dst, src, len);
            dst[len] = '\0';
        }
    }

    const char* strip_format_prefix(const char* format)
    {
        static const char prefix[] = "RHI_Format_";
        if (!format)
        {
            return "";
        }
        const size_t n = sizeof(prefix) - 1;
        if (strncmp(format, prefix, n) == 0)
        {
            return format + n;
        }
        return format;
    }

    void GpuMemory::Register(
        void* resource,
        uint64_t size,
        GpuMemoryKind kind,
        const char* name,
        uint64_t offset,
        uint64_t heap_id,
        uint64_t heap_size,
        const GpuMemoryDetail& detail
    )
    {
        if (!resource || size == 0)
        {
            return;
        }

        GpuMemoryBlock block = {};
        block.resource  = resource;
        block.size      = size;
        block.offset    = offset;
        block.heap_id   = heap_id != 0 ? heap_id : reinterpret_cast<uint64_t>(resource);
        block.heap_size = heap_size != 0 ? heap_size : size;
        block.kind      = kind;
        block.width     = detail.width;
        block.height    = detail.height;
        block.depth     = detail.depth;
        block.mip_count = detail.mip_count;
        copy_name(block.name, sizeof(block.name), name);
        copy_name(block.format, sizeof(block.format), strip_format_prefix(detail.format));
        copy_name(block.path, sizeof(block.path), detail.path);
        copy_name(block.type, sizeof(block.type), detail.type);

        lock_guard<mutex> lock(mutex_blocks);
        auto it = blocks.find(resource);
        if (it != blocks.end())
        {
            allocated_bytes -= it->second.size;
            it->second = block;
        }
        else
        {
            blocks.emplace(resource, block);
        }
        allocated_bytes += size;
    }

    void GpuMemory::Unregister(void* resource)
    {
        if (!resource)
        {
            return;
        }

        lock_guard<mutex> lock(mutex_blocks);
        auto it = blocks.find(resource);
        if (it == blocks.end())
        {
            return;
        }

        allocated_bytes -= it->second.size;
        blocks.erase(it);
    }

    void GpuMemory::Clear()
    {
        lock_guard<mutex> lock(mutex_blocks);
        blocks.clear();
        allocated_bytes = 0;
    }

    void GpuMemory::GetBlocks(vector<GpuMemoryBlock>& out)
    {
        lock_guard<mutex> lock(mutex_blocks);
        out.clear();
        out.reserve(blocks.size());
        for (const auto& pair : blocks)
        {
            out.push_back(pair.second);
        }
    }

    uint64_t GpuMemory::GetAllocatedBytes()
    {
        lock_guard<mutex> lock(mutex_blocks);
        return allocated_bytes;
    }

    uint32_t GpuMemory::GetAllocationCount()
    {
        lock_guard<mutex> lock(mutex_blocks);
        return static_cast<uint32_t>(blocks.size());
    }

    GpuMemoryKind GpuMemory::FromBufferType(uint32_t buffer_type)
    {
        switch (static_cast<RHI_Buffer_Type>(buffer_type))
        {
            case RHI_Buffer_Type::Vertex:              return GpuMemoryKind::Vertex;
            case RHI_Buffer_Type::Index:               return GpuMemoryKind::Index;
            case RHI_Buffer_Type::Instance:            return GpuMemoryKind::Instance;
            case RHI_Buffer_Type::Storage:             return GpuMemoryKind::Storage;
            case RHI_Buffer_Type::Constant:            return GpuMemoryKind::Constant;
            case RHI_Buffer_Type::ShaderBindingTable:  return GpuMemoryKind::ShaderBindingTable;
            case RHI_Buffer_Type::Upload:              return GpuMemoryKind::Upload;
            case RHI_Buffer_Type::Readback:            return GpuMemoryKind::Readback;
            default:                                   return GpuMemoryKind::Other;
        }
    }

    const char* GpuMemory::GetKindName(GpuMemoryKind kind)
    {
        switch (kind)
        {
            case GpuMemoryKind::Texture:               return "Texture";
            case GpuMemoryKind::Vertex:                return "Vertex";
            case GpuMemoryKind::Index:                 return "Index";
            case GpuMemoryKind::Instance:              return "Instance";
            case GpuMemoryKind::Storage:               return "Storage";
            case GpuMemoryKind::Constant:              return "Constant";
            case GpuMemoryKind::Upload:                return "Upload";
            case GpuMemoryKind::Readback:              return "Readback";
            case GpuMemoryKind::ShaderBindingTable:    return "SBT";
            case GpuMemoryKind::AccelerationStructure: return "AS";
            default:                                   return "Other";
        }
    }
}
