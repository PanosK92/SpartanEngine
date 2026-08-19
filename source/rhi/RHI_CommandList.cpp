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

//= INCLUDES =================
#include "pch.h"
#include "RHI_CommandList.h"
#include "RHI_Texture.h"
#include "RHI_SwapChain.h"
#include "RHI_Queue.h"
#include "RHI_Shader.h"
#include "RHI_DepthStencilState.h"
#include "RHI_DescriptorSetLayout.h"
#include "RHI_Descriptor.h"
#include "RHI_Device.h"
#include "RHI_Implementation.h"
#include <unordered_set>
//============================

//= NAMESPACES ========
using namespace std;
//=====================

namespace spartan
{
    namespace
    {
        // pair begin and end to the same list if bind changes mid block
        thread_local stack<RHI_CommandList*> timeblock_cmd_lists;
    }

    namespace resource_tracker
    {
        mutex global_mutex;
        unordered_map<uint64_t, array<RHI_Tracked_Usage, rhi_max_mip_count>> texture_history;
        unordered_map<uint64_t, RHI_Tracked_Usage> buffer_history;

        bool writes(RHI_Resource_Access access)
        {
            return (static_cast<uint8_t>(access) & static_cast<uint8_t>(RHI_Resource_Access::Write)) != 0;
        }

        RHI_Resource_Access merge(RHI_Resource_Access first, RHI_Resource_Access second)
        {
            return static_cast<RHI_Resource_Access>(static_cast<uint8_t>(first) | static_cast<uint8_t>(second));
        }
    }

    void RHI_CommandList::TrackTextureUsage(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index, const uint32_t mip_range, const uint32_t array_layer, const bool uav)
    {
        if (slot >= m_max_tracked_resource_slots)
        {
            return;
        }

        RHI_Tracked_Texture_Binding& binding = uav ? m_tracked_textures_uav[slot] : m_tracked_textures_srv[slot];
        const uint32_t resolved_mip   = mip_index == rhi_all_mips ? 0 : mip_index;
        const uint32_t resolved_range = !texture ? 0 : (mip_index == rhi_all_mips ? texture->GetMipCount() : (mip_range == 0 ? 1 : mip_range));
        const RHI_Resource_Access resolved_access = texture
            ? (uav ? RHI_Resource_Access::ReadWrite : RHI_Resource_Access::Read)
            : RHI_Resource_Access::None;
        if (
            binding.texture == texture &&
            binding.mip_index == resolved_mip &&
            binding.mip_range == resolved_range &&
            binding.array_layer == array_layer &&
            binding.access == resolved_access
        )
        {
            return;
        }

        m_resources_dirty = true;
        binding = {};
        if (!texture)
        {
            return;
        }
        m_resources_have_write_bindings |= uav;

        binding.texture     = texture;
        binding.mip_index   = resolved_mip;
        binding.mip_range   = resolved_range;
        binding.array_layer = array_layer;
        binding.access      = resolved_access;
        binding.layout      = RHI_Image_Layout::General;

        auto& opposite_bindings = uav ? m_tracked_textures_srv : m_tracked_textures_uav;
        for (RHI_Tracked_Texture_Binding& opposite : opposite_bindings)
        {
            if (opposite.texture != texture)
            {
                continue;
            }

            const uint32_t binding_mip_end  = binding.mip_index + binding.mip_range;
            const uint32_t opposite_mip_end = opposite.mip_index + opposite.mip_range;
            const bool mip_overlap          = binding.mip_index < opposite_mip_end && opposite.mip_index < binding_mip_end;
            const bool layer_overlap        = binding.array_layer == rhi_all_mips || opposite.array_layer == rhi_all_mips || binding.array_layer == opposite.array_layer;
            if (mip_overlap && layer_overlap)
            {
                binding.layout  = RHI_Image_Layout::General;
                opposite.layout = RHI_Image_Layout::General;
            }
        }
    }

    void RHI_CommandList::TrackBufferUsage(const uint32_t slot, RHI_Buffer* buffer, const RHI_Resource_Access access)
    {
        if (slot < m_max_tracked_resource_slots)
        {
            const RHI_Tracked_Buffer_Binding& previous =
                m_tracked_buffers[slot];
            m_resources_dirty |=
                previous.buffer != buffer ||
                previous.access !=
                    (
                        buffer
                            ? access
                            : RHI_Resource_Access::None
                    ) ||
                previous.usage !=
                    (
                        buffer
                            ? RHI_Resource_Usage::Shader
                            : RHI_Resource_Usage::None
                    );
            m_tracked_buffers[slot].buffer = buffer;
            m_tracked_buffers[slot].access = buffer ? access : RHI_Resource_Access::None;
            m_tracked_buffers[slot].usage  = buffer ? RHI_Resource_Usage::Shader : RHI_Resource_Usage::None;
            m_resources_have_write_bindings |=
                buffer &&
                resource_tracker::writes(access);
        }
    }

    void RHI_CommandList::TrackBufferRead(const uint32_t slot, RHI_Buffer* buffer, const RHI_Resource_Usage usage)
    {
        if (slot < m_tracked_buffers_read.size())
        {
            const RHI_Tracked_Buffer_Binding& previous =
                m_tracked_buffers_read[slot];
            m_resources_dirty |=
                previous.buffer != buffer ||
                previous.access !=
                    (
                        buffer
                            ? RHI_Resource_Access::Read
                            : RHI_Resource_Access::None
                    ) ||
                previous.usage !=
                    (
                        buffer
                            ? usage
                            : RHI_Resource_Usage::None
                    );
            m_tracked_buffers_read[slot].buffer = buffer;
            m_tracked_buffers_read[slot].access = buffer ? RHI_Resource_Access::Read : RHI_Resource_Access::None;
            m_tracked_buffers_read[slot].usage  = buffer ? usage : RHI_Resource_Usage::None;
        }
    }

    void RHI_CommandList::TrackExternalTextureUsage(RHI_Texture* texture, const RHI_Resource_Access access, const RHI_Image_Layout layout, const RHI_Barrier_Scope scope, const RHI_Resource_Usage usage)
    {
        if (!texture)
        {
            return;
        }

        m_resources_dirty = true;
        auto& usages = m_tracked_texture_history[texture->GetObjectId()];
        for (uint32_t mip = 0; mip < texture->GetMipCount(); mip++)
        {
            usages[mip].access   = access;
            usages[mip].usage    = usage;
            usages[mip].scope    = scope;
            usages[mip].queue    = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
            usages[mip].layout   = layout;
            usages[mip].unsynced = resource_tracker::writes(access);
        }
    }

    void RHI_CommandList::PrepareForExternalAccess(RHI_Texture* texture, const RHI_Resource_Access access, const RHI_Image_Layout layout, const RHI_Barrier_Scope scope)
    {
        if (!texture)
        {
            return;
        }

        const uint64_t resource_id = texture->GetObjectId();
        auto [history_it, inserted] = m_tracked_texture_history.try_emplace(resource_id);
        if (inserted)
        {
            lock_guard<mutex> lock(resource_tracker::global_mutex);
            auto global_it = resource_tracker::texture_history.find(resource_id);
            if (global_it != resource_tracker::texture_history.end())
            {
                history_it->second = global_it->second;
                for (RHI_Tracked_Usage& usage : history_it->second)
                {
                    usage.unsynced = resource_tracker::writes(usage.access);
                }
            }
        }

        const RHI_Resource_Usage usage_dst = scope == RHI_Barrier_Scope::Transfer
            ? RHI_Resource_Usage::Transfer
            : (scope == RHI_Barrier_Scope::Graphics ? RHI_Resource_Usage::Attachment : RHI_Resource_Usage::Shader);
        const RHI_Image_Layout target_layout = RHI_Image_Layout::General;
        (void)layout;

        for (uint32_t mip = 0; mip < texture->GetMipCount(); mip++)
        {
            const RHI_Tracked_Usage& previous = history_it->second[mip];
            const RHI_Queue_Type current_queue = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
            const bool cross_queue = previous.queue != RHI_Queue_Type::Max && current_queue != RHI_Queue_Type::Max && previous.queue != current_queue;
            const bool has_previous = previous.access != RHI_Resource_Access::None;
            const bool to_transfer = scope == RHI_Barrier_Scope::Transfer;
            const RHI_Barrier_Scope scope_src = cross_queue
                ? RHI_Barrier_Scope::None
                : (to_transfer || !has_previous ? RHI_Barrier_Scope::All : previous.scope);
            const RHI_Resource_Access access_src = cross_queue || to_transfer || !has_previous
                ? RHI_Resource_Access::None
                : previous.access;
            const RHI_Resource_Usage usage_src = cross_queue || to_transfer || !has_previous
                ? RHI_Resource_Usage::None
                : previous.usage;

            // d3d12 still flips uav vs shader read while the rhi layout stays general
            {
                RHI_Barrier barrier = RHI_Barrier::image_layout(texture, target_layout, mip, 1);
                barrier.from(scope_src).to(scope);
                barrier.access_src = access_src;
                barrier.access_dst = access;
                barrier.usage_src  = usage_src;
                barrier.usage_dst  = usage_dst;
                InsertBarrier(barrier);
            }

            const bool scope_change = has_previous && previous.scope != scope && previous.scope != RHI_Barrier_Scope::None && scope != RHI_Barrier_Scope::None;
            const bool hazard = has_previous && previous.unsynced &&
                (resource_tracker::writes(previous.access) || resource_tracker::writes(access));
            if (cross_queue || scope_change || hazard)
            {
                m_force_memory_sync = true;
            }
        }

        TrackExternalTextureUsage(texture, access, target_layout, scope, usage_dst);
    }

