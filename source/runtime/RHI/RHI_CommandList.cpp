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
#include "RHI_Device.h"
#include <unordered_set>
//============================

//= NAMESPACES ========
using namespace std;
//=====================

namespace spartan
{
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
        m_resources_dirty = true;
        binding = {};
        if (!texture)
        {
            return;
        }
        m_resources_have_write_bindings |= uav;

        binding.texture     = texture;
        binding.mip_index   = mip_index == rhi_all_mips ? 0 : mip_index;
        binding.mip_range   = mip_index == rhi_all_mips ? texture->GetMipCount() : (mip_range == 0 ? 1 : mip_range);
        binding.array_layer = array_layer;
        binding.access      = uav ? RHI_Resource_Access::ReadWrite : RHI_Resource_Access::Read;
        binding.layout      = uav ? RHI_Image_Layout::General : RHI_Image_Layout::Shader_Read;

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
            usages[mip].access = access;
            usages[mip].usage  = usage;
            usages[mip].scope  = scope;
            usages[mip].queue  = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
            usages[mip].layout = layout;
        }
    }

    void RHI_CommandList::PrepareForExternalWrite(RHI_Texture* texture, const RHI_Image_Layout layout, const RHI_Barrier_Scope scope)
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
            }
        }

        for (uint32_t mip = 0; mip < texture->GetMipCount(); mip++)
        {
            const RHI_Tracked_Usage& previous = history_it->second[mip];
            const RHI_Queue_Type current_queue = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
            const bool cross_queue = previous.queue != RHI_Queue_Type::Max && current_queue != RHI_Queue_Type::Max && previous.queue != current_queue;
            if (GetTrackedTextureLayout(texture, mip) != layout)
            {
                RHI_Barrier barrier = RHI_Barrier::image_layout(texture, layout, mip, 1);
                barrier.from(cross_queue ? RHI_Barrier_Scope::None : (previous.access == RHI_Resource_Access::None ? RHI_Barrier_Scope::All : previous.scope)).to(scope);
                barrier.access_src = cross_queue ? RHI_Resource_Access::None : previous.access;
                barrier.access_dst = RHI_Resource_Access::Write;
                barrier.usage_src  = cross_queue ? RHI_Resource_Usage::None : previous.usage;
                barrier.usage_dst  = layout == RHI_Image_Layout::Attachment ? RHI_Resource_Usage::Attachment : (layout == RHI_Image_Layout::Transfer_Destination ? RHI_Resource_Usage::Transfer : RHI_Resource_Usage::Shader);
                InsertBarrier(barrier);
            }
            else if (previous.access != RHI_Resource_Access::None)
            {
                RHI_Barrier barrier = RHI_Barrier::image_sync(texture, previous.access, RHI_Resource_Access::Write, mip, 1);
                barrier.from(cross_queue ? RHI_Barrier_Scope::None : previous.scope).to(scope);
                barrier.access_src = cross_queue ? RHI_Resource_Access::None : previous.access;
                barrier.usage_src = cross_queue ? RHI_Resource_Usage::None : previous.usage;
                barrier.usage_dst = layout == RHI_Image_Layout::Attachment ? RHI_Resource_Usage::Attachment : (layout == RHI_Image_Layout::Transfer_Destination ? RHI_Resource_Usage::Transfer : RHI_Resource_Usage::Shader);
                InsertBarrier(barrier);
            }
        }
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
                return m_pso.use_standard_resources;
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
                binding.layout    = RHI_Image_Layout::Attachment;
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
            binding.layout    = RHI_Image_Layout::Attachment;
        }

        if (m_pso.vrs_input_texture)
        {
            RHI_Tracked_Texture_Binding& binding = m_tracked_attachments[attachment_index];
            binding.texture   = m_pso.vrs_input_texture;
            binding.mip_range = 1;
            binding.access    = RHI_Resource_Access::Read;
            binding.usage     = RHI_Resource_Usage::ShadingRate;
            binding.layout    = RHI_Image_Layout::Shading_Rate_Attachment;
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
                }
            }

            auto& previous_usages = history_it->second;
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
                const bool layout_transition = current.access != RHI_Resource_Access::None && GetTrackedTextureLayout(texture, mip) != current.layout;
                if (layout_transition)
                {
                    const bool cross_queue = previous.queue != RHI_Queue_Type::Max && current.queue != RHI_Queue_Type::Max && previous.queue != current.queue;
                    RHI_Barrier barrier = RHI_Barrier::image_layout(texture, current.layout, mip, 1);
                    barrier.from(cross_queue ? RHI_Barrier_Scope::None : (previous.access == RHI_Resource_Access::None ? RHI_Barrier_Scope::All : previous.scope)).to(current.scope);
                    barrier.access_src = cross_queue ? RHI_Resource_Access::None : previous.access;
                    barrier.access_dst = current.access;
                    barrier.usage_src  = cross_queue ? RHI_Resource_Usage::None : previous.usage;
                    barrier.usage_dst  = current.usage;
                    InsertBarrier(barrier);
                }
                const bool hazard = previous.access != RHI_Resource_Access::None && current.access != RHI_Resource_Access::None && (resource_tracker::writes(previous.access) || resource_tracker::writes(current.access));
                const bool needs_barrier = hazard && previous.layout == current.layout && !layout_transition;
                const bool extends_range = barrier_start != rhi_all_mips && needs_barrier && previous.access == barrier_previous.access && previous.usage == barrier_previous.usage && previous.scope == barrier_previous.scope && previous.queue == barrier_previous.queue && current.access == barrier_current.access && current.usage == barrier_current.usage && current.scope == barrier_current.scope && current.queue == barrier_current.queue;

                if (!extends_range)
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
                }
            }

            RHI_Tracked_Usage& previous = history_it->second;
            const bool wrote = resource_tracker::writes(previous.access) || resource_tracker::writes(current.access);
            const bool compute_to_graphics =
                previous.scope == RHI_Barrier_Scope::Compute &&
                current.scope == RHI_Barrier_Scope::Graphics &&
                resource_tracker::writes(previous.access);
            if (previous.access != RHI_Resource_Access::None && (wrote || compute_to_graphics))
            {
                const bool cross_queue = previous.queue != RHI_Queue_Type::Max && current.queue != RHI_Queue_Type::Max && previous.queue != current.queue;
                RHI_Barrier barrier = RHI_Barrier::buffer_sync(buffer, previous.access, current.access);
                barrier.from(cross_queue ? RHI_Barrier_Scope::None : previous.scope).to(current.scope);
                barrier.access_src = cross_queue ? RHI_Resource_Access::None : previous.access;
                barrier.usage_src = cross_queue ? RHI_Resource_Usage::None : previous.usage;
                barrier.usage_dst = current.usage;
                InsertBarrier(barrier);
            }
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

    void RHI_CommandList::PrepareForPresent(RHI_SwapChain* swapchain)
    {
        if (swapchain)
        {
            InsertBarrier(swapchain->GetRhiRt(), swapchain->GetFormat(), 0, 1, 1, RHI_Image_Layout::Present_Source);
        }
    }

    void RHI_CommandList::PrepareTextureForUpload(RHI_Texture* texture)
    {
        if (texture)
        {
            InsertBarrier(texture, RHI_Image_Layout::Transfer_Destination, 0, texture->GetMipCount());
            FlushBarriers();
        }
    }

    void RHI_CommandList::PrepareTextureForSampling(RHI_Texture* texture)
    {
        if (texture && texture->GetRhiResource() && texture->GetResourceState() == ResourceState::PreparedForGpu)
        {
            InsertBarrier(texture, RHI_Image_Layout::Shader_Read, 0, texture->GetMipCount());
            TrackExternalTextureUsage(texture, RHI_Resource_Access::Read, RHI_Image_Layout::Shader_Read, RHI_Barrier_Scope::All);
        }
    }

    void RHI_CommandList::PrepareTexturesForSampling(const array<RHI_Texture*, rhi_max_array_size>* textures)
    {
        if (textures)
        {
            for (RHI_Texture* texture : *textures)
            {
                PrepareTextureForSampling(texture);
            }
        }
    }

    void RHI_CommandList::PrepareBufferForCompute(RHI_Buffer* buffer)
    {
        if (buffer)
        {
            InsertBarrier(RHI_Barrier::buffer_sync(buffer).from(RHI_Barrier_Scope::Transfer).to(RHI_Barrier_Scope::Compute));
            FlushBarriers();
        }
    }

    void RHI_CommandList::PrepareBufferForReadback(RHI_Buffer* buffer)
    {
        if (buffer)
        {
            InsertBarrier(RHI_Barrier::buffer_sync(buffer).from(RHI_Barrier_Scope::Compute).to(RHI_Barrier_Scope::Transfer));
            FlushBarriers();
        }
    }

    void RHI_CommandList::PrepareBufferForGraphics(RHI_Buffer* buffer)
    {
        if (buffer)
        {
            InsertBarrier(RHI_Barrier::buffer_sync(buffer).from(RHI_Barrier_Scope::Compute).to(RHI_Barrier_Scope::Graphics));
            FlushBarriers();
        }
    }

    void RHI_CommandList::Dispatch(RHI_Texture* texture, float resolution_scale /*= 1.0f*/)
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

        Dispatch(dispatch_x, dispatch_y, dispatch_z);
    }
}
