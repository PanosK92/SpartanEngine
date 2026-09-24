/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "RHI_TextureStreaming.h"
#include "RHI_Texture.h"
#include "RHI_Device.h"
#include <cmath>
#include <future>
#include "../commands/console/ConsoleCommands.h"

using namespace std;

namespace spartan
{
    namespace
    {
        TConsoleVar<bool> enabled("r.texture_streaming", true, "stream sampled material textures; disabling restores full mip chains");
        TConsoleVar<float> budget_mb("r.texture_streaming_budget_mb", 512.0f, "resident texture payload budget in MiB, excluding render targets and allocation overhead");
        TConsoleVar<float> upload_mb("r.texture_streaming_upload_mb", 16.0f, "upload allowance in MiB per frame; oversized replacements are amortised over subsequent frames");

        struct Entry
        {
            weak_ptr<RHI_Texture> texture;
            uint64_t request_frame = 0;
            uint64_t target_since = 0;
            uint64_t retry_frame = 0;
            uint32_t requested_mip = 0;
            uint32_t target_mip = 0;
        };
        unordered_map<uint64_t, Entry> entries;
        future<shared_ptr<RHI_Texture>> upload;
        weak_ptr<RHI_Texture> upload_source;
        uint32_t upload_mip = 0;
        void* upload_source_resource = nullptr;
        uint64_t next_upload_frame = 0;
        RHI_TextureStreaming::Statistics statistics;

        uint32_t tail_mip(const RHI_Texture& texture)
        {
            uint32_t mip = 0;
            // A standalone BC image must have a block-aligned top level on D3D12.
            // Keep at least a 128px tail where possible, and never drop narrow axes below 4.
            while (mip + 1 < texture.GetMipCount() &&
                   max(texture.GetWidth() >> mip, texture.GetHeight() >> mip) > 128 &&
                   (texture.GetWidth() >> (mip + 1)) >= 4 && (texture.GetHeight() >> (mip + 1)) >= 4 &&
                   (texture.GetWidth() >> (mip + 1)) % 4 == 0 && (texture.GetHeight() >> (mip + 1)) % 4 == 0)
                ++mip;
            return mip;
        }

        uint64_t mip_bytes(const RHI_Texture& texture, uint32_t first)
        {
            uint64_t bytes = 0;
            for (uint32_t mip = first; mip < texture.GetMipCount(); ++mip)
                bytes += RHI_Texture::CalculateMipSize(max(1u, texture.GetWidth() >> mip), max(1u, texture.GetHeight() >> mip),
                    1, texture.GetFormat(), texture.GetBitsPerChannel(), texture.GetChannelCount());
            return bytes;
        }

        uint64_t to_bytes(float mb, float fallback)
        {
            return static_cast<uint64_t>(clamp(isfinite(mb) ? mb : fallback, 1.0f, 65536.0f) * 1048576.0);
        }
    }

    bool RHI_TextureStreaming::HasStreamableData(const RHI_Texture& texture)
    {
        if (texture.m_type != RHI_Texture_Type::Type2D || texture.m_depth != 1 || !texture.IsSrv() ||
            (texture.GetFlags() & (RHI_Texture_Uav | RHI_Texture_Rtv | RHI_Texture_Vrs | RHI_Texture_Mappable | RHI_Texture_PerMipViews | RHI_Texture_ClearBlit)) ||
            texture.m_mip_count < 2 || texture.m_mip_count > rhi_max_mip_count || texture.m_slices.size() != 1 ||
            texture.m_slices[0].mips.size() != texture.m_mip_count)
            return false;

        if (texture.m_format != RHI_Format::BC1_Unorm && texture.m_format != RHI_Format::BC3_Unorm &&
            texture.m_format != RHI_Format::BC5_Unorm && texture.m_format != RHI_Format::BC7_Unorm)
            return false;

        for (uint32_t mip = 0; mip < texture.m_mip_count; ++mip)
        {
            const size_t expected = RHI_Texture::CalculateMipSize(max(1u, texture.m_width >> mip), max(1u, texture.m_height >> mip),
                1, texture.m_format, texture.m_bits_per_channel, texture.m_channel_count);
            if (texture.m_slices[0].mips[mip].bytes.size() != expected)
                return false;
        }
        return true;
    }

    bool RHI_TextureStreaming::CanStream(const RHI_Texture& texture)
    {
        return texture.GetResourceState() == ResourceState::PreparedForGpu && texture.m_rhi_resource && HasStreamableData(texture);
    }

