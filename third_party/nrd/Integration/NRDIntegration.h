/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

// Dependencies
#include <array>
#include <map>
#include <vector>

#ifndef NRD_VERSION_MAJOR
#    error "NRD.h" is not included
#endif

#ifndef NRI_VERSION
#    error "NRI.h" is not included
#endif

#ifndef NRI_HELPER_H
#    error "Extensions/NRIHelper.h" is not included
#endif

// Debugging
#ifdef NRD_INTEGRATION_DEBUG_LOGGING
#    include <stdio.h>
#endif

#ifndef NRD_INTEGRATION_ASSERT
#    ifdef _DEBUG
#        include <assert.h>
#        define NRD_INTEGRATION_ASSERT(expr, msg) assert(msg&& expr)
#    else
#        define NRD_INTEGRATION_ASSERT(expr, msg) (void)(expr)
#    endif
#endif

// NRI-based NRD integration layer
#define NRD_INTEGRATION_VERSION 22
#define NRD_INTEGRATION_DATE "6 April 2026"

namespace nrd {

//===================================================================================================
// Texture
//===================================================================================================

// For "Recreate" and "Denoise"
struct TextureNRI {
    nri::Texture* texture;
    uint32_t dummy;
};

// For "RecreateD3D11" and "DenoiseD3D11"
#ifdef NRI_WRAPPER_D3D11_H
struct TextureD3D11 {
    ID3D11Resource* resource;
    DXGIFormat format; // (optional) needed only if the resource is typeless
};
#endif

// For "RecreateD3D12" and "DenoiseD3D12"
#ifdef NRI_WRAPPER_D3D12_H
struct TextureD3D12 {
    ID3D12Resource* resource;
    DXGIFormat format; // (optional) needed only if the resource is typeless
};
#endif

// For "RecreateVK" and "DenoiseVK"
#ifdef NRI_WRAPPER_VK_H
struct TextureVK {
    VKNonDispatchableHandle image;
    VKEnum format;
};
#endif

//===================================================================================================
// Resource = texture + state
//===================================================================================================

struct Resource {
    // FOR INTERNAL USE ONLY
    union {
        TextureNRI nri = {};
#ifdef NRI_WRAPPER_D3D11_H
        TextureD3D11 d3d11;
#endif
#ifdef NRI_WRAPPER_D3D12_H
        TextureD3D12 d3d12;
#endif
#ifdef NRI_WRAPPER_VK_H
        TextureVK vk;
#endif
    };

    // Current state, which the resource has been left in
    nri::AccessLayoutStage state = {};

    // (Optional) Unused by the integration, but can be used to assosiate "state" with an app resource.
    // App-side resource states must be updated if "restoreInitialState = false"
    void* userArg = nullptr;
};

//===================================================================================================
// Resource snapshot = collection of resources
//===================================================================================================

// Represents the state of resources at the current moment:
//  - must contain valid entries for "resource types" referenced by a "Denoise" call
//  - if you know what you do, same resource may be used several times for different slots
//  - if "restoreInitialState" is "false":
//      - "Denoise" call modifies resource states, use "userArg" to assosiate "state" with an app resource
//      - update app-side resource states using "Resources::unique[0:uniqueNum]" entries
struct ResourceSnapshot {
    // FOR INTERNAL USE ONLY
    std::array<Resource*, (size_t)ResourceType::MAX_NUM - 2> slots = {};

    // Contain final state of resources after "Denoise" call
    std::array<Resource, (size_t)ResourceType::MAX_NUM - 2> unique = {};

    // for (i = 0; i < uniqueNum; i++) { Use(Resources::unique[i]); }
    size_t uniqueNum = 0;

    // "Denoise" input parameter
    bool restoreInitialState = false;

    // Texture requirements: 2D, 1 layer, 1 mip
    inline void SetResource(ResourceType slot, const Resource& resource) {
        for (size_t i = 0; i < unique.size(); i++) {
            Resource& entry = unique[i];

            if (entry.nri.texture == resource.nri.texture) {
                // Already in list
                NRD_INTEGRATION_ASSERT(entry.state.access == resource.state.access, "Same resource but different 'access'!");
                NRD_INTEGRATION_ASSERT(entry.state.layout == resource.state.layout, "Same resource but different 'layout'!");
                NRD_INTEGRATION_ASSERT(entry.state.stages == resource.state.stages, "Same resource but different 'stages'!");
                NRD_INTEGRATION_ASSERT(entry.userArg == resource.userArg, "Same resource but different 'userArg'!");

                slots[(size_t)slot] = &entry;

                return;
            } else if (!entry.nri.texture) {
                // Add new entry
                entry = resource;

                slots[(size_t)slot] = &entry;
                uniqueNum = i + 1;

                return;
            }
        }

        NRD_INTEGRATION_ASSERT(false, "Unexpected!");
    }