    void RHI_CommandList::PrepareForExternalRead(RHI_Texture* texture, const RHI_Image_Layout layout, const RHI_Barrier_Scope scope)
    {
        PrepareForExternalAccess(texture, RHI_Resource_Access::Read, layout, scope);
    }

    void RHI_CommandList::PrepareForExternalWrite(RHI_Texture* texture, const RHI_Image_Layout layout, const RHI_Barrier_Scope scope)
    {
        PrepareForExternalAccess(texture, RHI_Resource_Access::Write, layout, scope);
    }

    void RHI_CommandList::ResetTrackedBindings()
    {
        m_resources_dirty = true;
        m_resources_have_write_bindings = false;
        m_tracked_textures_srv.fill(RHI_Tracked_Texture_Binding{});
        m_tracked_textures_uav.fill(RHI_Tracked_Texture_Binding{});
        m_tracked_attachments.fill(RHI_Tracked_Texture_Binding{});
        m_tracked_buffers.fill(RHI_Tracked_Buffer_Binding{});
        m_tracked_buffers_read.fill(RHI_Tracked_Buffer_Binding{});
        m_constant_buffer_bound = false;
    }

    void RHI_CommandList::ResetTrackedResources()
    {
        m_batch_barrier_flush = false;
        m_flushing_barriers   = false;
        m_render_pass_pending = false;
        ResetTrackedBindings();
        m_tracked_texture_history.clear();
        m_tracked_buffer_history.clear();
        m_tracked_texture_layouts.clear();
        m_tracked_image_layouts.clear();
        m_tracked_texture_history.reserve(64);
        m_tracked_buffer_history.reserve(64);
        m_current_texture_usage.reserve(64);
        m_current_buffer_usage.reserve(64);
        m_force_memory_sync = true;
        m_pass_boundary = true;
        m_immediate_memory_sync = false;
    }

    void RHI_CommandList::MarkTrackedResourcesSynced()
    {
        for (auto& [resource_id, usages] : m_tracked_texture_history)
        {
            bool keep_writes = false;
            for (const auto& [texture, current] : m_current_texture_usage)
            {
                if (texture->GetObjectId() != resource_id)
                {
                    continue;
                }
                keep_writes = true;
                for (uint32_t mip = 0; mip < texture->GetMipCount(); mip++)
                {
                    usages[mip].unsynced = resource_tracker::writes(current[mip].access);
                }
                break;
            }
            if (!keep_writes)
            {
                for (RHI_Tracked_Usage& usage : usages)
                {
                    usage.unsynced = false;
                }
            }
        }
        for (auto& [resource_id, usage] : m_tracked_buffer_history)
        {
            bool keep_write = false;
            for (const auto& [buffer, current] : m_current_buffer_usage)
            {
                if (buffer->GetObjectId() != resource_id)
                {
                    continue;
                }
                keep_write = true;
                usage.unsynced = resource_tracker::writes(current.access);
                break;
            }
            if (!keep_write)
            {
                usage.unsynced = false;
            }
        }
    }

    RHI_Image_Layout RHI_CommandList::GetTrackedTextureLayout(RHI_Texture* texture, uint32_t mip_index)
    {
        auto [it, inserted] = m_tracked_texture_layouts.try_emplace(texture);
        if (inserted)
        {
            lock_guard<mutex> lock(resource_tracker::global_mutex);
            it->second = texture->GetLayouts();
        }
        return it->second[mip_index];
    }

    void RHI_CommandList::SetTrackedTextureLayout(RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range, RHI_Image_Layout layout)
    {
        auto [it, inserted] = m_tracked_texture_layouts.try_emplace(texture);
        if (inserted)
        {
            lock_guard<mutex> lock(resource_tracker::global_mutex);
            it->second = texture->GetLayouts();
        }
        const uint32_t mip_end = min(mip_index + mip_range, texture->GetMipCount());
        for (uint32_t mip = mip_index; mip < mip_end; mip++)
        {
            it->second[mip] = layout;
        }
    }

    bool RHI_CommandList::IsTextureBindingUsed(uint32_t slot, bool storage) const
    {
        const uint64_t pipeline_hash = m_pso.GetHash();
        if (m_texture_bindings_hash != pipeline_hash)
        {
            m_texture_bindings_hash = pipeline_hash;
            m_texture_bindings_srv  = 0;
            m_texture_bindings_uav  = 0;

            for (RHI_Shader* shader : m_pso.shaders)
            {
                if (!shader)
                {
                    continue;
                }

                for (
                    const RHI_Descriptor& descriptor :
                    shader->GetDescriptors()
                )
                {
                    uint32_t binding_slot = 0;
                    uint64_t* binding_mask = nullptr;
                    if (
                        descriptor.IsBindless()
                    )
                    {
                        continue;
                    }
                    if (
                        descriptor.type ==
                        RHI_Descriptor_Type::Image
                    )
                    {
                        binding_slot =
                            descriptor.slot -
                            rhi_shader_register_shift_t;
                        binding_mask = &m_texture_bindings_srv;
                    }
                    else if (
                        descriptor.type ==
                        RHI_Descriptor_Type::TextureStorage
                    )
                    {
                        binding_slot =
                            descriptor.slot -
                            rhi_shader_register_shift_u;
                        binding_mask = &m_texture_bindings_uav;
                    }

                    if (
                        binding_mask &&
                        binding_slot <
                            m_max_tracked_resource_slots
                    )
                    {
                        *binding_mask |=
                            uint64_t(1) << binding_slot;
                    }
                }
            }
        }

        if (slot >= m_max_tracked_resource_slots)
        {
            return false;
        }

        const uint64_t binding_mask =
            storage
                ? m_texture_bindings_uav
                : m_texture_bindings_srv;
        return (
            binding_mask &
            (uint64_t(1) << slot)
        ) != 0;
    }

    RHI_Resource_Access RHI_CommandList::GetBufferAccess(uint32_t slot) const
    {
        const uint32_t slot_read_write = slot + rhi_shader_register_shift_u;
        const uint32_t slot_read       = slot + rhi_shader_register_shift_t;
        RHI_Resource_Access access     = RHI_Resource_Access::None;
        for (RHI_Shader* shader : m_pso.shaders)
        {
            if (!shader)
            {
                continue;
            }

            for (const RHI_Descriptor& descriptor : shader->GetDescriptors())
            {
                if (descriptor.type != RHI_Descriptor_Type::StructuredBuffer)
                {
                    continue;
                }

                if (descriptor.slot == slot_read_write)
                {
                    return RHI_Resource_Access::ReadWrite;
                }

                if (descriptor.slot == slot_read)
                {
                    access = RHI_Resource_Access::Read;
                }
            }
        }
        return access;
    }