    bool RHI_TextureStreaming::Prepare(RHI_Texture& texture)
    {
        // Loading can run on worker threads: this path touches no manager state or CVars.
        // If streaming is disabled, the render-thread scheduler restores full detail.
        if (!(texture.m_flags & RHI_Texture_Stream) || !HasStreamableData(texture))
            return texture.RHI_CreateResource();
        const uint32_t mip = tail_mip(texture);
        auto replacement = CreateReplacement(texture, mip);
        if (!replacement->RHI_CreateResource() || !replacement->GetRhiSrv())
            return false;
        Publish(texture, *replacement, mip);
        return true;
    }

    void RHI_TextureStreaming::Request(RHI_Texture* texture, float pixels, uint64_t frame)
    {
        if (!texture || !CanStream(*texture))
            return;
        auto owner = static_pointer_cast<RHI_Texture>(texture->weak_from_this().lock());
        if (!owner)
            return;

        uint32_t mip = 0;
        const uint32_t tail = tail_mip(*texture);
        // One extra mip for anisotropy, camera motion and imperfect bounds/UV estimates.
        const float texels = static_cast<float>(max(texture->GetWidth(), texture->GetHeight()));
        if (isfinite(pixels))
        {
            const float ratio = texels / max(1.0f, pixels * 2.0f);
            if (ratio > 1.0f)
                mip = min(tail, static_cast<uint32_t>(floor(log2(ratio))));
        }

        auto [it, inserted] = entries.try_emplace(texture->GetObjectId());
        Entry& entry = it->second;
        entry.texture = owner;
        entry.requested_mip = !inserted && entry.request_frame == frame ? min(entry.requested_mip, mip) : mip;
        entry.request_frame = frame;
        if (inserted)
        {
            entry.target_mip = texture->GetResidentMip();
            entry.target_since = frame;
        }
    }

    shared_ptr<RHI_Texture> RHI_TextureStreaming::CreateReplacement(const RHI_Texture& source, uint32_t mip)
    {
        // Snapshot only the selected tail on the owner thread. The upload worker never
        // reads or mutates the source, so saving and rendering can continue during upload.
        auto replacement = make_shared<RHI_Texture>();
        replacement->m_type = RHI_Texture_Type::Type2D;
        replacement->m_width = max(1u, source.m_width >> mip);
        replacement->m_height = max(1u, source.m_height >> mip);
        replacement->m_depth = 1;
        replacement->m_mip_count = source.m_mip_count - mip;
        replacement->m_format = source.m_format;
        replacement->m_channel_count = source.m_channel_count;
        replacement->m_bits_per_channel = source.m_bits_per_channel;
        replacement->m_flags = source.m_flags & ~(RHI_Texture_Compress | RHI_Texture_DeferUpload | RHI_Texture_Stream);
        if (!source.GetResourceFilePath().empty())
            replacement->SetResourceFilePath(source.GetResourceFilePath());
        replacement->m_object_name = source.m_object_name;
        replacement->m_slices.resize(1);
        replacement->m_slices[0].mips.assign(source.m_slices[0].mips.begin() + mip, source.m_slices[0].mips.end());
        return replacement;
    }

    void RHI_TextureStreaming::Publish(RHI_Texture& texture, RHI_Texture& replacement, uint32_t mip)
    {
        // The backend retires the previous image/views behind their pending GPU work.
        // Preserve source metadata/bytes and object identity (materials hold this pointer).
        texture.RHI_DestroyResource();
        swap(texture.m_rhi_resource, replacement.m_rhi_resource);
        swap(texture.m_rhi_srv, replacement.m_rhi_srv);
        swap(texture.m_layouts, replacement.m_layouts);
        texture.m_resident_mip = mip;
        texture.ComputeMemoryUsage();
    }

