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
#include <chrono>
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
        TConsoleVar<bool> automatic_budget("r.texture_streaming_auto_budget", true, "cap the texture budget to 1/16 of the driver GPU budget, leaving room for geometry, render targets and ray tracing");
        TConsoleVar<float> upload_mb("r.texture_streaming_upload_mb", 16.0f, "upload allowance in MiB per frame; oversized replacements are amortised over subsequent frames");

        // one immediate graphics command list per replacement, the immediate ring holds 16
        constexpr size_t max_batch_count = 12;
        // demand changes arrive through Request, time based transitions (stale requests,
        // hysteresis, retries) only need an occasional look
        constexpr uint64_t evaluation_interval = 15;

        struct Entry
        {
            weak_ptr<RHI_Texture> texture;
            void* resource = nullptr; // the image the cached sizes were validated against
            array<uint64_t, rhi_max_mip_count> chain_bytes = {}; // payload from a mip to the end of the chain
            uint64_t request_frame = 0;
            uint64_t target_since = 0;
            uint64_t retry_frame = 0;
            uint32_t requested_mip = 0;
            uint32_t target_mip = 0;
            uint32_t tail = 0;
            bool in_flight = false;
        };

        struct Job
        {
            shared_ptr<RHI_Texture> source; // keeps the source alive and destroyed on the render thread
            shared_ptr<RHI_Texture> replacement;
            void* source_resource = nullptr;
            uint32_t mip = 0;
        };

        unordered_map<uint64_t, Entry> entries;
        future<vector<Job>> batch;
        vector<shared_ptr<RHI_Texture>> published;
        uint64_t next_upload_frame = 0;
        uint64_t next_evaluation_frame = 0;
        bool dirty = true;
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

    mutex& RHI_TextureStreaming::SourceMutex()
    {
        static mutex source_mutex;
        return source_mutex;
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
        if (!texture)
            return;

        auto [it, inserted] = entries.try_emplace(texture->GetObjectId());
        Entry& entry = it->second;
        // Validation walks the whole CPU chain, so it only runs for new textures and
        // after something other than the streamer replaced the image (a reload).
        if (inserted || (!entry.in_flight && entry.resource != texture->GetRhiResource()))
        {
            auto owner = CanStream(*texture) ? static_pointer_cast<RHI_Texture>(texture->weak_from_this().lock()) : nullptr;
            if (!owner)
            {
                if (!entry.in_flight)
                    entries.erase(it);
                return;
            }
            entry.texture = owner;
            entry.resource = texture->GetRhiResource();
            entry.tail = tail_mip(*texture);
            for (uint32_t mip = 0; mip < texture->GetMipCount(); ++mip)
                entry.chain_bytes[mip] = texture->CalculateMipChainSize(mip);
            entry.target_mip = texture->GetResidentMip();
            entry.target_since = frame;
            inserted = true;
        }

        uint32_t mip = 0;
        // One extra mip for anisotropy, camera motion and imperfect bounds/UV estimates.
        const float texels = static_cast<float>(max(texture->GetWidth(), texture->GetHeight()));
        if (isfinite(pixels))
        {
            const float ratio = texels / max(1.0f, pixels * 2.0f);
            if (ratio > 1.0f)
                mip = min(entry.tail, static_cast<uint32_t>(floor(log2(ratio))));
        }

        const uint32_t requested = !inserted && entry.request_frame == frame ? min(entry.requested_mip, mip) : mip;
        dirty |= inserted || requested != entry.requested_mip;
        entry.requested_mip = requested;
        entry.request_frame = frame;
    }

    shared_ptr<RHI_Texture> RHI_TextureStreaming::CreateReplacement(const RHI_Texture& source, uint32_t mip)
    {
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
        const auto tick_start = chrono::steady_clock::now();
        auto finish = [&tick_start](bool changed)
        {
            statistics.tick_ms = chrono::duration<float, milli>(chrono::steady_clock::now() - tick_start).count();
            return changed;
        };

        bool changed = false;
        if (batch.valid() && batch.wait_for(chrono::seconds(0)) == future_status::ready)
        {
            vector<Job> jobs = batch.get();
            for (Job& job : jobs)
            {
                auto entry = entries.find(job.source->GetObjectId());
                // a reload while the batch ran replaced the image, the copy is stale
                if (job.replacement && job.replacement->GetRhiSrv() && job.source->GetRhiResource() == job.source_resource &&
                    job.source->GetResourceState() == ResourceState::PreparedForGpu && !RHI_Device::IsDeviceLost())
                {
                    Publish(*job.source, *job.replacement, job.mip);
                    published.push_back(job.source);
                    ++statistics.published_count;
                    changed = true;
                    if (entry != entries.end())
                        entry->second.resource = job.source->GetRhiResource();
                }
                else if (entry != entries.end())
                {
                    entry->second.retry_frame = frame + 120;
                }
                if (entry != entries.end())
                    entry->second.in_flight = false;
            }
            statistics.pending_bytes = 0;
            statistics.pending_count = 0;
            dirty = true;
        }

        if (batch.valid() || frame < next_upload_frame || RHI_Device::IsDeviceLost())
            return finish(changed);
        if (!dirty && frame < next_evaluation_frame)
            return finish(changed);
        dirty = false;
        next_evaluation_frame = frame + evaluation_interval;

        struct Candidate { shared_ptr<RHI_Texture> texture; Entry* entry; uint32_t mip; uint32_t resident; };
        static vector<Candidate> candidates;
        candidates.clear();
        statistics.resident_bytes = statistics.full_bytes = 0;
        statistics.texture_count = 0;
        statistics.budget_bytes = to_bytes(budget_mb.GetValue(), 512.0f);
        // Settings travel between machines. A multi-GiB texture budget saved on a large GPU
        // must not crowd out geometry and BLAS builds on a smaller one. The configured value
        // remains an upper bound; disabling the automatic limit preserves explicit tuning.
        const uint64_t gpu_budget_mb = RHI_Device::MemoryGetAvailableMb();
        if (automatic_budget.GetValue() && gpu_budget_mb != 0)
            statistics.budget_bytes = min(statistics.budget_bytes, max<uint64_t>(128, gpu_budget_mb / 16) * 1024ull * 1024ull);
        uint64_t desired_bytes = 0;
        const bool streaming = enabled.GetValue();
        for (auto it = entries.begin(); it != entries.end();)
        {
            Entry& entry = it->second;
            auto texture = entry.texture.lock();
            if (!texture || (!entry.in_flight && (texture->GetRhiResource() != entry.resource || texture->GetResourceState() != ResourceState::PreparedForGpu)))
            {
                // gone or reloaded, the next Request revalidates it
                it = entries.erase(it);
                continue;
            }
            const uint32_t resident = texture->GetResidentMip();
            const uint32_t desired = !streaming ? 0 : (frame > entry.request_frame + 120 ? entry.tail : entry.requested_mip);
            statistics.resident_bytes += entry.chain_bytes[resident];
            statistics.full_bytes += entry.chain_bytes[0];
            desired_bytes += entry.chain_bytes[desired];
            ++statistics.texture_count;
            candidates.push_back({move(texture), &entry, desired, resident});
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
                    if (candidate.mip < candidate.entry->tail)
                    {
                        desired_bytes -= candidate.entry->chain_bytes[candidate.mip];
                        desired_bytes += candidate.entry->chain_bytes[++candidate.mip];
                        reduced = true;
                    }
                }
                if (!reduced) break; // the mandatory tails can exceed a tiny configured budget
            }
        }

        static vector<Candidate*> work;
        work.clear();
        for (auto& candidate : candidates)
        {
            if (candidate.entry->target_mip != candidate.mip)
            {
                candidate.entry->target_mip = candidate.mip;
                candidate.entry->target_since = frame;
            }
            if (candidate.mip != candidate.resident && !candidate.entry->in_flight && frame >= candidate.entry->retry_frame)
                work.push_back(&candidate);
        }

        // Evict first under pressure; otherwise prioritise the largest detail deficit.
        sort(work.begin(), work.end(), [](const Candidate* a, const Candidate* b)
        {
            const int a_delta = static_cast<int>(a->mip) - static_cast<int>(a->resident);
            const int b_delta = static_cast<int>(b->mip) - static_cast<int>(b->resident);
            if ((a_delta > 0) != (b_delta > 0)) return a_delta > 0;
            if (abs(a_delta) != abs(b_delta)) return abs(a_delta) > abs(b_delta);
            return a->texture->GetObjectId() < b->texture->GetObjectId();
        });

        const uint64_t upload_limit = to_bytes(upload_mb.GetValue(), 16.0f);
        uint64_t resident_bytes = statistics.resident_bytes;
        uint64_t batch_bytes = 0;
        vector<Job> jobs;
        for (Candidate* candidate : work)
        {
            const bool shrinking = candidate->mip > candidate->resident;
            if (streaming && shrinking && resident_bytes <= statistics.budget_bytes && frame < candidate->entry->target_since + 30)
                continue; // hysteresis stops boundary jitter from repeatedly reallocating
            const uint64_t bytes = candidate->entry->chain_bytes[candidate->mip];
            const uint64_t after = resident_bytes - candidate->entry->chain_bytes[candidate->resident] + bytes;
            if (streaming && !shrinking && after > statistics.budget_bytes)
                continue;
            // an oversized replacement goes alone and is amortised over the following frames
            if (!jobs.empty() && batch_bytes + bytes > upload_limit)
                continue;
            jobs.push_back({candidate->texture, nullptr, candidate->texture->GetRhiResource(), candidate->mip});
            candidate->entry->in_flight = true;
            batch_bytes += bytes;
            resident_bytes = after;
            if (jobs.size() == max_batch_count)
                break;
        }
        candidates.clear(); // drop the strong references, jobs keep their own

        if (jobs.empty())
            return finish(changed);

        statistics.pending_bytes = batch_bytes;
        statistics.pending_count = static_cast<uint32_t>(jobs.size());
        next_upload_frame = frame + max(uint64_t(1), (batch_bytes + upload_limit - 1) / upload_limit);

        vector<uint64_t> ids;
        ids.reserve(jobs.size());
        for (const Job& job : jobs)
            ids.push_back(job.source->GetObjectId());
        try
        {
            batch = async(launch::async, [jobs = move(jobs)]() mutable
            {
                for (Job& job : jobs)
                {
                    try
                    {
                        {
                            // the copy is the only read of the source, a reload waits for it
                            lock_guard<mutex> lock(SourceMutex());
                            job.replacement = CreateReplacement(*job.source, job.mip);
                        }
                        job.replacement->m_flags |= RHI_Texture_AsyncUpload;
                        job.replacement->PrepareForGpu();
                        job.replacement->ClearData();
                    }
                    catch (const exception& e)
                    {
                        SP_LOG_WARNING("Texture streaming upload failed: %s", e.what());
                        job.replacement.reset();
                    }
                }
                return move(jobs);
            });
        }
        catch (const exception& e)
        {
            SP_LOG_WARNING("Texture streaming scheduling failed: %s", e.what());
            for (uint64_t id : ids)
            {
                auto entry = entries.find(id);
                if (entry != entries.end())
                {
                    entry->second.in_flight = false;
                    entry->second.retry_frame = frame + 120;
                }
            }
            statistics.pending_bytes = 0;
            statistics.pending_count = 0;
        }
        return finish(changed);
    }

    vector<shared_ptr<RHI_Texture>> RHI_TextureStreaming::TakePublished()
    {
        return exchange(published, {});
    }

    void RHI_TextureStreaming::Shutdown()
    {
        // Must finish before ImmediateExecutionShutdown/device destruction.
        if (batch.valid())
            batch.get();
        entries.clear();
        published.clear();
        next_upload_frame = 0;
        next_evaluation_frame = 0;
        dirty = true;
        statistics = {};
    }

    RHI_TextureStreaming::Statistics RHI_TextureStreaming::GetStatistics() { return statistics; }
}