    RHI_Barrier_Scope RHI_CommandList::GetResourceScope() const
    {
        if (m_pso.IsCompute())
        {
            return RHI_Barrier_Scope::Compute;
        }

        if (m_pso.IsGraphics())
        {
            return RHI_Barrier_Scope::Graphics;
        }

        return RHI_Barrier_Scope::All;
    }

#ifdef DEBUG
    namespace
    {
        bool is_generic_slot_name(const string& name)
        {
            static const char* names[] =
            {
                "tex", "tex2", "tex3", "tex4", "tex5", "tex6", "tex3d",
                "tex_uav", "tex_uav2", "tex_uav3", "tex_uav4", "tex3d_uav",
                "tex_uav_sss", "tex_uav_uint", "tex_uav_mips"
            };
            for (const char* candidate : names)
            {
                if (name == candidate)
                {
                    return true;
                }
            }
            return false;
        }

        void log_bind_once(uint64_t key, const char* fmt, const char* pass, const char* shader, const char* resource, uint32_t slot)
        {
            static mutex log_mutex;
            static unordered_set<uint64_t> logged;
            lock_guard<mutex> lock(log_mutex);
            if (logged.insert(key).second)
            {
                SP_LOG_WARNING(fmt, pass, shader, resource, slot);
            }
        }
    }
#endif