    // The structure stores pointers to itself, thus can't be copied
    ResourceSnapshot(const ResourceSnapshot&) = delete;
    ResourceSnapshot& operator=(const ResourceSnapshot&) = delete;
    ResourceSnapshot() = default;
};

//===================================================================================================
// Integration instance
//===================================================================================================

struct IntegrationCreationDesc {
    // Not so long name
    char name[64] = "";

    // Resource allocation priority (most likely 1 allocation per integration instance)
    float residencyPriority; // [-1; 1]: low < 0, normal = 0, high > 0

    // Resource dimensions
    uint16_t resourceWidth = 0;
    uint16_t resourceHeight = 0;

    // (1-3 usually) the application must provide number of queued frames, it's needed to guarantee
    // that constant data and descriptor sets are not overwritten while being executed on the GPU
    uint8_t queuedFrameNum = 3;

    // false - descriptors are cached only within a single "Denoise" call. The app must not
    //         destroy NRD related resources if there is NRD work in-flight!
    // true - enables descriptor caching for the whole lifetime of the Integration instance.
    //        The app must not destroy NRD related resources during this time, since created
    //        under-the-hood descriptors (views) will reference destroyed resources!
    //        App can call "DestroyCachedDescriptors" to avoid destroying the whole NRD instance.
    bool enableWholeLifetimeDescriptorCaching = false;

    // Wait for idle on GRAPHICS/COMPUTE queues in mandatory places (for lazy people)
    bool autoWaitForIdle = true;

    // Demote FP32 to FP16 (slightly improves performance in exchange of precision loss)
    // (FP32 is used only for viewZ under the hood, all denoisers are FP16 compatible)
    bool demoteFloat32to16 = false;

    // Promote FP16 to FP32 (overkill, kills performance)
    bool promoteFloat16to32 = false;

    // TODO: there can be an option for UNORM/SNORM 8/16-bit promotion/demotion, but only for resources
    // marked as history (i.e. not normal-roughness and internal data resources)
};

// Threadsafe: no
struct Integration {
    inline Integration() {
    }

    // Expects alive device
    inline ~Integration() {
        Destroy();
    }

    // Creation and re-creation, aka resize. "Destroy" is called under the hood
    Result Recreate(const IntegrationCreationDesc& nrdIntegrationDesc, const InstanceCreationDesc& instanceCreationDesc, nri::Device* device);
#ifdef NRI_WRAPPER_D3D11_H
    Result RecreateD3D11(const IntegrationCreationDesc& nrdIntegrationDesc, const InstanceCreationDesc& instanceCreationDesc, const nri::DeviceCreationD3D11Desc& deviceCreationD3D11Desc);
#endif
#ifdef NRI_WRAPPER_D3D12_H
    Result RecreateD3D12(const IntegrationCreationDesc& nrdIntegrationDesc, const InstanceCreationDesc& instanceCreationDesc, const nri::DeviceCreationD3D12Desc& deviceCreationD3D12Desc);
#endif
#ifdef NRI_WRAPPER_VK_H
    Result RecreateVK(const IntegrationCreationDesc& nrdIntegrationDesc, const InstanceCreationDesc& instanceCreationDesc, const nri::DeviceCreationVKDesc& deviceCreationVKDesc);
#endif

    // Must be called once on a frame start
    void NewFrame();

    // Must be used instead of eponymous NRD API functions
    Result SetCommonSettings(const CommonSettings& commonSettings);
    Result SetDenoiserSettings(Identifier denoiser, const void* denoiserSettings);

