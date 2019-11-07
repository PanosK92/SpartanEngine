/*
Copyright(c) 2016-2019 Panos Karabelas

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

#pragma once

//= INCLUDES =================
#include "RHI_Shader.h"
#include "RHI_PipelineCache.h"
//============================

namespace Spartan
{
	class RHI_Pipeline
	{
	public:
		RHI_Pipeline() = default;
		RHI_Pipeline(Renderer* renderer, const RHI_PipelineState& pipeline_state);
		~RHI_Pipeline();

        void OnCommandListConsumed();
        void SetConstantBuffer(uint32_t slot, RHI_ConstantBuffer* constant_buffer);
        void SetSampler(uint32_t slot, RHI_Sampler* sampler);
        void SetTexture(uint32_t slot, RHI_Texture* texture);
		auto GetPipeline()          const { return m_pipeline; }
		auto GetPipelineLayout()    const { return m_pipeline_layout; }
		auto GetState()             const { return m_state; }
        void* GetDescriptorPendingUpdate();

	private:
        uint32_t GetDescriptorBlueprintHash(const std::vector<RHI_Descriptor>& descriptor_blueprint);
        void* UpdateDescriptorSet();
		bool CreateDescriptorPool();
		bool CreateDescriptorSetLayout();
        void ReflectShaders();

        // Descriptors
        const uint32_t m_constant_buffer_max    = 10;
        const uint32_t m_sampler_max            = 10;
        const uint32_t m_texture_max            = 10;
        uint32_t m_descriptor_capacity          = 20;
        // Descriptors - Acts as a blueprint and is left untouched after being filled by ReflectShaders().
        std::vector<RHI_Descriptor> m_descriptor_blueprint;
        // Hash(type, slot, id) > Descriptor - Acts as a the API's descriptor cache.
        std::map<uint32_t, void*> m_descriptors_cache;

        // Dependencies
        Renderer* m_renderer = nullptr;
        std::shared_ptr<RHI_Device> m_rhi_device;
        const RHI_PipelineState* m_state = nullptr;

		// API
		void* m_pipeline					= nullptr;
		void* m_pipeline_layout				= nullptr;
		void* m_descriptor_pool				= nullptr;
		void* m_descriptor_set_layout		= nullptr;
    };
}