    void RHI_CommandList::ValidateBindings()
    {
#ifdef DEBUG
        const char* pass_name = m_pso.name ? m_pso.name : "unnamed";
        const uint64_t pso_hash = m_pso.GetHash();

        auto shader_name = [](RHI_Shader* shader) -> const char*
        {
            if (!shader)
            {
                return "unknown";
            }
            const string& name = shader->GetObjectName();
            return name.empty() ? shader->GetFilePath().c_str() : name.c_str();
        };

        auto slot_bound = [&](const RHI_Descriptor& descriptor) -> bool
        {
            if (descriptor.type == RHI_Descriptor_Type::Image)
            {
                const uint32_t slot = descriptor.slot - rhi_shader_register_shift_t;
                return slot < m_max_tracked_resource_slots && m_tracked_textures_srv[slot].texture != nullptr;
            }
            if (descriptor.type == RHI_Descriptor_Type::TextureStorage)
            {
                const uint32_t slot = descriptor.slot - rhi_shader_register_shift_u;
                return slot < m_max_tracked_resource_slots && m_tracked_textures_uav[slot].texture != nullptr;
            }
            if (descriptor.type == RHI_Descriptor_Type::StructuredBuffer)
            {
                uint32_t slot = descriptor.slot;
                if (slot >= rhi_shader_register_shift_u && slot < rhi_shader_register_shift_b)
                {
                    slot -= rhi_shader_register_shift_u;
                }
                else if (slot >= rhi_shader_register_shift_t)
                {
                    slot -= rhi_shader_register_shift_t;
                }
                return slot < m_max_tracked_resource_slots && m_tracked_buffers[slot].buffer != nullptr;
            }
            if (descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
            {
                if (m_pso.use_standard_resources)
                {
                    return true;
                }
                // d3d12 root cbv at b0 and root constants at b1, not a descriptor set
                const uint32_t unshifted = descriptor.slot >= rhi_shader_register_shift_b
                    ? descriptor.slot - rhi_shader_register_shift_b
                    : descriptor.slot;
                if (unshifted == 0 && m_constant_buffer_bound)
                {
                    return true;
                }
                if (unshifted == 1 && m_push_constant_size > 0)
                {
                    return true;
                }
                if (m_descriptor_layout_current)
                {
                    const vector<RHI_Descriptor>& descriptors = m_descriptor_layout_current->GetDescriptors();
                    const vector<RHI_DescriptorBinding>& bindings = m_descriptor_layout_current->GetBindings();
                    for (size_t i = 0; i < descriptors.size() && i < bindings.size(); i++)
                    {
                        if (descriptors[i].slot == descriptor.slot && bindings[i].resource != nullptr)
                        {
                            return true;
                        }
                    }
                }
                return false;
            }
            if (descriptor.type == RHI_Descriptor_Type::AccelerationStructure && m_descriptor_layout_current)
            {
                const vector<RHI_Descriptor>& descriptors = m_descriptor_layout_current->GetDescriptors();
                const vector<RHI_DescriptorBinding>& bindings = m_descriptor_layout_current->GetBindings();
                for (size_t i = 0; i < descriptors.size() && i < bindings.size(); i++)
                {
                    if (descriptors[i].slot == descriptor.slot && bindings[i].resource != nullptr)
                    {
                        return true;
                    }
                }
            }
            return false;
        };

        for (RHI_Shader* shader : m_pso.shaders)
        {
            if (!shader)
            {
                continue;
            }

            for (const RHI_Descriptor& descriptor : shader->GetDescriptors())
            {
                if (descriptor.IsBindless() || descriptor.type == RHI_Descriptor_Type::PushConstantBuffer)
                {
                    continue;
                }
                if (!descriptor.used)
                {
                    continue;
                }
                if (is_generic_slot_name(descriptor.name))
                {
                    continue;
                }
                if (slot_bound(descriptor))
                {
                    continue;
                }

                const uint64_t key = rhi_hash_combine(pso_hash, rhi_hash_combine(static_cast<uint64_t>(descriptor.slot), static_cast<uint64_t>(descriptor.type)));
                log_bind_once(key, "%s (%s): shader uses \"%s\" at slot %u, nothing bound", pass_name, shader_name(shader), descriptor.name.c_str(), descriptor.slot);
            }

            for (const RHI_Descriptor& descriptor : shader->GetDescriptors())
            {
                if (descriptor.type != RHI_Descriptor_Type::PushConstantBuffer)
                {
                    continue;
                }
                if (m_push_constant_size == 0)
                {
                    continue;
                }
                if (descriptor.struct_size == 0 || m_push_constant_size == descriptor.struct_size)
                {
                    continue;
                }
                static mutex push_log_mutex;
                static unordered_set<uint64_t> push_logged;
                const uint64_t key = rhi_hash_combine(pso_hash, static_cast<uint64_t>(descriptor.struct_size));
                lock_guard<mutex> lock(push_log_mutex);
                if (push_logged.insert(key).second)
                {
                    SP_LOG_WARNING("%s (%s): push constant size %u does not match reflected %u", pass_name, shader_name(shader), m_push_constant_size, descriptor.struct_size);
                }
            }
        }

        auto slot_in_shaders = [&](uint32_t shifted_slot) -> bool
        {
            for (RHI_Shader* shader : m_pso.shaders)
            {
                if (!shader)
                {
                    continue;
                }
                for (const RHI_Descriptor& descriptor : shader->GetDescriptors())
                {
                    if (descriptor.IsBindless())
                    {
                        continue;
                    }
                    if (descriptor.slot == shifted_slot)
                    {
                        return true;
                    }
                }
            }
            return false;
        };

        for (uint32_t slot = 0; slot < m_max_tracked_resource_slots; slot++)
        {
            if (m_tracked_textures_srv[slot].texture && !slot_in_shaders(slot + rhi_shader_register_shift_t))
            {
                const uint64_t key = rhi_hash_combine(pso_hash, 0x1000ull + slot);
                log_bind_once(key, "%s (%s): unused bind of \"%s\" at srv slot %u", pass_name, pass_name, m_tracked_textures_srv[slot].texture->GetObjectName().c_str(), slot);
            }
            if (m_tracked_textures_uav[slot].texture && !slot_in_shaders(slot + rhi_shader_register_shift_u))
            {
                const uint64_t key = rhi_hash_combine(pso_hash, 0x2000ull + slot);
                log_bind_once(key, "%s (%s): unused bind of \"%s\" at uav slot %u", pass_name, pass_name, m_tracked_textures_uav[slot].texture->GetObjectName().c_str(), slot);
            }
            if (m_tracked_buffers[slot].buffer && !slot_in_shaders(slot + rhi_shader_register_shift_u) && !slot_in_shaders(slot + rhi_shader_register_shift_t))
            {
                const uint64_t key = rhi_hash_combine(pso_hash, 0x3000ull + slot);
                log_bind_once(key, "%s (%s): unused bind of \"%s\" at buffer slot %u", pass_name, pass_name, m_tracked_buffers[slot].buffer->GetObjectName().c_str(), slot);
            }
        }
#endif
    }

    void RHI_CommandList::SynchronizeRenderTargets()
    {
        m_tracked_attachments.fill(RHI_Tracked_Texture_Binding{});
        uint32_t attachment_index = 0;
        for (RHI_Texture* texture : m_pso.render_target_color_textures)
        {
            if (texture)
            {
                RHI_Tracked_Texture_Binding& binding = m_tracked_attachments[attachment_index++];
                binding.texture   = texture;
                binding.mip_range = 1;
                binding.access    = RHI_Resource_Access::ReadWrite;
                binding.usage     = RHI_Resource_Usage::Attachment;
                binding.layout    = RHI_Image_Layout::General;
            }
        }

        if (m_pso.render_target_depth_texture)
        {
            RHI_Tracked_Texture_Binding& binding = m_tracked_attachments[attachment_index++];
            binding.texture   = m_pso.render_target_depth_texture;
            binding.mip_range = 1;
            const bool depth_write = m_pso.depth_stencil_state && (m_pso.depth_stencil_state->GetDepthWriteEnabled() || m_pso.depth_stencil_state->GetStencilWriteEnabled());
            binding.access    = depth_write ? RHI_Resource_Access::ReadWrite : RHI_Resource_Access::Read;
            binding.usage     = RHI_Resource_Usage::Attachment;
            binding.layout    = RHI_Image_Layout::General;
        }

        if (m_pso.vrs_input_texture)
        {
            RHI_Tracked_Texture_Binding& binding = m_tracked_attachments[attachment_index];
            binding.texture   = m_pso.vrs_input_texture;
            binding.mip_range = 1;
            binding.access    = RHI_Resource_Access::Read;
            binding.usage     = RHI_Resource_Usage::ShadingRate;
            binding.layout    = RHI_Image_Layout::General;
        }

        SynchronizeResources(false);
        m_tracked_attachments.fill(RHI_Tracked_Texture_Binding{});
    }

    void RHI_CommandList::SynchronizeResources(const bool include_bindings)
    {
        if (
            include_bindings &&
            !m_resources_dirty &&
            !m_resources_have_write_bindings
        )
        {
            return;
        }

#ifdef DEBUG
        if (include_bindings)
        {
            ValidateBindings();
        }
#endif

        m_batch_barrier_flush = true;
        m_current_texture_usage.clear();
        auto collect_texture = [&](const RHI_Tracked_Texture_Binding& binding)
        {
            if (!binding.texture)
            {
                return;
            }

            auto& usages = m_current_texture_usage[binding.texture];
            const uint32_t mip_end = min(binding.mip_index + binding.mip_range, binding.texture->GetMipCount());
            for (uint32_t mip = binding.mip_index; mip < mip_end; mip++)
            {
                usages[mip].access = resource_tracker::merge(usages[mip].access, binding.access);
                usages[mip].usage  = binding.usage;
                usages[mip].scope  = GetResourceScope();
                usages[mip].queue  = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
                usages[mip].layout = binding.layout;
            }
        };

        if (include_bindings)
        {
            for (const RHI_Tracked_Texture_Binding& binding : m_tracked_textures_srv)
            {
                collect_texture(binding);
            }
            for (const RHI_Tracked_Texture_Binding& binding : m_tracked_textures_uav)
            {
                collect_texture(binding);
            }
        }
        for (const RHI_Tracked_Texture_Binding& binding : m_tracked_attachments)
        {
            collect_texture(binding);
        }

        for (auto& [texture, current_usages] : m_current_texture_usage)
        {
            const uint64_t resource_id = texture->GetObjectId();
            auto [history_it, inserted] = m_tracked_texture_history.try_emplace(resource_id);
            if (inserted)
            {
                lock_guard<mutex> lock(resource_tracker::global_mutex);
                auto global_it = resource_tracker::texture_history.find(resource_id);
                if (global_it != resource_tracker::texture_history.end())
                {
                    history_it->second = global_it->second;
                    for (RHI_Tracked_Usage& usage : history_it->second)
                    {
                        usage.unsynced = resource_tracker::writes(usage.access);
                    }
                }
            }

            auto& previous_usages = history_it->second;
            bool mip_chain = false;
            if (include_bindings)
            {
                for (const RHI_Tracked_Texture_Binding& binding : m_tracked_textures_srv)
                {
                    if (binding.texture == texture)
                    {
                        for (const RHI_Tracked_Texture_Binding& uav : m_tracked_textures_uav)
                        {
                            if (uav.texture == texture)
                            {
                                mip_chain = true;
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            uint32_t barrier_start = rhi_all_mips;
            RHI_Tracked_Usage barrier_previous;
            RHI_Tracked_Usage barrier_current;
            auto flush_range = [&](uint32_t barrier_end)
            {
                if (barrier_start == rhi_all_mips)
                {
                    return;
                }

                const bool cross_queue = barrier_previous.queue != RHI_Queue_Type::Max && barrier_current.queue != RHI_Queue_Type::Max && barrier_previous.queue != barrier_current.queue;
                RHI_Barrier barrier = RHI_Barrier::image_sync(texture, barrier_previous.access, barrier_current.access, barrier_start, barrier_end - barrier_start);
                barrier.from(cross_queue ? RHI_Barrier_Scope::None : barrier_previous.scope).to(barrier_current.scope);
                barrier.access_src = cross_queue ? RHI_Resource_Access::None : barrier_previous.access;
                barrier.usage_src = cross_queue ? RHI_Resource_Usage::None : barrier_previous.usage;
                barrier.usage_dst = barrier_current.usage;
                InsertBarrier(barrier);
                barrier_start = rhi_all_mips;
            };

            for (uint32_t mip = 0; mip < texture->GetMipCount(); mip++)
            {
                RHI_Tracked_Usage& previous = previous_usages[mip];
                RHI_Tracked_Usage& current  = current_usages[mip];
                const RHI_Image_Layout desired_layout = RHI_Image_Layout::General;
                const bool layout_transition = current.access != RHI_Resource_Access::None && GetTrackedTextureLayout(texture, mip) != desired_layout;
                if (layout_transition)
                {
                    const bool cross_queue = previous.queue != RHI_Queue_Type::Max && current.queue != RHI_Queue_Type::Max && previous.queue != current.queue;
                    RHI_Barrier barrier = RHI_Barrier::image_layout(texture, desired_layout, mip, 1);
                    barrier.from(cross_queue ? RHI_Barrier_Scope::None : (previous.access == RHI_Resource_Access::None ? RHI_Barrier_Scope::All : previous.scope)).to(current.scope);
                    barrier.access_src = cross_queue ? RHI_Resource_Access::None : previous.access;
                    barrier.access_dst = current.access;
                    barrier.usage_src  = cross_queue ? RHI_Resource_Usage::None : previous.usage;
                    barrier.usage_dst  = current.usage;
                    InsertBarrier(barrier);
                }
                const bool cross_queue_mip = previous.queue != RHI_Queue_Type::Max && current.queue != RHI_Queue_Type::Max && previous.queue != current.queue;
                const bool same_rp_attachment = m_render_pass_active && previous.usage == RHI_Resource_Usage::Attachment && current.usage == RHI_Resource_Usage::Attachment;
                const bool hazard = previous.access != RHI_Resource_Access::None && current.access != RHI_Resource_Access::None && (resource_tracker::writes(previous.access) || resource_tracker::writes(current.access));
                const bool scope_change = previous.scope != current.scope && previous.scope != RHI_Barrier_Scope::None && current.scope != RHI_Barrier_Scope::None;
                const bool attachment_release = previous.usage == RHI_Resource_Usage::Attachment && current.usage != RHI_Resource_Usage::Attachment;
                const bool vulkan = RHI_Context::api_type == RHI_Api_Type::Vulkan;
                const bool sampled_after_write = previous.unsynced &&
                    resource_tracker::writes(previous.access) &&
                    current.access == RHI_Resource_Access::Read;
                const bool needs_barrier = hazard && !layout_transition && !same_rp_attachment &&
                    (
                        cross_queue_mip ||
                        scope_change ||
                        attachment_release ||
                        sampled_after_write ||
                        (mip_chain && previous.unsynced) ||
                        (!vulkan && previous.unsynced)
                    );
                const bool extends_range = barrier_start != rhi_all_mips && needs_barrier && previous.access == barrier_previous.access && previous.usage == barrier_previous.usage && previous.scope == barrier_previous.scope && previous.queue == barrier_previous.queue && current.access == barrier_current.access && current.usage == barrier_current.usage && current.scope == barrier_current.scope && current.queue == barrier_current.queue;

                if (vulkan && needs_barrier)
                {
                    m_force_memory_sync = true;
                    if (
                        mip_chain ||
                        (sampled_after_write && !m_pass_boundary) ||
                        cross_queue_mip ||
                        scope_change ||
                        attachment_release
                    )
                    {
                        m_immediate_memory_sync = true;
                    }
                }
                else if (!extends_range)
                {
                    flush_range(mip);
                    if (needs_barrier)
                    {
                        barrier_start    = mip;
                        barrier_previous = previous;
                        barrier_current  = current;
                    }
                }

                if (current.access != RHI_Resource_Access::None)
                {
                    current.unsynced = resource_tracker::writes(current.access);
                    previous = current;
                }
            }
            flush_range(texture->GetMipCount());
        }

        if (!include_bindings)
        {
            m_batch_barrier_flush = false;
            return;
        }

        m_current_buffer_usage.clear();
        auto collect_buffer = [&](const RHI_Tracked_Buffer_Binding& binding)
        {
            if (!binding.buffer)
            {
                return;
            }

            RHI_Tracked_Usage& usage = m_current_buffer_usage[binding.buffer];
            usage.access = resource_tracker::merge(usage.access, binding.access);
            if (
                usage.usage == RHI_Resource_Usage::None ||
                binding.usage == RHI_Resource_Usage::Indirect ||
                (binding.usage != RHI_Resource_Usage::Shader && usage.usage == RHI_Resource_Usage::Shader)
            )
            {
                usage.usage = binding.usage;
            }
            usage.scope = GetResourceScope();
            usage.queue = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
        };
        for (const RHI_Tracked_Buffer_Binding& binding : m_tracked_buffers)
        {
            collect_buffer(binding);
        }
        for (const RHI_Tracked_Buffer_Binding& binding : m_tracked_buffers_read)
        {
            collect_buffer(binding);
        }

        for (auto& [buffer, current] : m_current_buffer_usage)
        {
            const uint64_t resource_id = buffer->GetObjectId();
            auto [history_it, inserted] = m_tracked_buffer_history.try_emplace(resource_id);
            if (inserted)
            {
                lock_guard<mutex> lock(resource_tracker::global_mutex);
                auto global_it = resource_tracker::buffer_history.find(resource_id);
                if (global_it != resource_tracker::buffer_history.end())
                {
                    history_it->second = global_it->second;
                    history_it->second.unsynced = resource_tracker::writes(history_it->second.access);
                }
            }

            RHI_Tracked_Usage& previous = history_it->second;
            const bool wrote = resource_tracker::writes(previous.access) || resource_tracker::writes(current.access);
            const bool compute_to_graphics =
                previous.scope == RHI_Barrier_Scope::Compute &&
                current.scope == RHI_Barrier_Scope::Graphics &&
                resource_tracker::writes(previous.access);
            const bool cross_queue = previous.queue != RHI_Queue_Type::Max && current.queue != RHI_Queue_Type::Max && previous.queue != current.queue;
            const bool vulkan = RHI_Context::api_type == RHI_Api_Type::Vulkan;
            const bool to_indirect = current.usage == RHI_Resource_Usage::Indirect;
            if (previous.access != RHI_Resource_Access::None && (wrote || compute_to_graphics || to_indirect) &&
                (cross_queue || compute_to_graphics || to_indirect || previous.unsynced))
            {
                if (vulkan)
                {
                    m_force_memory_sync = true;
                    if (cross_queue || compute_to_graphics || to_indirect || previous.unsynced)
                    {
                        m_immediate_memory_sync = true;
                    }
                }
                else
                {
                    RHI_Barrier barrier = RHI_Barrier::buffer_sync(buffer, previous.access, current.access);
                    barrier.from(cross_queue ? RHI_Barrier_Scope::None : previous.scope).to(current.scope);
                    barrier.access_src = cross_queue ? RHI_Resource_Access::None : previous.access;
                    barrier.usage_src = cross_queue ? RHI_Resource_Usage::None : previous.usage;
                    barrier.usage_dst = current.usage;
                    InsertBarrier(barrier);
                }
            }
            current.unsynced = resource_tracker::writes(current.access);
            previous = current;
        }

        for (RHI_Tracked_Buffer_Binding& binding : m_tracked_buffers_read)
        {
            if (binding.usage == RHI_Resource_Usage::Indirect)
            {
                binding = {};
            }
        }
        m_resources_dirty = false;
        m_batch_barrier_flush = false;
    }

    void RHI_CommandList::CommitTrackedResources()
    {
        lock_guard<mutex> lock(resource_tracker::global_mutex);
        for (auto& [texture, layouts] : m_tracked_texture_layouts)
        {
            for (uint32_t mip = 0; mip < texture->GetMipCount(); mip++)
            {
                texture->SetLayoutDirect(mip, 1, layouts[mip]);
            }
        }
        for (const auto& [resource_id, usages] : m_tracked_texture_history)
        {
            resource_tracker::texture_history[resource_id] = usages;
        }
        for (const auto& [resource_id, usage] : m_tracked_buffer_history)
        {
            resource_tracker::buffer_history[resource_id] = usage;
        }
    }

    void RHI_CommandList::prepare_for_present(RHI_SwapChain* swapchain)
    {
        if (swapchain)
        {
            InsertBarrier(swapchain->GetRhiRt(), swapchain->GetFormat(), 0, 1, 1, RHI_Image_Layout::Present_Source);
        }
    }

    void RHI_CommandList::prepare_texture_for_upload(RHI_Texture* texture)
    {
        if (texture)
        {
            InsertBarrier(texture, RHI_Image_Layout::General, 0, texture->GetMipCount());
            FlushBarriers();
        }
    }

    void RHI_CommandList::PrepareTextureForSampling(RHI_Texture* texture)
    {
        if (texture && texture->GetRhiResource() && texture->GetResourceState() == ResourceState::PreparedForGpu)
        {
            const RHI_Image_Layout layout = RHI_Image_Layout::General;
            InsertBarrier(texture, layout, 0, texture->GetMipCount());
            TrackExternalTextureUsage(texture, RHI_Resource_Access::Read, layout, RHI_Barrier_Scope::All);
        }
    }

    void RHI_CommandList::prepare_textures_for_sampling(const array<RHI_Texture*, rhi_max_array_size>* textures)
    {
        if (textures)
        {
            for (RHI_Texture* texture : *textures)
            {
                PrepareTextureForSampling(texture);
            }
        }
    }

    void RHI_CommandList::prepare_buffer_for_compute(RHI_Buffer* buffer)
    {
        if (buffer)
        {
            InsertBarrier(RHI_Barrier::buffer_sync(buffer).from(RHI_Barrier_Scope::Transfer).to(RHI_Barrier_Scope::Compute));
            FlushBarriers();
        }
    }

    void RHI_CommandList::prepare_buffer_for_readback(RHI_Buffer* buffer)
    {
        if (buffer)
        {
            InsertBarrier(RHI_Barrier::buffer_sync(buffer).from(RHI_Barrier_Scope::Compute).to(RHI_Barrier_Scope::Transfer));
            FlushBarriers();
        }
    }

    void RHI_CommandList::prepare_buffer_for_graphics(RHI_Buffer* buffer)
    {
        if (buffer)
        {
            InsertBarrier(RHI_Barrier::buffer_sync(buffer).from(RHI_Barrier_Scope::Compute).to(RHI_Barrier_Scope::Graphics));
            FlushBarriers();
        }
    }

    void RHI_CommandList::dispatch(RHI_Texture* texture, float resolution_scale /*= 1.0f*/)
    {
        const uint32_t thread_group_size = 8;

        // scaled dimensions
        const uint32_t scaled_width  = RHI_Device::ScaleDimension(texture->GetWidth(), resolution_scale);
        const uint32_t scaled_height = RHI_Device::ScaleDimension(texture->GetHeight(), resolution_scale);
        const uint32_t scaled_depth  = (texture->GetType() == RHI_Texture_Type::Type3D) ? RHI_Device::ScaleDimension(texture->GetDepth(), resolution_scale) : 1;

        // conservative dispatch counts
        const uint32_t dispatch_x = (scaled_width + thread_group_size - 1) / thread_group_size;
        const uint32_t dispatch_y = (scaled_height + thread_group_size - 1) / thread_group_size;
        const uint32_t dispatch_z = (scaled_depth + thread_group_size - 1) / thread_group_size;

        dispatch(dispatch_x, dispatch_y, dispatch_z);
    }

    namespace
    {
        uint32_t descriptor_slot_unshifted(const RHI_Descriptor& descriptor)
        {
            switch (descriptor.type)
            {
            case RHI_Descriptor_Type::Image:
            case RHI_Descriptor_Type::AccelerationStructure:
                return descriptor.slot - rhi_shader_register_shift_t;
            case RHI_Descriptor_Type::TextureStorage:
                return descriptor.slot - rhi_shader_register_shift_u;
            case RHI_Descriptor_Type::ConstantBuffer:
                return descriptor.slot - rhi_shader_register_shift_b;
            case RHI_Descriptor_Type::StructuredBuffer:
                if (descriptor.slot >= rhi_shader_register_shift_t)
                {
                    return descriptor.slot - rhi_shader_register_shift_t;
                }
                return descriptor.slot - rhi_shader_register_shift_u;
            default:
                return descriptor.slot;
            }
        }

        bool find_descriptor(const RHI_PipelineState& pso, const char* name, uint32_t& slot, RHI_Descriptor_Type& type)
        {
            if (!name)
            {
                return false;
            }

            for (RHI_Shader* shader : pso.shaders)
            {
                if (!shader)
                {
                    continue;
                }

                for (const RHI_Descriptor& descriptor : shader->GetDescriptors())
                {
                    if (descriptor.IsBindless())
                    {
                        continue;
                    }

                    if (descriptor.name != name)
                    {
                        continue;
                    }

                    slot = descriptor_slot_unshifted(descriptor);
                    type = descriptor.type;
                    return true;
                }
            }

            return false;
        }
    }

    void RHI_CommandList::set_texture(const char* name, RHI_Texture* texture, const uint32_t mip_index, uint32_t mip_range, const uint32_t array_layer)
    {
        uint32_t slot = 0;
        RHI_Descriptor_Type type = RHI_Descriptor_Type::Max;
        if (!find_descriptor(m_pso, name, slot, type))
        {
            return;
        }

        if (type != RHI_Descriptor_Type::Image && type != RHI_Descriptor_Type::TextureStorage)
        {
            return;
        }

        const bool uav = type == RHI_Descriptor_Type::TextureStorage;
        set_texture(slot, texture, mip_index, mip_range, uav, array_layer);
    }

    void RHI_CommandList::set_buffer(const char* name, RHI_Buffer* buffer)
    {
        uint32_t slot = 0;
        RHI_Descriptor_Type type = RHI_Descriptor_Type::Max;
        if (!find_descriptor(m_pso, name, slot, type) || type != RHI_Descriptor_Type::StructuredBuffer)
        {
            return;
        }

        set_buffer(slot, buffer);
    }

    void RHI_CommandList::set_constant_buffer(const char* name, RHI_Buffer* constant_buffer)
    {
        uint32_t slot = 0;
        RHI_Descriptor_Type type = RHI_Descriptor_Type::Max;
        if (!find_descriptor(m_pso, name, slot, type) || type != RHI_Descriptor_Type::ConstantBuffer)
        {
            return;
        }

        set_constant_buffer(slot, constant_buffer);
    }

    void RHI_CommandList::set_acceleration_structure(const char* name, RHI_AccelerationStructure* tlas)
    {
        uint32_t slot = 0;
        RHI_Descriptor_Type type = RHI_Descriptor_Type::Max;
        if (!find_descriptor(m_pso, name, slot, type) || type != RHI_Descriptor_Type::AccelerationStructure)
        {
            return;
        }

        set_acceleration_structure(slot, tlas);
    }

    void RHI_CommandList::begin_pass(const char* name)
    {
        begin_timeblock(name);
        set_pass(name);
    }

    void RHI_CommandList::end_pass()
    {
        end_timeblock();
        m_pso_pending = RHI_PipelineState();
        m_pipeline_state_dirty = false;
        RHI_Device::InvokePassReset();
    }

    void RHI_CommandList::set_pass(const char* name)
    {
        RHI_Device::InvokePassReset();
        m_pso_pending = RHI_PipelineState();
        m_pso_pending.name = name;
        m_pipeline_state_dirty = true;
        m_pass_boundary = true;
    }

    void RHI_CommandList::set_shader(RHI_Shader* shader, const char* name)
    {
        SP_ASSERT(shader != nullptr);

        const RHI_Shader_Type stage = shader->GetShaderStage();
        if (stage == RHI_Shader_Type::Compute)
        {
            const char* keep_name = name ? name : m_pso_pending.name;
            m_pso_pending = RHI_PipelineState();
            m_pso_pending.name = keep_name;
        }
        else if (name)
        {
            m_pso_pending.name = name;
        }

        m_pso_pending.shaders[static_cast<uint32_t>(stage)] = shader;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_shaders(RHI_Shader* shader_a, RHI_Shader* shader_b, RHI_Shader* shader_c)
    {
        if (shader_a)
        {
            set_shader(shader_a);
        }
        if (shader_b)
        {
            set_shader(shader_b);
        }
        if (shader_c)
        {
            set_shader(shader_c);
        }
    }

    void RHI_CommandList::set_color_target(RHI_Texture* texture)
    {
        set_color_targets(texture);
    }

    void RHI_CommandList::set_color_targets(
        RHI_Texture* t0,
        RHI_Texture* t1,
        RHI_Texture* t2,
        RHI_Texture* t3,
        RHI_Texture* t4,
        RHI_Texture* t5,
        RHI_Texture* t6,
        RHI_Texture* t7
    )
    {
        m_pso_pending.SetColorTargets(t0, t1, t2, t3, t4, t5, t6, t7);
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_depth_target(RHI_Texture* texture)
    {
        m_pso_pending.SetDepthTarget(texture);
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_swap_chain(RHI_SwapChain* swapchain)
    {
        m_pso_pending.render_target_swapchain = swapchain;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_blend_state(RHI_BlendState* state)
    {
        m_pso_pending.blend_state = state;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_rasterizer_state(RHI_RasterizerState* state)
    {
        m_pso_pending.rasterizer_state = state;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_depth_stencil_state(RHI_DepthStencilState* state)
    {
        m_pso_pending.depth_stencil_state = state;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_primitive_topology(RHI_PrimitiveTopology topology)
    {
        m_pso_pending.primitive_topology = topology;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_clear_color(uint32_t index, const Color& color)
    {
        SP_ASSERT(index < rhi_max_render_target_count);
        m_pso_pending.clear_color[index] = color;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_clear_depth(float depth)
    {
        m_pso_pending.clear_depth = depth;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_vrs_texture(RHI_Texture* texture)
    {
        m_pso_pending.vrs_input_texture = texture;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_resolution_scale(bool enabled)
    {
        m_pso_pending.resolution_scale = enabled;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_multiview(bool enabled)
    {
        m_pso_pending.is_multiview = enabled;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    void RHI_CommandList::set_array_index(uint32_t index)
    {
        m_pso_pending.render_target_array_index = index;
        m_pipeline_state_dirty = true;
        TryBindPendingPipeline();
    }

    bool RHI_CommandList::IsPendingPipelineReady() const
    {
        if (m_pso_pending.IsCompute())
        {
            RHI_Shader* shader = m_pso_pending.shaders[static_cast<uint32_t>(RHI_Shader_Type::Compute)];
            return m_pso_pending.name != nullptr && shader && shader->IsCompiled();
        }

        if (m_pso_pending.IsRayTracing())
        {
            RHI_Shader* raygen = m_pso_pending.shaders[static_cast<uint32_t>(RHI_Shader_Type::RayGeneration)];
            RHI_Shader* miss = m_pso_pending.shaders[static_cast<uint32_t>(RHI_Shader_Type::RayMiss)];
            RHI_Shader* hit = m_pso_pending.shaders[static_cast<uint32_t>(RHI_Shader_Type::RayHit)];
            return m_pso_pending.name != nullptr
                && raygen && raygen->IsCompiled()
                && miss && miss->IsCompiled()
                && hit && hit->IsCompiled();
        }

        if (m_pso_pending.IsGraphics())
        {
            const bool has_target =
                m_pso_pending.render_target_color_textures[0]
                || m_pso_pending.render_target_depth_texture
                || m_pso_pending.render_target_swapchain;
            RHI_Shader* vs = m_pso_pending.shaders[static_cast<uint32_t>(RHI_Shader_Type::Vertex)];
            RHI_Shader* ms = m_pso_pending.shaders[static_cast<uint32_t>(RHI_Shader_Type::MeshShader)];
            const bool has_compiled_shader =
                (vs && vs->IsCompiled()) || (ms && ms->IsCompiled());
            return m_pso_pending.name != nullptr && has_target && has_compiled_shader;
        }

        return false;
    }

    void RHI_CommandList::TryBindPendingPipeline()
    {
        if (!m_pipeline_state_dirty)
        {
            return;
        }

        if (!IsPendingPipelineReady())
        {
            return;
        }

        set_pipeline_state(m_pso_pending);
    }

    void RHI_CommandList::PrepareDispatch()
    {
        TryBindPendingPipeline();
        if (!m_pipeline)
        {
            return;
        }
        if (m_pso.use_standard_resources && m_push_constant_size == 0)
        {
            RHI_Device::InvokeDefaultPushConstants(this);
        }
    }

    const RHI_PipelineState& RHI_CommandList::GetPipelineState()
    {
        return RHI_Device::Cmd()->get_pipeline_state();
    }

    void RHI_CommandList::SetPipelineState(RHI_PipelineState& pso)
    {
        RHI_Device::Cmd()->set_pipeline_state(pso);
    }

    void RHI_CommandList::SetPipelineState(RHI_CommandList* cmd_list, RHI_PipelineState& pso)
    {
        if (!cmd_list)
        {
            return;
        }
        cmd_list->set_pipeline_state(pso);
    }

    void RHI_CommandList::BeginPass(const char* name)
    {
        RHI_Device::Cmd()->begin_pass(name);
    }

    void RHI_CommandList::EndPass()
    {
        RHI_Device::Cmd()->end_pass();
    }

    void RHI_CommandList::SetPass(const char* name)
    {
        RHI_Device::Cmd()->set_pass(name);
    }

    void RHI_CommandList::SetShader(RHI_Shader* shader, const char* name)
    {
        RHI_Device::Cmd()->set_shader(shader, name);
    }

    void RHI_CommandList::SetShaders(RHI_Shader* shader_a, RHI_Shader* shader_b, RHI_Shader* shader_c)
    {
        RHI_Device::Cmd()->set_shaders(shader_a, shader_b, shader_c);
    }

    void RHI_CommandList::SetColorTarget(RHI_Texture* texture)
    {
        RHI_Device::Cmd()->set_color_target(texture);
    }

    void RHI_CommandList::SetColorTargets(
        RHI_Texture* t0,
        RHI_Texture* t1,
        RHI_Texture* t2,
        RHI_Texture* t3,
        RHI_Texture* t4,
        RHI_Texture* t5,
        RHI_Texture* t6,
        RHI_Texture* t7
    )
    {
        RHI_Device::Cmd()->set_color_targets(t0, t1, t2, t3, t4, t5, t6, t7);
    }

    void RHI_CommandList::SetDepthTarget(RHI_Texture* texture)
    {
        RHI_Device::Cmd()->set_depth_target(texture);
    }

    void RHI_CommandList::SetSwapChain(RHI_SwapChain* swapchain)
    {
        RHI_Device::Cmd()->set_swap_chain(swapchain);
    }

    void RHI_CommandList::SetBlendState(RHI_BlendState* state)
    {
        RHI_Device::Cmd()->set_blend_state(state);
    }

    void RHI_CommandList::SetRasterizerState(RHI_RasterizerState* state)
    {
        RHI_Device::Cmd()->set_rasterizer_state(state);
    }

    void RHI_CommandList::SetDepthStencilState(RHI_DepthStencilState* state)
    {
        RHI_Device::Cmd()->set_depth_stencil_state(state);
    }

    void RHI_CommandList::SetPrimitiveTopology(RHI_PrimitiveTopology topology)
    {
        RHI_Device::Cmd()->set_primitive_topology(topology);
    }

    void RHI_CommandList::SetClearColor(uint32_t index, const Color& color)
    {
        RHI_Device::Cmd()->set_clear_color(index, color);
    }

    void RHI_CommandList::SetClearDepth(float depth)
    {
        RHI_Device::Cmd()->set_clear_depth(depth);
    }

    void RHI_CommandList::SetVrsTexture(RHI_Texture* texture)
    {
        RHI_Device::Cmd()->set_vrs_texture(texture);
    }

    void RHI_CommandList::SetResolutionScale(bool enabled)
    {
        RHI_Device::Cmd()->set_resolution_scale(enabled);
    }

    void RHI_CommandList::SetMultiview(bool enabled)
    {
        RHI_Device::Cmd()->set_multiview(enabled);
    }

    void RHI_CommandList::SetArrayIndex(uint32_t index)
    {
        RHI_Device::Cmd()->set_array_index(index);
    }

    void RHI_CommandList::ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state)
    {
        RHI_Device::Cmd()->clear_pipeline_state_render_targets(pipeline_state);
    }

    void RHI_CommandList::ClearTexture(RHI_Texture* texture, const Color& clear_color, const float clear_depth, const uint32_t clear_stencil)
    {
        RHI_Device::Cmd()->clear_texture(texture, clear_color, clear_depth, clear_stencil);
    }

    void RHI_CommandList::Draw(const uint32_t vertex_count, const uint32_t vertex_start_index)
    {
        RHI_Device::Cmd()->draw(vertex_count, vertex_start_index);
    }

    void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset, const uint32_t instance_index, const uint32_t instance_count)
    {
        RHI_Device::Cmd()->draw_indexed(index_count, index_offset, vertex_offset, instance_index, instance_count);
    }

    void RHI_CommandList::DrawIndexedIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset, const uint32_t draw_count)
    {
        RHI_Device::Cmd()->draw_indexed_indirect(args_buffer, args_offset, draw_count);
    }

    void RHI_CommandList::DrawIndexedIndirectCount(RHI_Buffer* args_buffer, const uint32_t args_offset, RHI_Buffer* count_buffer, const uint32_t count_offset, const uint32_t max_draw_count)
    {
        RHI_Device::Cmd()->draw_indexed_indirect_count(args_buffer, args_offset, count_buffer, count_offset, max_draw_count);
    }

    void RHI_CommandList::DrawIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        RHI_Device::Cmd()->draw_indirect(args_buffer, args_offset);
    }

    void RHI_CommandList::DrawMeshTasksIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        RHI_Device::Cmd()->draw_mesh_tasks_indirect(args_buffer, args_offset);
    }

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        RHI_Device::Cmd()->dispatch(x, y, z);
    }

    void RHI_CommandList::Dispatch(RHI_CommandList* cmd_list, uint32_t x, uint32_t y, uint32_t z)
    {
        if (!cmd_list)
        {
            return;
        }
        cmd_list->dispatch(x, y, z);
    }

    void RHI_CommandList::Dispatch(RHI_Texture* texture, float resolution_scale)
    {
        RHI_Device::Cmd()->dispatch(texture, resolution_scale);
    }

    void RHI_CommandList::DispatchIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        RHI_Device::Cmd()->dispatch_indirect(args_buffer, args_offset);
    }

    void RHI_CommandList::TraceRays(const uint32_t width, const uint32_t height)
    {
        RHI_Device::Cmd()->trace_rays(width, height);
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float source_scaling)
    {
        RHI_Device::Cmd()->blit(source, destination, blit_mips, source_scaling);
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_SwapChain* destination)
    {
        RHI_Device::Cmd()->blit(source, destination);
    }

    void RHI_CommandList::BlitToArrayLayer(RHI_Texture* source, RHI_Texture* destination, uint32_t dst_layer)
    {
        RHI_Device::Cmd()->blit_to_array_layer(source, destination, dst_layer);
    }

    void RHI_CommandList::BlitToXrSwapchain(RHI_Texture* source)
    {
        RHI_Device::Cmd()->blit_to_xr_swapchain(source);
    }

    void RHI_CommandList::PrepareForPresent(RHI_SwapChain* swapchain)
    {
        RHI_Device::Cmd()->prepare_for_present(swapchain);
    }

    void RHI_CommandList::PrepareTextureForUpload(RHI_Texture* texture)
    {
        RHI_Device::Cmd()->prepare_texture_for_upload(texture);
    }

    void RHI_CommandList::PrepareTextureForUpload(RHI_CommandList* cmd_list, RHI_Texture* texture)
    {
        if (!cmd_list)
        {
            return;
        }
        cmd_list->prepare_texture_for_upload(texture);
    }

    void RHI_CommandList::PrepareTexturesForSampling(const std::array<RHI_Texture*, rhi_max_array_size>* textures)
    {
        RHI_Device::Cmd()->prepare_textures_for_sampling(textures);
    }

    void RHI_CommandList::PrepareTextureForCompute(RHI_Texture* texture)
    {
        RHI_CommandList* cmd_list = RHI_Device::Cmd();
        if (!cmd_list || !texture)
        {
            return;
        }

        cmd_list->EnsureComputeShaderResource(texture, true);
    }

    void RHI_CommandList::PrepareBufferForCompute(RHI_Buffer* buffer)
    {
        RHI_Device::Cmd()->prepare_buffer_for_compute(buffer);
    }

    void RHI_CommandList::PrepareBufferForCompute(RHI_CommandList* cmd_list, RHI_Buffer* buffer)
    {
        if (!cmd_list)
        {
            return;
        }
        cmd_list->prepare_buffer_for_compute(buffer);
    }

    void RHI_CommandList::PrepareBufferForReadback(RHI_Buffer* buffer)
    {
        RHI_Device::Cmd()->prepare_buffer_for_readback(buffer);
    }

    void RHI_CommandList::PrepareBufferForReadback(RHI_CommandList* cmd_list, RHI_Buffer* buffer)
    {
        if (!cmd_list)
        {
            return;
        }
        cmd_list->prepare_buffer_for_readback(buffer);
    }

    void RHI_CommandList::PrepareBufferForGraphics(RHI_Buffer* buffer)
    {
        RHI_Device::Cmd()->prepare_buffer_for_graphics(buffer);
    }

    void RHI_CommandList::Copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips)
    {
        RHI_Device::Cmd()->copy(source, destination, blit_mips);
    }

    void RHI_CommandList::Copy(RHI_Texture* source, RHI_SwapChain* destination)
    {
        RHI_Device::Cmd()->copy(source, destination);
    }

    void RHI_CommandList::SetViewport(const RHI_Viewport& viewport)
    {
        RHI_Device::Cmd()->set_viewport(viewport);
    }

    void RHI_CommandList::SetScissorRectangle(const math::Rectangle& scissor_rectangle)
    {
        RHI_Device::Cmd()->set_scissor_rectangle(scissor_rectangle);
    }

    void RHI_CommandList::SetCullMode(const RHI_CullMode cull_mode)
    {
        RHI_Device::Cmd()->set_cull_mode(cull_mode);
    }

    void RHI_CommandList::SetBufferVertex(const RHI_Buffer* vertex, RHI_Buffer* instance)
    {
        RHI_Device::Cmd()->set_buffer_vertex(vertex, instance);
    }

    void RHI_CommandList::SetBufferIndex(const RHI_Buffer* buffer)
    {
        RHI_Device::Cmd()->set_buffer_index(buffer);
    }

    void RHI_CommandList::SetBuffer(const uint32_t slot, RHI_Buffer* buffer)
    {
        RHI_Device::Cmd()->set_buffer(slot, buffer);
    }

    void RHI_CommandList::SetBuffer(RHI_CommandList* cmd_list, const uint32_t slot, RHI_Buffer* buffer)
    {
        if (!cmd_list)
        {
            return;
        }
        cmd_list->set_buffer(slot, buffer);
    }

    void RHI_CommandList::SetBuffer(const char* name, RHI_Buffer* buffer)
    {
        RHI_Device::Cmd()->set_buffer(name, buffer);
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, RHI_Buffer* constant_buffer)
    {
        RHI_Device::Cmd()->set_constant_buffer(slot, constant_buffer);
    }

    void RHI_CommandList::SetConstantBuffer(const char* name, RHI_Buffer* constant_buffer)
    {
        RHI_Device::Cmd()->set_constant_buffer(name, constant_buffer);
    }

    void RHI_CommandList::PushConstants(const uint32_t offset, const uint32_t size, const void* data)
    {
        RHI_Device::Cmd()->push_constants(offset, size, data);
    }

    void RHI_CommandList::PushConstants(RHI_CommandList* cmd_list, const uint32_t offset, const uint32_t size, const void* data)
    {
        if (!cmd_list || !cmd_list->m_pipeline)
        {
            return;
        }
        cmd_list->push_constants(offset, size, data);
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index, uint32_t mip_range, const bool uav, const uint32_t array_layer)
    {
        RHI_Device::Cmd()->set_texture(slot, texture, mip_index, mip_range, uav, array_layer);
    }

    void RHI_CommandList::SetTexture(const char* name, RHI_Texture* texture, const uint32_t mip_index, uint32_t mip_range, const uint32_t array_layer)
    {
        RHI_Device::Cmd()->set_texture(name, texture, mip_index, mip_range, array_layer);
    }

    void RHI_CommandList::SetAccelerationStructure(const uint32_t slot, RHI_AccelerationStructure* tlas)
    {
        RHI_Device::Cmd()->set_acceleration_structure(slot, tlas);
    }

    void RHI_CommandList::SetAccelerationStructure(const char* name, RHI_AccelerationStructure* tlas)
    {
        RHI_Device::Cmd()->set_acceleration_structure(name, tlas);
    }

    void RHI_CommandList::BeginMarker(const char* name)
    {
        RHI_Device::Cmd()->begin_marker(name);
    }

    void RHI_CommandList::EndMarker()
    {
        RHI_Device::Cmd()->end_marker();
    }

    void RHI_CommandList::WriteGpuBreadcrumb(RHI_Buffer* buffer, uint32_t slot, uint32_t value)
    {
        RHI_Device::Cmd()->write_gpu_breadcrumb(buffer, slot, value);
    }

    uint32_t RHI_CommandList::BeginTimestamp()
    {
        return RHI_Device::Cmd()->begin_timestamp();
    }

    uint32_t RHI_CommandList::EndTimestamp()
    {
        return RHI_Device::Cmd()->end_timestamp();
    }

    void RHI_CommandList::BeginOcclusionQuery(const uint64_t entity_id)
    {
        RHI_Device::Cmd()->begin_occlusion_query(entity_id);
    }

    void RHI_CommandList::EndOcclusionQuery()
    {
        RHI_Device::Cmd()->end_occlusion_query();
    }

    void RHI_CommandList::UpdateOcclusionQueries()
    {
        RHI_Device::Cmd()->update_occlusion_queries();
    }

    void RHI_CommandList::BeginTimeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        RHI_CommandList* cmd_list = RHI_Device::Cmd();
        SP_ASSERT(cmd_list != nullptr);
        cmd_list->begin_timeblock(name, gpu_marker, gpu_timing);
        timeblock_cmd_lists.push(cmd_list);
    }

    void RHI_CommandList::EndTimeblock()
    {
        SP_ASSERT(!timeblock_cmd_lists.empty());
        RHI_CommandList* cmd_list = timeblock_cmd_lists.top();
        timeblock_cmd_lists.pop();
        cmd_list->end_timeblock();
    }

    void RHI_CommandList::UpdateBuffer(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const void* data, const bool use_mapped_memory)
    {
        RHI_Device::Cmd()->update_buffer(buffer, offset, size, data, use_mapped_memory);
    }

    void RHI_CommandList::RenderPassEnd()
    {
        RHI_Device::Cmd()->render_pass_end();
    }

    void RHI_CommandList::RestoreAfterExternalPass()
    {
        RHI_Device::Cmd()->restore_after_external_pass();
    }

    void RHI_CommandList::CopyTextureToBuffer(RHI_Texture* source, RHI_Buffer* destination)
    {
        RHI_Device::Cmd()->copy_texture_to_buffer(source, destination);
    }

    void RHI_CommandList::CopyBufferToBuffer(void* source, RHI_Buffer* destination, uint64_t size)
    {
        RHI_Device::Cmd()->copy_buffer_to_buffer(source, destination, size);
    }

    void RHI_CommandList::CopyBufferToBuffer(RHI_Buffer* source, RHI_Buffer* destination, uint64_t size)
    {
        RHI_Device::Cmd()->copy_buffer_to_buffer(source, destination, size);
    }

    void RHI_CommandList::CopyBufferToBuffer(RHI_CommandList* cmd_list, RHI_Buffer* source, RHI_Buffer* destination, uint64_t size)
    {
        if (!cmd_list)
        {
            return;
        }
        cmd_list->copy_buffer_to_buffer(source, destination, size);
    }
}