    // Invoke denoising for specified denoisers. This function binds own descriptor heap (pool).
    // After the call "resourceSnapshot" entries will represent the "final" state of resources,
    // which must be used as "before" state in next "barrier" calls. The initial state of resources
    // can be restored by using "resourceSnapshot.restoreInitialState = true" (suboptimal).
    void Denoise(const Identifier* denoisers, uint32_t denoisersNum, nri::CommandBuffer& commandBuffer, ResourceSnapshot& resourceSnapshot);
#ifdef NRI_WRAPPER_D3D11_H
    void DenoiseD3D11(const Identifier* denoisers, uint32_t denoisersNum, const nri::CommandBufferD3D11Desc& commandBufferD3D11Desc, ResourceSnapshot& resourceSnapshot);
#endif
#ifdef NRI_WRAPPER_D3D12_H
    void DenoiseD3D12(const Identifier* denoisers, uint32_t denoisersNum, const nri::CommandBufferD3D12Desc& commandBufferD3D12Desc, ResourceSnapshot& resourceSnapshot);
#endif
#ifdef NRI_WRAPPER_VK_H
    void DenoiseVK(const Identifier* denoisers, uint32_t denoisersNum, const nri::CommandBufferVKDesc& commandBufferVKDesc, ResourceSnapshot& resourceSnapshot);
#endif

    // Destroy.
    // Device should have no NRD work in flight if "autoWaitForIdle = false"!
    void Destroy();

    // (Optional) Destroy cached descriptors. It's called automatically under the hood, but can be used if app is going to recreate an NRD related resource.
    // Device should have no NRD work in flight if "autoWaitForIdle = false"!
    void DestroyCachedDescriptors();

    // (Optional) Called under the hood, but can be used to explicitly reload pipelines.
    // Device should have no NRD work in flight if "autoWaitForIdle = false"!
    bool RecreatePipelines();

    // (Optional) Statistics
    inline double GetTotalMemoryUsageInMb() const {
        return double(m_PermanentPoolSize + m_TransientPoolSize) / (1024.0 * 1024.0);
    }

    inline double GetPersistentMemoryUsageInMb() const {
        return double(m_PermanentPoolSize) / (1024.0 * 1024.0);
    }

    inline double GetAliasableMemoryUsageInMb() const {
        return double(m_TransientPoolSize) / (1024.0 * 1024.0);
    }

private:
    Integration(const Integration&) = delete;

    bool _CreateResources();
    void _Dispatch(nri::CommandBuffer& commandBuffer, nri::DescriptorPool& descriptorPool, const DispatchDesc& dispatchDesc, ResourceSnapshot& resourceSnapshot);
    void _WaitForIdle();

    std::vector<Resource> m_TexturePool;
    std::vector<nri::Pipeline*> m_Pipelines;
    std::vector<nri::Memory*> m_MemoryAllocations;
    std::vector<nri::DescriptorPool*> m_DescriptorPools = {};
    std::vector<std::vector<nri::Descriptor*>> m_DescriptorsInFlight;
    std::map<uint64_t, nri::Descriptor*> m_CachedDescriptors;
    IntegrationCreationDesc m_Desc = {};
    nri::CoreInterface m_iCore = {};
#ifdef NRI_WRAPPER_D3D11_H
    nri::WrapperD3D11Interface m_iWrapperD3D11 = {};
#endif
#ifdef NRI_WRAPPER_D3D12_H
    nri::WrapperD3D12Interface m_iWrapperD3D12 = {};
#endif
#ifdef NRI_WRAPPER_VK_H
    nri::WrapperVKInterface m_iWrapperVK = {};
#endif
    nri::Device* m_Device = nullptr;
    nri::Buffer* m_ConstantBuffer = nullptr;
    nri::Descriptor* m_ConstantBufferView = nullptr;
    nri::PipelineLayout* m_PipelineLayout = nullptr;
#ifdef NRD_INTEGRATION_DEBUG_LOGGING
    FILE* m_Log = nullptr;
#endif
    Instance* m_Instance = nullptr;
    uint64_t m_PermanentPoolSize = 0;
    uint64_t m_TransientPoolSize = 0;
    uint64_t m_ConstantBufferSize = 0;
    uint32_t m_ConstantBufferViewSize = 0;
    uint32_t m_ConstantBufferOffset = 0;
    uint32_t m_ConstantBufferOffsetPrev = 0;
    uint32_t m_DescriptorPoolIndex = 0;
    uint32_t m_FrameIndex = uint32_t(-1); // 0 needed after 1st "NewFrame"
    uint32_t m_PrevFrameIndexFromSettings = 0;
    nri::GraphicsAPI m_Wrapped = nri::GraphicsAPI::NONE;
    bool m_SkipDestroy = false;
};

} // namespace nrd