    bool RHI_TextureStreaming::Tick(uint64_t frame)
    {
        bool changed = false;
        if (upload.valid() && upload.wait_for(chrono::seconds(0)) == future_status::ready)
        {
            try
            {
                auto replacement = upload.get();
                auto source = upload_source.lock();
                if (source && replacement && replacement->GetRhiSrv() && CanStream(*source) &&
                    source->GetRhiResource() == upload_source_resource && !RHI_Device::IsDeviceLost())
                {
                    Publish(*source, *replacement, upload_mip);
                    changed = true;
                }
            }
            catch (const exception& e)
            {
                SP_LOG_WARNING("Texture streaming upload failed: %s", e.what());
            }
            if (!changed)
            {
                if (auto source = upload_source.lock())
                {
                    auto entry = entries.find(source->GetObjectId());
                    if (entry != entries.end()) entry->second.retry_frame = frame + 120;
                }
            }
            upload_source.reset();
            statistics.pending_bytes = 0;
        }

        struct Candidate { shared_ptr<RHI_Texture> texture; Entry* entry; uint32_t mip; uint32_t tail; };
        vector<Candidate> candidates;
        statistics.resident_bytes = statistics.full_bytes = 0;
        statistics.texture_count = 0;
        statistics.budget_bytes = to_bytes(budget_mb.GetValue(), 512.0f);
        uint64_t desired_bytes = 0;
        const bool streaming = enabled.GetValue();
        for (auto it = entries.begin(); it != entries.end();)
        {
            auto texture = it->second.texture.lock();
            if (!texture || !CanStream(*texture))
            {
                it = entries.erase(it);
                continue;
            }
            Entry& entry = it->second;
            const uint32_t tail = tail_mip(*texture);
            const uint32_t desired = !streaming ? 0 : (frame > entry.request_frame + 120 ? tail : entry.requested_mip);
            candidates.push_back({texture, &entry, desired, tail});
            statistics.resident_bytes += mip_bytes(*texture, texture->GetResidentMip());
            statistics.full_bytes += mip_bytes(*texture, 0);
            desired_bytes += mip_bytes(*texture, desired);
            ++statistics.texture_count;
            ++it;
        }

        // Apply a common pressure bias so a budget cut does not arbitrarily starve a
        // material based on hash iteration order. The small tails are never evicted.
        if (streaming)
        {
            for (uint32_t bias = 0; desired_bytes > statistics.budget_bytes && bias < rhi_max_mip_count; ++bias)
            {
                bool reduced = false;
                for (auto& candidate : candidates)
                {
                    if (candidate.mip < candidate.tail)
                    {
                        desired_bytes -= mip_bytes(*candidate.texture, candidate.mip);
                        desired_bytes += mip_bytes(*candidate.texture, ++candidate.mip);
                        reduced = true;
                    }
                }
                if (!reduced) break; // the mandatory tails can exceed a tiny configured budget
            }
        }

        const uint64_t upload_limit = to_bytes(upload_mb.GetValue(), 16.0f);
        for (auto& candidate : candidates)
        {
            if (candidate.entry->target_mip != candidate.mip)
            {
                candidate.entry->target_mip = candidate.mip;
                candidate.entry->target_since = frame;
            }
        }

        if (upload.valid() || frame < next_upload_frame || RHI_Device::IsDeviceLost()) return changed;

        // Evict first under pressure; otherwise prioritise the largest detail deficit.
        stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
        {
            const int a_delta = static_cast<int>(a.mip) - static_cast<int>(a.texture->GetResidentMip());
            const int b_delta = static_cast<int>(b.mip) - static_cast<int>(b.texture->GetResidentMip());
            if ((a_delta > 0) != (b_delta > 0)) return a_delta > 0;
            if (abs(a_delta) != abs(b_delta)) return abs(a_delta) > abs(b_delta);
            return a.texture->GetObjectId() < b.texture->GetObjectId();
        });

        for (auto& candidate : candidates)
        {
            const uint32_t resident = candidate.texture->GetResidentMip();
            if (candidate.mip == resident || frame < candidate.entry->retry_frame) continue;
            const bool shrinking = candidate.mip > resident;
            if (streaming && shrinking && statistics.resident_bytes <= statistics.budget_bytes && frame < candidate.entry->target_since + 30)
                continue; // hysteresis stops boundary jitter from repeatedly reallocating
            const uint64_t bytes = mip_bytes(*candidate.texture, candidate.mip);
            if (streaming && !shrinking && statistics.resident_bytes - mip_bytes(*candidate.texture, resident) + bytes > statistics.budget_bytes)
                continue;
            try
            {
                auto replacement = CreateReplacement(*candidate.texture, candidate.mip);
                upload = async(launch::async, [replacement]()
                {
                    replacement->PrepareForGpu();
                    replacement->ClearData();
                    return replacement;
                });
                upload_source = candidate.texture;
                upload_source_resource = candidate.texture->GetRhiResource();
                upload_mip = candidate.mip;
                next_upload_frame = frame + max(uint64_t(1), (bytes + upload_limit - 1) / upload_limit);
                statistics.pending_bytes = bytes;
            }
            catch (const exception& e)
            {
                candidate.entry->retry_frame = frame + 120;
                SP_LOG_WARNING("Texture streaming scheduling failed: %s", e.what());
            }
            break;
        }
        return changed;
    }

    void RHI_TextureStreaming::Shutdown()
    {
        // Must finish before ImmediateExecutionShutdown/device destruction.
        if (upload.valid())
        {
            try { upload.get(); }
            catch (const exception& e) { SP_LOG_WARNING("Texture streaming shutdown: %s", e.what()); }
        }
        entries.clear();
        upload_source.reset();
        upload_source_resource = nullptr;
        next_upload_frame = 0;
        statistics = {};
    }

    RHI_TextureStreaming::Statistics RHI_TextureStreaming::GetStatistics() { return statistics; }
}
