// This file is part of the FidelityFX SDK.
//
// Copyright (c) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "../ffx_fsr2.h"
#include "ffx_fsr2_vk.h"
#include "shaders/ffx_fsr2_shaders_vk.h"  // include all the precompiled VK shaders for the FSR2 passes
#include "../ffx_fsr2_private.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <codecvt>

// prototypes for functions in the interface
FfxErrorCode GetDeviceCapabilitiesVK(FfxFsr2Interface* backendInterface, FfxDeviceCapabilities* deviceCapabilities, FfxDevice device);
FfxErrorCode CreateBackendContextVK(FfxFsr2Interface* backendInterface, FfxDevice device);
FfxErrorCode DestroyBackendContextVK(FfxFsr2Interface* backendInterface);
FfxErrorCode CreateResourceVK(FfxFsr2Interface* backendInterface, const FfxCreateResourceDescription* desc, FfxResourceInternal* outResource);
FfxErrorCode RegisterResourceVK(FfxFsr2Interface* backendInterface, const FfxResource* inResource, FfxResourceInternal* outResourceInternal);
FfxErrorCode UnregisterResourcesVK(FfxFsr2Interface* backendInterface);
FfxResourceDescription GetResourceDescriptorVK(FfxFsr2Interface* backendInterface, FfxResourceInternal resource);
FfxErrorCode DestroyResourceVK(FfxFsr2Interface* backendInterface, FfxResourceInternal resource);
FfxErrorCode CreatePipelineVK(FfxFsr2Interface* backendInterface, FfxFsr2Pass passId, const FfxPipelineDescription* desc, FfxPipelineState* outPass);
FfxErrorCode DestroyPipelineVK(FfxFsr2Interface* backendInterface, FfxPipelineState* pipeline);
FfxErrorCode ScheduleGpuJobVK(FfxFsr2Interface* backendInterface, const FfxGpuJobDescription* job);
FfxErrorCode ExecuteGpuJobsVK(FfxFsr2Interface* backendInterface, FfxCommandList commandList);

#define FSR2_MAX_QUEUED_FRAMES              ( 4)
#define FSR2_MAX_RESOURCE_COUNT             (64)
#define FSR2_MAX_STAGING_RESOURCE_COUNT     ( 8)
#define FSR2_MAX_BARRIERS                   (16)
#define FSR2_MAX_GPU_JOBS                   (32)
#define FSR2_MAX_IMAGE_COPY_MIPS            (32)
#define FSR2_MAX_SAMPLERS                   ( 2)
#define FSR2_MAX_UNIFORM_BUFFERS            ( 4)
#define FSR2_MAX_IMAGE_VIEWS                (32)
#define FSR2_MAX_BUFFERED_DESCRIPTORS       (FFX_FSR2_PASS_COUNT * FSR2_MAX_QUEUED_FRAMES)
#define FSR2_UBO_RING_BUFFER_SIZE           (FSR2_MAX_BUFFERED_DESCRIPTORS * FSR2_MAX_UNIFORM_BUFFERS)
#define FSR2_UBO_MEMORY_BLOCK_SIZE          (FSR2_UBO_RING_BUFFER_SIZE * 256)

typedef struct BackendContext_VK {

    // store for resources and resourceViews
    typedef struct Resource
    {
#ifdef _DEBUG
        char                    resourceName[64] = {};
#endif
        VkImage                 imageResource;
        VkImageAspectFlags      aspectFlags;
        VkBuffer                bufferResource;
        VkDeviceMemory          deviceMemory;
        VkMemoryPropertyFlags   memoryProperties;
        FfxResourceDescription  resourceDescription;
        FfxResourceStates       state;
        VkImageView             allMipsImageView;
        VkImageView             singleMipImageViews[FSR2_MAX_IMAGE_VIEWS];
        bool                    undefined;
    } Resource;

    typedef struct UniformBuffer
    {
        VkBuffer bufferResource;
        uint8_t* pData;
    } UniformBuffer;

    typedef struct PipelineLayout
    {
        VkDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet       descriptorSets[FSR2_MAX_QUEUED_FRAMES];
        uint32_t              descriptorSetIndex;
        VkPipelineLayout      pipelineLayout;
    } PipelineLayout;

    typedef struct VKFunctionTable
    {
        PFN_vkGetDeviceProcAddr             vkGetDeviceProcAddr = 0;
        PFN_vkSetDebugUtilsObjectNameEXT    vkSetDebugUtilsObjectNameEXT = 0;
        PFN_vkCreateDescriptorPool          vkCreateDescriptorPool = 0;
        PFN_vkCreateSampler                 vkCreateSampler = 0;
        PFN_vkCreateDescriptorSetLayout     vkCreateDescriptorSetLayout = 0;
        PFN_vkCreateBuffer                  vkCreateBuffer = 0;
        PFN_vkCreateImage                   vkCreateImage = 0;
        PFN_vkCreateImageView               vkCreateImageView = 0;
        PFN_vkCreateShaderModule            vkCreateShaderModule = 0;
        PFN_vkCreatePipelineLayout          vkCreatePipelineLayout = 0;
        PFN_vkCreateComputePipelines        vkCreateComputePipelines = 0;
        PFN_vkDestroyPipelineLayout         vkDestroyPipelineLayout = 0;
        PFN_vkDestroyPipeline               vkDestroyPipeline = 0;
        PFN_vkDestroyImage                  vkDestroyImage = 0;
        PFN_vkDestroyImageView              vkDestroyImageView = 0;
        PFN_vkDestroyBuffer                 vkDestroyBuffer = 0;
        PFN_vkDestroyDescriptorSetLayout    vkDestroyDescriptorSetLayout = 0;
        PFN_vkDestroyDescriptorPool         vkDestroyDescriptorPool = 0;
        PFN_vkDestroySampler                vkDestroySampler = 0;
        PFN_vkDestroyShaderModule           vkDestroyShaderModule = 0;
        PFN_vkGetBufferMemoryRequirements   vkGetBufferMemoryRequirements = 0;
        PFN_vkGetImageMemoryRequirements    vkGetImageMemoryRequirements = 0;
        PFN_vkAllocateDescriptorSets        vkAllocateDescriptorSets = 0;
        PFN_vkAllocateMemory                vkAllocateMemory = 0;
        PFN_vkFreeMemory                    vkFreeMemory = 0;
        PFN_vkMapMemory                     vkMapMemory = 0;
        PFN_vkUnmapMemory                   vkUnmapMemory = 0;
        PFN_vkBindBufferMemory              vkBindBufferMemory = 0;
        PFN_vkBindImageMemory               vkBindImageMemory = 0;
        PFN_vkUpdateDescriptorSets          vkUpdateDescriptorSets = 0;
        PFN_vkFlushMappedMemoryRanges       vkFlushMappedMemoryRanges = 0;
        PFN_vkCmdPipelineBarrier            vkCmdPipelineBarrier = 0;
        PFN_vkCmdBindPipeline               vkCmdBindPipeline = 0;
        PFN_vkCmdBindDescriptorSets         vkCmdBindDescriptorSets = 0;
        PFN_vkCmdDispatch                   vkCmdDispatch = 0;
        PFN_vkCmdCopyBuffer                 vkCmdCopyBuffer = 0;
        PFN_vkCmdCopyImage                  vkCmdCopyImage = 0;
        PFN_vkCmdCopyBufferToImage          vkCmdCopyBufferToImage = 0;
        PFN_vkCmdClearColorImage            vkCmdClearColorImage = 0;
    } VkFunctionTable;

    VkPhysicalDevice        physicalDevice = nullptr;
    VkDevice                device = nullptr;
    VkFunctionTable         vkFunctionTable = {};
                            
    uint32_t                gpuJobCount = 0;
    FfxGpuJobDescription    gpuJobs[FSR2_MAX_GPU_JOBS] = {};

    uint32_t                nextStaticResource = 0;
    uint32_t                nextDynamicResource = 0;
    uint32_t                stagingResourceCount = 0;
    Resource                resources[FSR2_MAX_RESOURCE_COUNT] = {};
    FfxResourceInternal     stagingResources[FSR2_MAX_STAGING_RESOURCE_COUNT] = {};

    VkDescriptorPool        descPool = nullptr;
    VkDescriptorSetLayout   samplerDescriptorSetLayout = nullptr;
    VkDescriptorSet         samplerDescriptorSet = nullptr;
    uint32_t                allocatedPipelineLayoutCount = 0;
    PipelineLayout          pipelineLayouts[FFX_FSR2_PASS_COUNT] = {};
    VkSampler               pointSampler = nullptr;
    VkSampler               linearSampler = nullptr;
    
    VkDeviceMemory          uboMemory = nullptr;
    VkMemoryPropertyFlags   uboMemoryProperties = 0;
    UniformBuffer           uboRingBuffer[FSR2_UBO_RING_BUFFER_SIZE] = {};
    uint32_t                uboRingBufferIndex = 0;
 
    VkImageMemoryBarrier    imageMemoryBarriers[FSR2_MAX_BARRIERS] = {};
    VkBufferMemoryBarrier   bufferMemoryBarriers[FSR2_MAX_BARRIERS] = {};
    uint32_t                scheduledImageBarrierCount = 0;
    uint32_t                scheduledBufferBarrierCount = 0;
    VkPipelineStageFlags    srcStageMask = 0;
    VkPipelineStageFlags    dstStageMask = 0;

    uint32_t                numDeviceExtensions = 0;
    VkExtensionProperties*  extensionProperties = nullptr;

} BackendContext_VK;

FFX_API size_t ffxFsr2GetScratchMemorySizeVK(VkPhysicalDevice physicalDevice)
{
    uint32_t numExtensions = 0;
    
    if (physicalDevice)
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &numExtensions, nullptr);

    return FFX_ALIGN_UP(sizeof(BackendContext_VK) + sizeof(VkExtensionProperties) * numExtensions, sizeof(uint64_t));
}

FfxErrorCode ffxFsr2GetInterfaceVK(
    FfxFsr2Interface* outInterface,
    void* scratchBuffer,
    size_t scratchBufferSize,
    VkPhysicalDevice physicalDevice,
    PFN_vkGetDeviceProcAddr getDeviceProcAddr)
{
    FFX_RETURN_ON_ERROR(
        outInterface,
        FFX_ERROR_INVALID_POINTER);
    FFX_RETURN_ON_ERROR(
        scratchBuffer,
        FFX_ERROR_INVALID_POINTER);
    FFX_RETURN_ON_ERROR(
        scratchBufferSize >= ffxFsr2GetScratchMemorySizeVK(physicalDevice),
        FFX_ERROR_INSUFFICIENT_MEMORY);

    outInterface->fpGetDeviceCapabilities = GetDeviceCapabilitiesVK;
    outInterface->fpCreateBackendContext = CreateBackendContextVK;
    outInterface->fpDestroyBackendContext = DestroyBackendContextVK;
    outInterface->fpCreateResource = CreateResourceVK;
    outInterface->fpRegisterResource = RegisterResourceVK;
    outInterface->fpUnregisterResources = UnregisterResourcesVK;
    outInterface->fpGetResourceDescription = GetResourceDescriptorVK;
    outInterface->fpDestroyResource = DestroyResourceVK;
    outInterface->fpCreatePipeline = CreatePipelineVK;
    outInterface->fpDestroyPipeline = DestroyPipelineVK;
    outInterface->fpScheduleGpuJob = ScheduleGpuJobVK;
    outInterface->fpExecuteGpuJobs = ExecuteGpuJobsVK;
    outInterface->scratchBuffer = scratchBuffer;
    outInterface->scratchBufferSize = scratchBufferSize;

    BackendContext_VK* context = (BackendContext_VK*)scratchBuffer;

    context->physicalDevice = physicalDevice;
    context->vkFunctionTable.vkGetDeviceProcAddr = getDeviceProcAddr;

    return FFX_OK;
}

void loadVKFunctions(BackendContext_VK* backendContext, PFN_vkGetDeviceProcAddr getDeviceProcAddr)
{
    FFX_ASSERT(NULL != backendContext);

    backendContext->vkFunctionTable.vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)getDeviceProcAddr(backendContext->device, "vkSetDebugUtilsObjectNameEXT");
    backendContext->vkFunctionTable.vkFlushMappedMemoryRanges    = (PFN_vkFlushMappedMemoryRanges)getDeviceProcAddr(backendContext->device, "vkFlushMappedMemoryRanges");
    backendContext->vkFunctionTable.vkCreateDescriptorPool = (PFN_vkCreateDescriptorPool)getDeviceProcAddr(backendContext->device, "vkCreateDescriptorPool");
    backendContext->vkFunctionTable.vkCreateSampler = (PFN_vkCreateSampler)getDeviceProcAddr(backendContext->device, "vkCreateSampler");
    backendContext->vkFunctionTable.vkCreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)getDeviceProcAddr(backendContext->device, "vkCreateDescriptorSetLayout");
    backendContext->vkFunctionTable.vkCreateBuffer = (PFN_vkCreateBuffer)getDeviceProcAddr(backendContext->device, "vkCreateBuffer");
    backendContext->vkFunctionTable.vkCreateImage = (PFN_vkCreateImage)getDeviceProcAddr(backendContext->device, "vkCreateImage");
    backendContext->vkFunctionTable.vkCreateImageView = (PFN_vkCreateImageView)getDeviceProcAddr(backendContext->device, "vkCreateImageView");
    backendContext->vkFunctionTable.vkCreateShaderModule = (PFN_vkCreateShaderModule)getDeviceProcAddr(backendContext->device, "vkCreateShaderModule");
    backendContext->vkFunctionTable.vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)getDeviceProcAddr(backendContext->device, "vkCreatePipelineLayout");
    backendContext->vkFunctionTable.vkCreateComputePipelines = (PFN_vkCreateComputePipelines)getDeviceProcAddr(backendContext->device, "vkCreateComputePipelines");
    backendContext->vkFunctionTable.vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)getDeviceProcAddr(backendContext->device, "vkDestroyPipelineLayout");
    backendContext->vkFunctionTable.vkDestroyPipeline = (PFN_vkDestroyPipeline)getDeviceProcAddr(backendContext->device, "vkDestroyPipeline");
    backendContext->vkFunctionTable.vkDestroyImage = (PFN_vkDestroyImage)getDeviceProcAddr(backendContext->device, "vkDestroyImage");
    backendContext->vkFunctionTable.vkDestroyImageView = (PFN_vkDestroyImageView)getDeviceProcAddr(backendContext->device, "vkDestroyImageView");
    backendContext->vkFunctionTable.vkDestroyBuffer = (PFN_vkDestroyBuffer)getDeviceProcAddr(backendContext->device, "vkDestroyBuffer");
    backendContext->vkFunctionTable.vkDestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)getDeviceProcAddr(backendContext->device, "vkDestroyDescriptorSetLayout");
    backendContext->vkFunctionTable.vkDestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)getDeviceProcAddr(backendContext->device, "vkDestroyDescriptorPool");
    backendContext->vkFunctionTable.vkDestroySampler = (PFN_vkDestroySampler)getDeviceProcAddr(backendContext->device, "vkDestroySampler");
    backendContext->vkFunctionTable.vkDestroyShaderModule = (PFN_vkDestroyShaderModule)getDeviceProcAddr(backendContext->device, "vkDestroyShaderModule");
    backendContext->vkFunctionTable.vkGetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)getDeviceProcAddr(backendContext->device, "vkGetBufferMemoryRequirements");
    backendContext->vkFunctionTable.vkGetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)getDeviceProcAddr(backendContext->device, "vkGetImageMemoryRequirements");
    backendContext->vkFunctionTable.vkAllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)getDeviceProcAddr(backendContext->device, "vkAllocateDescriptorSets");
    backendContext->vkFunctionTable.vkAllocateMemory = (PFN_vkAllocateMemory)getDeviceProcAddr(backendContext->device, "vkAllocateMemory");
    backendContext->vkFunctionTable.vkFreeMemory = (PFN_vkFreeMemory)getDeviceProcAddr(backendContext->device, "vkFreeMemory");
    backendContext->vkFunctionTable.vkMapMemory = (PFN_vkMapMemory)getDeviceProcAddr(backendContext->device, "vkMapMemory");
    backendContext->vkFunctionTable.vkUnmapMemory = (PFN_vkUnmapMemory)getDeviceProcAddr(backendContext->device, "vkUnmapMemory");
    backendContext->vkFunctionTable.vkBindBufferMemory = (PFN_vkBindBufferMemory)getDeviceProcAddr(backendContext->device, "vkBindBufferMemory");
    backendContext->vkFunctionTable.vkBindImageMemory = (PFN_vkBindImageMemory)getDeviceProcAddr(backendContext->device, "vkBindImageMemory");
    backendContext->vkFunctionTable.vkUpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)getDeviceProcAddr(backendContext->device, "vkUpdateDescriptorSets");
    backendContext->vkFunctionTable.vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)getDeviceProcAddr(backendContext->device, "vkCmdPipelineBarrier");
    backendContext->vkFunctionTable.vkCmdBindPipeline = (PFN_vkCmdBindPipeline)getDeviceProcAddr(backendContext->device, "vkCmdBindPipeline");
    backendContext->vkFunctionTable.vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)getDeviceProcAddr(backendContext->device, "vkCmdBindDescriptorSets");
    backendContext->vkFunctionTable.vkCmdDispatch = (PFN_vkCmdDispatch)getDeviceProcAddr(backendContext->device, "vkCmdDispatch");
    backendContext->vkFunctionTable.vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer)getDeviceProcAddr(backendContext->device, "vkCmdCopyBuffer");
    backendContext->vkFunctionTable.vkCmdCopyImage = (PFN_vkCmdCopyImage)getDeviceProcAddr(backendContext->device, "vkCmdCopyImage");
    backendContext->vkFunctionTable.vkCmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)getDeviceProcAddr(backendContext->device, "vkCmdCopyBufferToImage");
    backendContext->vkFunctionTable.vkCmdClearColorImage = (PFN_vkCmdClearColorImage)getDeviceProcAddr(backendContext->device, "vkCmdClearColorImage");
}

void setVKObjectName(BackendContext_VK::VKFunctionTable& vkFunctionTable, VkDevice device, VkObjectType objectType, uint64_t object, char* name)
{
    VkDebugUtilsObjectNameInfoEXT s{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr, objectType, object, name };

    if (vkFunctionTable.vkSetDebugUtilsObjectNameEXT)
        vkFunctionTable.vkSetDebugUtilsObjectNameEXT(device, &s);
}

VkFormat getVKFormatFromSurfaceFormat(FfxSurfaceFormat fmt)
{
    switch (fmt) {

    case(FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS):
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case(FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT):
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case(FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT):
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case(FFX_SURFACE_FORMAT_R16G16B16A16_UNORM):
        return VK_FORMAT_R16G16B16A16_UNORM;
    case(FFX_SURFACE_FORMAT_R32G32_FLOAT):
        return VK_FORMAT_R32G32_SFLOAT;
    case(FFX_SURFACE_FORMAT_R32_UINT):
        return VK_FORMAT_R32_UINT;
    case(FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS):
        return VK_FORMAT_R8G8B8A8_UNORM;
    case(FFX_SURFACE_FORMAT_R8G8B8A8_UNORM):
        return VK_FORMAT_R8G8B8A8_UNORM;
    case(FFX_SURFACE_FORMAT_R11G11B10_FLOAT):
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case(FFX_SURFACE_FORMAT_R16G16_FLOAT):
        return VK_FORMAT_R16G16_SFLOAT;
    case(FFX_SURFACE_FORMAT_R16G16_UINT):
        return VK_FORMAT_R16G16_UINT;
    case(FFX_SURFACE_FORMAT_R16_FLOAT):
        return VK_FORMAT_R16_SFLOAT;
    case(FFX_SURFACE_FORMAT_R16_UINT):
        return VK_FORMAT_R16_UINT;
    case(FFX_SURFACE_FORMAT_R16_UNORM):
        return VK_FORMAT_R16_UNORM;
    case(FFX_SURFACE_FORMAT_R16_SNORM):
        return VK_FORMAT_R16_SNORM;
    case(FFX_SURFACE_FORMAT_R8_UNORM):
        return VK_FORMAT_R8_UNORM;
    case(FFX_SURFACE_FORMAT_R8G8_UNORM):
        return VK_FORMAT_R8G8_UNORM;
    case(FFX_SURFACE_FORMAT_R32_FLOAT):
        return VK_FORMAT_R32_SFLOAT;
    case(FFX_SURFACE_FORMAT_R8_UINT):
        return VK_FORMAT_R8_UINT;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

VkImageUsageFlags getVKImageUsageFlagsFromResourceUsage(FfxResourceUsage flags)
{
    VkImageUsageFlags ret = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (flags & FFX_RESOURCE_USAGE_RENDERTARGET) ret |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (flags & FFX_RESOURCE_USAGE_UAV) ret |= (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    return ret;
}

VkBufferUsageFlags getVKBufferUsageFlagsFromResourceUsage(FfxResourceUsage flags)
{
    if (flags & FFX_RESOURCE_USAGE_UAV)
        return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    else
        return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
}

VkImageType getVKImageTypeFromResourceType(FfxResourceType type)
{
    switch (type) {
    case(FFX_RESOURCE_TYPE_TEXTURE1D):
        return VK_IMAGE_TYPE_1D;
    case(FFX_RESOURCE_TYPE_TEXTURE2D):
        return VK_IMAGE_TYPE_2D;
    case(FFX_RESOURCE_TYPE_TEXTURE3D):
        return VK_IMAGE_TYPE_3D;
    default:
        return VK_IMAGE_TYPE_MAX_ENUM;
    }
}

VkImageLayout getVKImageLayoutFromResourceState(FfxResourceStates state)
{
    switch (state) {

    case(FFX_RESOURCE_STATE_GENERIC_READ):
        return VK_IMAGE_LAYOUT_GENERAL;
    case(FFX_RESOURCE_STATE_UNORDERED_ACCESS):
        return VK_IMAGE_LAYOUT_GENERAL;
    case(FFX_RESOURCE_STATE_COMPUTE_READ):
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case FFX_RESOURCE_STATE_COPY_SRC:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case FFX_RESOURCE_STATE_COPY_DEST:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    default:
        return VK_IMAGE_LAYOUT_GENERAL;
    }
}

VkPipelineStageFlags getVKPipelineStageFlagsFromResourceState(FfxResourceStates state)
{
    switch (state) {

    case(FFX_RESOURCE_STATE_GENERIC_READ):
    case(FFX_RESOURCE_STATE_UNORDERED_ACCESS):
    case(FFX_RESOURCE_STATE_COMPUTE_READ):
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case FFX_RESOURCE_STATE_COPY_SRC:
    case FFX_RESOURCE_STATE_COPY_DEST:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    default:
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
}

VkAccessFlags getVKAccessFlagsFromResourceState(FfxResourceStates state)
{
    switch (state) {

    case(FFX_RESOURCE_STATE_GENERIC_READ):
        return VK_ACCESS_SHADER_READ_BIT;
    case(FFX_RESOURCE_STATE_UNORDERED_ACCESS):
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    case(FFX_RESOURCE_STATE_COMPUTE_READ):
        return VK_ACCESS_SHADER_READ_BIT;
    case FFX_RESOURCE_STATE_COPY_SRC:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case FFX_RESOURCE_STATE_COPY_DEST:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    default:
        return VK_ACCESS_SHADER_READ_BIT;
    }
}

FfxSurfaceFormat ffxGetSurfaceFormatVK(VkFormat fmt)
{
    switch (fmt) {

    case(VK_FORMAT_R32G32B32A32_SFLOAT):
        return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
    case(VK_FORMAT_R16G16B16A16_SFLOAT):
        return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case(VK_FORMAT_R16G16B16A16_UNORM):
        return FFX_SURFACE_FORMAT_R16G16B16A16_UNORM;
    case(VK_FORMAT_R32G32_SFLOAT):
        return FFX_SURFACE_FORMAT_R32G32_FLOAT;
    case(VK_FORMAT_R32_UINT):
        return FFX_SURFACE_FORMAT_R32_UINT;
    case(VK_FORMAT_R8G8B8A8_UNORM):
        return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case(VK_FORMAT_B10G11R11_UFLOAT_PACK32):
        return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
    case(VK_FORMAT_R16G16_SFLOAT):
        return FFX_SURFACE_FORMAT_R16G16_FLOAT;
    case(VK_FORMAT_R16G16_UINT):
        return FFX_SURFACE_FORMAT_R16G16_UINT;
    case(VK_FORMAT_R16_SFLOAT):
        return FFX_SURFACE_FORMAT_R16_FLOAT;
    case(VK_FORMAT_R16_UINT):
        return FFX_SURFACE_FORMAT_R16_UINT;
    case(VK_FORMAT_R16_UNORM):
        return FFX_SURFACE_FORMAT_R16_UNORM;
    case(VK_FORMAT_R16_SNORM):
        return FFX_SURFACE_FORMAT_R16_SNORM;
    case(VK_FORMAT_R8_UNORM):
        return FFX_SURFACE_FORMAT_R8_UNORM;
    case(VK_FORMAT_R32_SFLOAT):
        return FFX_SURFACE_FORMAT_R32_FLOAT;
    case(VK_FORMAT_R8_UINT):
        return FFX_SURFACE_FORMAT_R8_UINT;
    default:
        return FFX_SURFACE_FORMAT_UNKNOWN;
    }
}

uint32_t findMemoryTypeIndex(VkPhysicalDevice physicalDevice, VkMemoryRequirements memRequirements, VkMemoryPropertyFlags requestedProperties, VkMemoryPropertyFlags& outProperties)
{
    FFX_ASSERT(NULL != physicalDevice);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    uint32_t bestCandidate = UINT32_MAX;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & requestedProperties)) {

            // if just device-local memory is requested, make sure this is the invisible heap to prevent over-subscribing the local heap
            if (requestedProperties == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT && (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
                continue;

            bestCandidate = i;
            outProperties = memProperties.memoryTypes[i].propertyFlags;

            // if host-visible memory is requested, check for host coherency as well and if available, return immediately
            if ((requestedProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return bestCandidate;
        }
    }

    return bestCandidate;
}

VkDescriptorBufferInfo accquireDynamicUBO(BackendContext_VK* backendContext, uint32_t size, void* pData)
{
    // the ubo ring buffer is pre-populated with VkBuffer objects of 256-bytes to prevent creating buffers at runtime
    FFX_ASSERT(size <= 256);

    BackendContext_VK::UniformBuffer& ubo = backendContext->uboRingBuffer[backendContext->uboRingBufferIndex];

    VkDescriptorBufferInfo bufferInfo = {};

    bufferInfo.buffer = ubo.bufferResource;
    bufferInfo.offset = 0;
    bufferInfo.range = size;
    
    if (pData)
    {
        memcpy(ubo.pData, pData, size);

        // flush mapped range if memory type is not coherant
        if ((backendContext->uboMemoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
        {
            VkMappedMemoryRange memoryRange;
            memset(&memoryRange, 0, sizeof(memoryRange));

            memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            memoryRange.memory = backendContext->uboMemory;
            memoryRange.offset = 256 * backendContext->uboRingBufferIndex;
            memoryRange.size = size;

            backendContext->vkFunctionTable.vkFlushMappedMemoryRanges(backendContext->device, 1, &memoryRange);
        }
    }

    backendContext->uboRingBufferIndex++;

    if (backendContext->uboRingBufferIndex >= FSR2_UBO_RING_BUFFER_SIZE)
        backendContext->uboRingBufferIndex = 0;

    return bufferInfo;
}

static uint32_t getDefaultSubgroupSize(const BackendContext_VK* backendContext)
{
    VkPhysicalDeviceVulkan11Properties vulkan11Properties = {};
    vulkan11Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;

    VkPhysicalDeviceProperties2 deviceProperties2 = {};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &vulkan11Properties;
    vkGetPhysicalDeviceProperties2(backendContext->physicalDevice, &deviceProperties2);
    FFX_ASSERT(vulkan11Properties.subgroupSize == 32 || vulkan11Properties.subgroupSize == 64); // current desktop market

    return vulkan11Properties.subgroupSize;
}

// Create a FfxFsr2Device from a VkDevice
FfxDevice ffxGetDeviceVK(VkDevice vkDevice)
{
    FFX_ASSERT(NULL != vkDevice);
    return reinterpret_cast<FfxDevice>(vkDevice);
}

FfxCommandList ffxGetCommandListVK(VkCommandBuffer cmdBuf)
{
    FFX_ASSERT(NULL != cmdBuf);
    return reinterpret_cast<FfxCommandList>(cmdBuf);
}

FfxResource ffxGetTextureResourceVK(FfxFsr2Context* context, VkImage imgVk, VkImageView imageView, uint32_t width, uint32_t height, VkFormat imgFormat, const wchar_t* name, FfxResourceStates state)
{
    FfxResource resource = {};
    resource.resource = reinterpret_cast<void*>(imgVk);
    resource.state = state;
    resource.descriptorData = reinterpret_cast<uint64_t>(imageView);
    resource.description.flags = FFX_RESOURCE_FLAGS_NONE;
    resource.description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
    resource.description.width = width;
    resource.description.height = height;
    resource.description.depth = 1;
    resource.description.mipCount = 1;
    resource.description.format = ffxGetSurfaceFormatVK(imgFormat);

    switch (imgFormat)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    {
        resource.isDepth = true;
        break;
    }
    default:
    {
        resource.isDepth = false;
        break;
    }
    }

#ifdef _DEBUG
    if (name) {
        wcscpy_s(resource.name, name);
    }
#endif

    return resource;
}

FfxResource ffxGetBufferResourceVK(FfxFsr2Context* context, VkBuffer bufVk, uint32_t size, const wchar_t* name, FfxResourceStates state)
{
    FfxResource resource = {};
    resource.resource = reinterpret_cast<void*>(bufVk);
    resource.state = state;
    resource.descriptorData = 0;
    resource.description.flags = FFX_RESOURCE_FLAGS_NONE;
    resource.description.type = FFX_RESOURCE_TYPE_BUFFER;
    resource.description.width = size;
    resource.description.height = 1;
    resource.description.depth = 1;
    resource.description.mipCount = 1;
    resource.description.format = FFX_SURFACE_FORMAT_UNKNOWN;
    resource.isDepth = false;

#ifdef _DEBUG
    if (name) {
        wcscpy_s(resource.name, name);
    }
#endif

    return resource;
}

VkImage ffxGetVkImage(FfxFsr2Context* context, uint32_t resId)
{
    FFX_ASSERT(NULL != context);

    FfxFsr2Context_Private* contextPrivate = (FfxFsr2Context_Private*)(context);
    BackendContext_VK* backendContext = (BackendContext_VK*)(contextPrivate->contextDescription.callbacks.scratchBuffer);

    int32_t internalIndex = contextPrivate->uavResources[resId].internalIndex;

    return (internalIndex == -1) ? nullptr : backendContext->resources[internalIndex].imageResource;
}

VkImageView ffxGetVkImageView(FfxFsr2Context* context, uint32_t resId)
{
    FFX_ASSERT(NULL != context);

    FfxFsr2Context_Private* contextPrivate = (FfxFsr2Context_Private*)(context);
    BackendContext_VK* backendContext = (BackendContext_VK*)(contextPrivate->contextDescription.callbacks.scratchBuffer);
    BackendContext_VK::Resource& internalRes = backendContext->resources[contextPrivate->uavResources[resId].internalIndex];

    return internalRes.allMipsImageView;
}

VkImageLayout ffxGetVkImageLayout(FfxFsr2Context* context, uint32_t resId)
{
    FfxFsr2Context_Private* contextPrivate = (FfxFsr2Context_Private*)(context);
    BackendContext_VK* backendContext = (BackendContext_VK*)(contextPrivate->contextDescription.callbacks.scratchBuffer);
    BackendContext_VK::Resource& internalRes = backendContext->resources[contextPrivate->uavResources[resId].internalIndex];

    return getVKImageLayoutFromResourceState(internalRes.state);
}

FfxErrorCode RegisterResourceVK(
    FfxFsr2Interface* backendInterface,
    const FfxResource* inFfxResource,
    FfxResourceInternal* outFfxResourceInternal
)
{
    FFX_ASSERT(NULL != backendInterface);

    BackendContext_VK* backendContext = (BackendContext_VK*)(backendInterface->scratchBuffer);

    if (inFfxResource->resource == nullptr) {

        outFfxResourceInternal->internalIndex = FFX_FSR2_RESOURCE_IDENTIFIER_NULL;
        return FFX_OK;
    }

    FFX_ASSERT(backendContext->nextDynamicResource > backendContext->nextStaticResource);
    outFfxResourceInternal->internalIndex = backendContext->nextDynamicResource--;

    BackendContext_VK::Resource* backendResource = &backendContext->resources[outFfxResourceInternal->internalIndex];

    backendResource->resourceDescription = inFfxResource->description;
    backendResource->state = inFfxResource->state;
    backendResource->undefined = false;

#ifdef _DEBUG
    size_t retval = 0;
    wcstombs_s(&retval, backendResource->resourceName, sizeof(backendResource->resourceName), inFfxResource->name, sizeof(backendResource->resourceName));
    if (retval >= 64) backendResource->resourceName[63] = '\0';
#endif

    if (inFfxResource->description.type == FFX_RESOURCE_TYPE_BUFFER)
    {
        VkBuffer buffer = reinterpret_cast<VkBuffer>(inFfxResource->resource);

        backendResource->bufferResource = buffer;
    }
    else
    {
        VkImage image = reinterpret_cast<VkImage>(inFfxResource->resource);
        VkImageView imageView = reinterpret_cast<VkImageView>(inFfxResource->descriptorData);

        backendResource->imageResource = image;
 
        if (image) {

            if (imageView) {

                if (inFfxResource->isDepth)
                    backendResource->aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
                else
                    backendResource->aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;

                backendResource->allMipsImageView = imageView;
                backendResource->singleMipImageViews[0] = imageView;
            }
        }
    }
    
    return FFX_OK;
}

// dispose dynamic resources: This should be called at the end of the frame
FfxErrorCode UnregisterResourcesVK(FfxFsr2Interface* backendInterface)
{
    FFX_ASSERT(NULL != backendInterface);

    BackendContext_VK* backendContext = (BackendContext_VK*)(backendInterface->scratchBuffer);

    backendContext->nextDynamicResource = FSR2_MAX_RESOURCE_COUNT - 1;

    return FFX_OK;
}

FfxErrorCode GetDeviceCapabilitiesVK(FfxFsr2Interface* backendInterface, FfxDeviceCapabilities* deviceCapabilities, FfxDevice device)
{
    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;

    const uint32_t defaultSubgroupSize = getDefaultSubgroupSize(backendContext);

    // no shader model in vulkan so assume the minimum
    deviceCapabilities->minimumSupportedShaderModel = FFX_SHADER_MODEL_5_1;
    deviceCapabilities->waveLaneCountMin = defaultSubgroupSize;
    deviceCapabilities->waveLaneCountMax = defaultSubgroupSize;
    deviceCapabilities->fp16Supported = false;
    deviceCapabilities->raytracingSupported = false;

    // check if extensions are enabled

    for (uint32_t i = 0; i < backendContext->numDeviceExtensions; i++)
    {
        if (strcmp(backendContext->extensionProperties[i].extensionName, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME) == 0)
        {
            // check if we the max subgroup size allows us to use wave64
            VkPhysicalDeviceSubgroupSizeControlProperties subgroupSizeControlProperties = {};
            subgroupSizeControlProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;

            VkPhysicalDeviceProperties2 deviceProperties2 = {};
            deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            deviceProperties2.pNext = &subgroupSizeControlProperties;
            vkGetPhysicalDeviceProperties2(backendContext->physicalDevice, &deviceProperties2);

            // NOTE: It's important to check requiredSubgroupSizeStages flags (and it's required by the spec).
            // As of August 2022, AMD's Vulkan drivers do not support subgroup size selection through Vulkan API
            // and this information is reported through requiredSubgroupSizeStages flags.
            if (subgroupSizeControlProperties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT)
            {
                deviceCapabilities->waveLaneCountMin = subgroupSizeControlProperties.minSubgroupSize;
                deviceCapabilities->waveLaneCountMax = subgroupSizeControlProperties.maxSubgroupSize;
            }
        }
        if (strcmp(backendContext->extensionProperties[i].extensionName, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME) == 0)
        {
            // check for fp16 support
            VkPhysicalDeviceShaderFloat16Int8Features shaderFloat18Int8Features = {};
            shaderFloat18Int8Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;

            VkPhysicalDeviceFeatures2 physicalDeviceFeatures2 = {};
            physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            physicalDeviceFeatures2.pNext = &shaderFloat18Int8Features;

            vkGetPhysicalDeviceFeatures2(backendContext->physicalDevice, &physicalDeviceFeatures2);

            deviceCapabilities->fp16Supported = (bool)shaderFloat18Int8Features.shaderFloat16;
        }
        if (strcmp(backendContext->extensionProperties[i].extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
        {
            // check for ray tracing support 
            VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};
            accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

            VkPhysicalDeviceFeatures2 physicalDeviceFeatures2 = {};
            physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            physicalDeviceFeatures2.pNext = &accelerationStructureFeatures;

            vkGetPhysicalDeviceFeatures2(backendContext->physicalDevice, &physicalDeviceFeatures2);

            deviceCapabilities->raytracingSupported = (bool)accelerationStructureFeatures.accelerationStructure;
        }
    }

    return FFX_OK;
}

FfxErrorCode CreateBackendContextVK(FfxFsr2Interface* backendInterface, FfxDevice device)
{
    FFX_ASSERT(NULL != backendInterface);

    VkDevice vkDevice = reinterpret_cast<VkDevice>(device);

    // set up some internal resources we need (space for resource views and constant buffers)
    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;
    backendContext->extensionProperties = (VkExtensionProperties*)(backendContext + 1);

    // make sure the extra parameters were already passed in
    FFX_ASSERT(backendContext->physicalDevice != NULL);

    // if vkGetDeviceProcAddr is NULL, use the one from the vulkan header
    if (backendContext->vkFunctionTable.vkGetDeviceProcAddr == NULL)
        backendContext->vkFunctionTable.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    if (vkDevice != NULL) {
        backendContext->device = vkDevice;
    }

    backendContext->nextStaticResource = 0;
    backendContext->nextDynamicResource = FSR2_MAX_RESOURCE_COUNT - 1;

    // load vulkan functions
    loadVKFunctions(backendContext, backendContext->vkFunctionTable.vkGetDeviceProcAddr);

    // enumerate all the device extensions 
    backendContext->numDeviceExtensions = 0;
    vkEnumerateDeviceExtensionProperties(backendContext->physicalDevice, nullptr, &backendContext->numDeviceExtensions, nullptr);
    vkEnumerateDeviceExtensionProperties(backendContext->physicalDevice, nullptr, &backendContext->numDeviceExtensions, backendContext->extensionProperties);

    // create descriptor pool
    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};

    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, FSR2_MAX_IMAGE_VIEWS * FSR2_MAX_BUFFERED_DESCRIPTORS },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FSR2_MAX_IMAGE_VIEWS * FSR2_MAX_BUFFERED_DESCRIPTORS },
        { VK_DESCRIPTOR_TYPE_SAMPLER, FSR2_MAX_SAMPLERS * FSR2_MAX_BUFFERED_DESCRIPTORS  },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FSR2_MAX_UNIFORM_BUFFERS * FSR2_MAX_BUFFERED_DESCRIPTORS },
    };

    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.maxSets = (FSR2_MAX_BUFFERED_DESCRIPTORS * FSR2_MAX_QUEUED_FRAMES);
    descriptorPoolCreateInfo.poolSizeCount = 4;
    descriptorPoolCreateInfo.pPoolSizes = poolSizes;

    if (backendContext->vkFunctionTable.vkCreateDescriptorPool(backendContext->device, &descriptorPoolCreateInfo, nullptr, &backendContext->descPool) != VK_SUCCESS) {
        return FFX_ERROR_BACKEND_API_ERROR;
    }

    VkSamplerCreateInfo samplerCreateInfo = {};

    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCreateInfo.minLod = -1000;
    samplerCreateInfo.maxLod = 1000;
    samplerCreateInfo.maxAnisotropy = 1.0f;

    if (backendContext->vkFunctionTable.vkCreateSampler(backendContext->device, &samplerCreateInfo, nullptr, &backendContext->pointSampler) != VK_SUCCESS) {
        return FFX_ERROR_BACKEND_API_ERROR;
    }

    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;

    if (backendContext->vkFunctionTable.vkCreateSampler(backendContext->device, &samplerCreateInfo, nullptr, &backendContext->linearSampler) != VK_SUCCESS) {
        return FFX_ERROR_BACKEND_API_ERROR;
    }

    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};

        VkDescriptorSetLayoutBinding bindings[] = {
            { 0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &backendContext->pointSampler },
            { 1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &backendContext->linearSampler },
        };

        descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCreateInfo.bindingCount = 2;
        descriptorSetLayoutCreateInfo.pBindings = bindings;

        if (backendContext->vkFunctionTable.vkCreateDescriptorSetLayout(backendContext->device, &descriptorSetLayoutCreateInfo, NULL, &backendContext->samplerDescriptorSetLayout) != VK_SUCCESS) {
            return FFX_ERROR_BACKEND_API_ERROR;
        }
    }

    {
        VkDescriptorSetAllocateInfo allocateInfo = {};

        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = backendContext->descPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &backendContext->samplerDescriptorSetLayout;

        backendContext->vkFunctionTable.vkAllocateDescriptorSets(backendContext->device, &allocateInfo, &backendContext->samplerDescriptorSet);
    }

    // allocate ring buffer of uniform buffers
    {
        for (uint32_t i = 0; i < FSR2_UBO_RING_BUFFER_SIZE; i++)
        {
            BackendContext_VK::UniformBuffer& ubo = backendContext->uboRingBuffer[i];

            VkBufferCreateInfo bufferInfo = {};

            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = 256;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (backendContext->vkFunctionTable.vkCreateBuffer(backendContext->device, &bufferInfo, NULL, &ubo.bufferResource) != VK_SUCCESS) {
                return FFX_ERROR_BACKEND_API_ERROR;
            }
        }

        // allocate memory block for all uniform buffers
        VkMemoryRequirements memRequirements = {};
        backendContext->vkFunctionTable.vkGetBufferMemoryRequirements(backendContext->device, backendContext->uboRingBuffer[0].bufferResource, &memRequirements);

        VkMemoryPropertyFlags requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = FSR2_UBO_MEMORY_BLOCK_SIZE;
        allocInfo.memoryTypeIndex = findMemoryTypeIndex(backendContext->physicalDevice, memRequirements, requiredMemoryProperties, backendContext->uboMemoryProperties);

        if (allocInfo.memoryTypeIndex == UINT32_MAX) {
            requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            allocInfo.memoryTypeIndex = findMemoryTypeIndex(backendContext->physicalDevice, memRequirements, requiredMemoryProperties, backendContext->uboMemoryProperties);

            if (allocInfo.memoryTypeIndex == UINT32_MAX) {
                return FFX_ERROR_BACKEND_API_ERROR;
            }
        }

        VkResult result = backendContext->vkFunctionTable.vkAllocateMemory(backendContext->device, &allocInfo, nullptr, &backendContext->uboMemory);

        if (result != VK_SUCCESS) {
            switch (result) {
            case(VK_ERROR_OUT_OF_HOST_MEMORY):
            case(VK_ERROR_OUT_OF_DEVICE_MEMORY):
                return FFX_ERROR_OUT_OF_MEMORY;
            default:
                return FFX_ERROR_BACKEND_API_ERROR;
            }
        }

        // map the memory block 
        uint8_t* pData = nullptr;

        if (backendContext->vkFunctionTable.vkMapMemory(backendContext->device, backendContext->uboMemory, 0, FSR2_UBO_MEMORY_BLOCK_SIZE, 0, reinterpret_cast<void**>(&pData)) != VK_SUCCESS) {
            return FFX_ERROR_BACKEND_API_ERROR;
        }

        // bind each 256-byte block to the ubos
        for (uint32_t i = 0; i < FSR2_UBO_RING_BUFFER_SIZE; i++)
        {
            BackendContext_VK::UniformBuffer& ubo = backendContext->uboRingBuffer[i];

            // get the buffer memory requirements for each buffer object to silence validation errors
            VkMemoryRequirements memRequirements = {};
            backendContext->vkFunctionTable.vkGetBufferMemoryRequirements(backendContext->device, ubo.bufferResource, &memRequirements);

            ubo.pData = pData + 256 * i;

            if (backendContext->vkFunctionTable.vkBindBufferMemory(backendContext->device, ubo.bufferResource, backendContext->uboMemory, 256 * i) != VK_SUCCESS) {
                return FFX_ERROR_BACKEND_API_ERROR;
            }
        }
    }

    backendContext->gpuJobCount = 0;
    backendContext->scheduledImageBarrierCount = 0;
    backendContext->scheduledBufferBarrierCount = 0;
    backendContext->stagingResourceCount = 0;
    backendContext->allocatedPipelineLayoutCount = 0;
    backendContext->srcStageMask = 0;
    backendContext->dstStageMask = 0;
    backendContext->uboRingBufferIndex = 0;

    return FFX_OK;
}

FfxErrorCode DestroyBackendContextVK(FfxFsr2Interface* backendInterface)
{
    FFX_ASSERT(NULL != backendInterface);

    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;

    for (uint32_t i = 0; i < backendContext->stagingResourceCount; i++)
        DestroyResourceVK(backendInterface, backendContext->stagingResources[i]);

    for (uint32_t i = 0; i < FSR2_UBO_RING_BUFFER_SIZE; i++)
    {
        BackendContext_VK::UniformBuffer& ubo = backendContext->uboRingBuffer[i];

        backendContext->vkFunctionTable.vkDestroyBuffer(backendContext->device, ubo.bufferResource, nullptr);

        ubo.bufferResource = nullptr;
        ubo.pData = nullptr;
    }

    backendContext->vkFunctionTable.vkUnmapMemory(backendContext->device, backendContext->uboMemory);
    backendContext->vkFunctionTable.vkFreeMemory(backendContext->device, backendContext->uboMemory, nullptr);
    backendContext->uboMemory = nullptr;

    backendContext->vkFunctionTable.vkDestroyDescriptorPool(backendContext->device, backendContext->descPool, nullptr);
    backendContext->descPool = nullptr;

    backendContext->vkFunctionTable.vkDestroyDescriptorSetLayout(backendContext->device, backendContext->samplerDescriptorSetLayout, nullptr);
    backendContext->samplerDescriptorSet = nullptr;
    backendContext->samplerDescriptorSetLayout = nullptr;

    backendContext->vkFunctionTable.vkDestroySampler(backendContext->device, backendContext->pointSampler, nullptr);
    backendContext->vkFunctionTable.vkDestroySampler(backendContext->device, backendContext->linearSampler, nullptr);
    backendContext->pointSampler = nullptr;
    backendContext->linearSampler = nullptr;

    if (backendContext->device != nullptr) {

        backendContext->device = nullptr;
    }

    return FFX_OK;
}

// create a internal resource that will stay alive until effect gets shut down
FfxErrorCode CreateResourceVK(
    FfxFsr2Interface* backendInterface, 
    const FfxCreateResourceDescription* createResourceDescription,
    FfxResourceInternal* outResource)
{
    FFX_ASSERT(NULL != backendInterface);
    FFX_ASSERT(NULL != createResourceDescription);
    FFX_ASSERT(NULL != outResource);

    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;
    VkDevice vkDevice = reinterpret_cast<VkDevice>(backendContext->device);

    FFX_ASSERT(backendContext->nextStaticResource + 1 < backendContext->nextDynamicResource);
    outResource->internalIndex = backendContext->nextStaticResource++;
    BackendContext_VK::Resource* res = &backendContext->resources[outResource->internalIndex];
    res->resourceDescription = createResourceDescription->resourceDescription;
    res->resourceDescription.mipCount = createResourceDescription->resourceDescription.mipCount;
    res->undefined = true; // A flag to make sure the first barrier for this image resource always uses an src layout of undefined

    if (res->resourceDescription.mipCount == 0)
        res->resourceDescription.mipCount = (uint32_t)(1 + floor(log2(FFX_MAXIMUM(FFX_MAXIMUM(createResourceDescription->resourceDescription.width, createResourceDescription->resourceDescription.height), createResourceDescription->resourceDescription.depth))));
#ifdef _DEBUG
    size_t retval = 0;
    wcstombs_s(&retval, res->resourceName, sizeof(res->resourceName), createResourceDescription->name, sizeof(res->resourceName));
    if (retval >= 64) res->resourceName[63] = '\0';
#endif
    VkMemoryRequirements memRequirements = {};
    
    switch (createResourceDescription->resourceDescription.type)
    {
    case FFX_RESOURCE_TYPE_BUFFER:
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = createResourceDescription->resourceDescription.width;
        bufferInfo.usage = getVKBufferUsageFlagsFromResourceUsage(createResourceDescription->usage);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (createResourceDescription->initData)
            bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
        if (backendContext->vkFunctionTable.vkCreateBuffer(backendContext->device, &bufferInfo, NULL, &res->bufferResource) != VK_SUCCESS) {
            return FFX_ERROR_BACKEND_API_ERROR;
        }

#ifdef _DEBUG
        setVKObjectName(backendContext->vkFunctionTable, backendContext->device, VK_OBJECT_TYPE_BUFFER, (uint64_t)res->bufferResource, res->resourceName);
#endif

        backendContext->vkFunctionTable.vkGetBufferMemoryRequirements(backendContext->device, res->bufferResource, &memRequirements);
        break;
    }
    case FFX_RESOURCE_TYPE_TEXTURE1D:
    case FFX_RESOURCE_TYPE_TEXTURE2D:
    case FFX_RESOURCE_TYPE_TEXTURE3D:
    {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = getVKImageTypeFromResourceType(createResourceDescription->resourceDescription.type);
        imageInfo.extent.width = createResourceDescription->resourceDescription.width;
        imageInfo.extent.height = createResourceDescription->resourceDescription.type == FFX_RESOURCE_TYPE_TEXTURE1D ? 1 : createResourceDescription->resourceDescription.height;
        imageInfo.extent.depth = createResourceDescription->resourceDescription.type == FFX_RESOURCE_TYPE_TEXTURE3D ? createResourceDescription->resourceDescription.depth : 1;
        imageInfo.mipLevels = res->resourceDescription.mipCount;
        imageInfo.arrayLayers = 1;
        imageInfo.format = getVKFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = getVKImageUsageFlagsFromResourceUsage(createResourceDescription->usage);
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (backendContext->vkFunctionTable.vkCreateImage(backendContext->device, &imageInfo, nullptr, &res->imageResource) != VK_SUCCESS) {
            return FFX_ERROR_BACKEND_API_ERROR;
        }

        res->aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;

#ifdef _DEBUG
        setVKObjectName(backendContext->vkFunctionTable, backendContext->device, VK_OBJECT_TYPE_IMAGE, (uint64_t)res->imageResource, res->resourceName);
#endif

        backendContext->vkFunctionTable.vkGetImageMemoryRequirements(backendContext->device, res->imageResource, &memRequirements);
        break;
    }
    default:;
    }

    VkMemoryPropertyFlags requiredMemoryProperties;
    
    if (createResourceDescription->heapType == FFX_HEAP_TYPE_UPLOAD)
        requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    else 
        requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryTypeIndex(backendContext->physicalDevice, memRequirements, requiredMemoryProperties, res->memoryProperties);

    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        return FFX_ERROR_BACKEND_API_ERROR;
    }

    VkResult result = backendContext->vkFunctionTable.vkAllocateMemory(backendContext->device, &allocInfo, nullptr, &res->deviceMemory);

    if (result != VK_SUCCESS) {
        switch (result) {
        case(VK_ERROR_OUT_OF_HOST_MEMORY):
        case(VK_ERROR_OUT_OF_DEVICE_MEMORY):
            return FFX_ERROR_OUT_OF_MEMORY;
        default:
            return FFX_ERROR_BACKEND_API_ERROR;
        }
    }

    switch (createResourceDescription->resourceDescription.type)
    {
    case FFX_RESOURCE_TYPE_BUFFER:
    {
        if (backendContext->vkFunctionTable.vkBindBufferMemory(backendContext->device, res->bufferResource, res->deviceMemory, 0) != VK_SUCCESS) {
            return FFX_ERROR_BACKEND_API_ERROR;
        }
        break;
    }
    case FFX_RESOURCE_TYPE_TEXTURE1D:
    case FFX_RESOURCE_TYPE_TEXTURE2D:
    case FFX_RESOURCE_TYPE_TEXTURE3D:
    {
        if (backendContext->vkFunctionTable.vkBindImageMemory(backendContext->device, res->imageResource, res->deviceMemory, 0) != VK_SUCCESS) {
            return FFX_ERROR_BACKEND_API_ERROR;
        }

        VkImageViewCreateInfo imageViewCreateInfo = {};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.image = res->imageResource;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = getVKFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format);
        imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = res->resourceDescription.mipCount;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;

        // create an image view containing all mip levels for use as an srv
        if (backendContext->vkFunctionTable.vkCreateImageView(backendContext->device, &imageViewCreateInfo, NULL, &res->allMipsImageView) != VK_SUCCESS) {
            return FFX_ERROR_BACKEND_API_ERROR;
        }
#ifdef _DEBUG
        setVKObjectName(backendContext->vkFunctionTable, backendContext->device, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)res->allMipsImageView, res->resourceName);
#endif
        // create image views of individual mip levels for use as a uav
        for (uint32_t mip = 0; mip < res->resourceDescription.mipCount; ++mip)
        {
            imageViewCreateInfo.subresourceRange.levelCount = 1;
            imageViewCreateInfo.subresourceRange.baseMipLevel = mip;

            if (backendContext->vkFunctionTable.vkCreateImageView(backendContext->device, &imageViewCreateInfo, NULL, &res->singleMipImageViews[mip]) != VK_SUCCESS) {
                return FFX_ERROR_BACKEND_API_ERROR;
            }
#ifdef _DEBUG
            setVKObjectName(backendContext->vkFunctionTable, backendContext->device, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)res->singleMipImageViews[mip], res->resourceName);
#endif
        }
        break;
    }
    default:;
    }

    if (createResourceDescription->initData)
    {
        // only allow copies directy into mapped memory for buffer resources since all texture resources are in optimal tiling
        if (createResourceDescription->heapType == FFX_HEAP_TYPE_UPLOAD && createResourceDescription->resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
        {
            void* data = NULL;

            if (backendContext->vkFunctionTable.vkMapMemory(backendContext->device, res->deviceMemory, 0, createResourceDescription->initDataSize, 0, &data) != VK_SUCCESS) {
                return FFX_ERROR_BACKEND_API_ERROR;
            }

            memcpy(data, createResourceDescription->initData, createResourceDescription->initDataSize);

            // flush mapped range if memory type is not coherant
            if ((res->memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
            {
                VkMappedMemoryRange memoryRange = {};
                memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                memoryRange.memory = res->deviceMemory;
                memoryRange.size = createResourceDescription->initDataSize;

                backendContext->vkFunctionTable.vkFlushMappedMemoryRanges(backendContext->device, 1, &memoryRange);
            }

            backendContext->vkFunctionTable.vkUnmapMemory(backendContext->device, res->deviceMemory);
        }
        else
        {
            FfxResourceInternal copySrc;
            FfxCreateResourceDescription uploadDesc = { *createResourceDescription };
            uploadDesc.heapType = FFX_HEAP_TYPE_UPLOAD;
            uploadDesc.resourceDescription.type = FFX_RESOURCE_TYPE_BUFFER;
            uploadDesc.resourceDescription.width = createResourceDescription->initDataSize;
            uploadDesc.usage = FFX_RESOURCE_USAGE_READ_ONLY;
            uploadDesc.initalState = FFX_RESOURCE_STATE_GENERIC_READ;
            uploadDesc.initData = createResourceDescription->initData;
            uploadDesc.initDataSize = createResourceDescription->initDataSize;

            backendInterface->fpCreateResource(backendInterface, &uploadDesc, &copySrc);

            // setup the upload job
            FfxGpuJobDescription copyJob =
            {
                FFX_GPU_JOB_COPY
            };
            copyJob.copyJobDescriptor.src = copySrc;
            copyJob.copyJobDescriptor.dst = *outResource;

            backendInterface->fpScheduleGpuJob(backendInterface, &copyJob);

            // add to the list of staging resources to delete later 
            uint32_t stagingResIdx = backendContext->stagingResourceCount++;

            FFX_ASSERT(backendContext->stagingResourceCount < FSR2_MAX_STAGING_RESOURCE_COUNT);

            backendContext->stagingResources[stagingResIdx] = copySrc;
        }
    }

    return FFX_OK;
}

FfxResourceDescription GetResourceDescriptorVK(FfxFsr2Interface* backendInterface, FfxResourceInternal resource)
{
    FFX_ASSERT(NULL != backendInterface);

    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;

    if (resource.internalIndex != -1)
    {
        FfxResourceDescription desc = backendContext->resources[resource.internalIndex].resourceDescription;
        return desc;
    }
    else
    {
        FfxResourceDescription desc = {};
        return desc;
    }
}

FfxErrorCode CreatePipelineVK(FfxFsr2Interface* backendInterface, FfxFsr2Pass pass, const FfxPipelineDescription* pipelineDescription, FfxPipelineState* outPipeline)
{
    FFX_ASSERT(NULL != backendInterface);
    FFX_ASSERT(NULL != pipelineDescription);

    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;

    // query device capabilities 
    FfxDeviceCapabilities deviceCapabilities;

    GetDeviceCapabilitiesVK(backendInterface, &deviceCapabilities, ffxGetDeviceVK(backendContext->device));
    const uint32_t defaultSubgroupSize = getDefaultSubgroupSize(backendContext);

    // check if we can force wave64
    bool canForceWave64 = false;
    bool useLut = false;

    if (defaultSubgroupSize == 32 && deviceCapabilities.waveLaneCountMax == 64)
    {
        useLut = true;
        canForceWave64 = true;
    }
    else if (defaultSubgroupSize == 64)
    {
        useLut = true;
    }

    // check if we have 16bit floating point.
    bool supportedFP16 = deviceCapabilities.fp16Supported;

    if (pass == FFX_FSR2_PASS_ACCUMULATE || pass == FFX_FSR2_PASS_ACCUMULATE_SHARPEN)
    {
        VkPhysicalDeviceProperties physicalDeviceProperties = {};
        vkGetPhysicalDeviceProperties(backendContext->physicalDevice, &physicalDeviceProperties);

        // Workaround: Disable FP16 path for the accumulate pass on NVIDIA due to reduced occupancy and high VRAM throughput.
        if (physicalDeviceProperties.vendorID == 0x10DE)
            supportedFP16 = false;
    }

    // work out what permutation to load.
    uint32_t flags = 0;
    flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE) ? FSR2_SHADER_PERMUTATION_HDR_COLOR_INPUT : 0;
    flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) ? 0 : FSR2_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS;
    flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) ? FSR2_SHADER_PERMUTATION_JITTER_MOTION_VECTORS : 0;
    flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_DEPTH_INVERTED) ? FSR2_SHADER_PERMUTATION_DEPTH_INVERTED : 0;
    flags |= (pass == FFX_FSR2_PASS_ACCUMULATE_SHARPEN) ? FSR2_SHADER_PERMUTATION_ENABLE_SHARPENING : 0;
    flags |= (useLut) ? FSR2_SHADER_PERMUTATION_REPROJECT_USE_LANCZOS_TYPE : 0;
    flags |= (canForceWave64) ? FSR2_SHADER_PERMUTATION_FORCE_WAVE64 : 0;
    flags |= (supportedFP16 && (pass != FFX_FSR2_PASS_RCAS)) ? FSR2_SHADER_PERMUTATION_ALLOW_FP16 : 0;

    const Fsr2ShaderBlobVK shaderBlob = fsr2GetPermutationBlobByIndexVK(pass, flags);
    FFX_ASSERT(shaderBlob.data && shaderBlob.size);

    // populate the pass.
    outPipeline->srvCount = shaderBlob.sampledImageCount;
    outPipeline->uavCount = shaderBlob.storageImageCount;
    outPipeline->constCount = shaderBlob.uniformBufferCount;

    FFX_ASSERT(shaderBlob.storageImageCount < FFX_MAX_NUM_UAVS);
    FFX_ASSERT(shaderBlob.sampledImageCount < FFX_MAX_NUM_SRVS);
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

    for (uint32_t srvIndex = 0; srvIndex < outPipeline->srvCount; ++srvIndex)
    {
        outPipeline->srvResourceBindings[srvIndex].slotIndex = shaderBlob.boundSampledImageBindings[srvIndex];
        wcscpy_s(outPipeline->srvResourceBindings[srvIndex].name, converter.from_bytes(shaderBlob.boundSampledImageNames[srvIndex]).c_str());
    }
    for (uint32_t uavIndex = 0; uavIndex < outPipeline->uavCount; ++uavIndex)
    {
        outPipeline->uavResourceBindings[uavIndex].slotIndex = shaderBlob.boundStorageImageBindings[uavIndex];
        wcscpy_s(outPipeline->uavResourceBindings[uavIndex].name, converter.from_bytes(shaderBlob.boundStorageImageNames[uavIndex]).c_str());
    }
    for (uint32_t cbIndex = 0; cbIndex < outPipeline->constCount; ++cbIndex)
    {
        outPipeline->cbResourceBindings[cbIndex].slotIndex = shaderBlob.boundUniformBufferBindings[cbIndex];
        wcscpy_s(outPipeline->cbResourceBindings[cbIndex].name, converter.from_bytes(shaderBlob.boundUniformBufferNames[cbIndex]).c_str());
    }

    // create descriptor set layout
    FFX_ASSERT(backendContext->allocatedPipelineLayoutCount < FFX_FSR2_PASS_COUNT);
    BackendContext_VK::PipelineLayout& pipelineLayout = backendContext->pipelineLayouts[backendContext->allocatedPipelineLayoutCount++];
    VkDescriptorSetLayoutBinding bindings[32];
    uint32_t bindingIndex = 0;

    for (uint32_t srvIndex = 0; srvIndex < outPipeline->srvCount; ++srvIndex)
    {
        VkDescriptorSetLayoutBinding& binding = bindings[bindingIndex++];
        binding.binding = outPipeline->srvResourceBindings[srvIndex].slotIndex;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        binding.pImmutableSamplers = nullptr;
    }

    for (uint32_t uavIndex = 0; uavIndex < outPipeline->uavCount; ++uavIndex)
    {
        VkDescriptorSetLayoutBinding& binding = bindings[bindingIndex++];
        binding.binding = outPipeline->uavResourceBindings[uavIndex].slotIndex;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        binding.pImmutableSamplers = nullptr;
    }

    for (uint32_t cbIndex = 0; cbIndex < outPipeline->constCount; ++cbIndex)
    {
        VkDescriptorSetLayoutBinding& binding = bindings[bindingIndex++];
        binding.binding = outPipeline->cbResourceBindings[cbIndex].slotIndex;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        binding.pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo dsLayoutCreateInfo = {};
    dsLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsLayoutCreateInfo.bindingCount = bindingIndex;
    dsLayoutCreateInfo.pBindings = bindings;

    if (backendContext->vkFunctionTable.vkCreateDescriptorSetLayout(backendContext->device, &dsLayoutCreateInfo, nullptr, &pipelineLayout.descriptorSetLayout) != VK_SUCCESS) {
        return FFX_ERROR_BACKEND_API_ERROR;
    }

    // allocate descriptor sets
    pipelineLayout.descriptorSetIndex = 0;
    
    for (uint32_t i = 0; i < FSR2_MAX_QUEUED_FRAMES; i++)
    {
        VkDescriptorSetAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = backendContext->descPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &pipelineLayout.descriptorSetLayout;

        backendContext->vkFunctionTable.vkAllocateDescriptorSets(backendContext->device, &allocateInfo, &pipelineLayout.descriptorSets[i]);
    }

    // create pipeline layout
    VkDescriptorSetLayout dsLayouts[] = { backendContext->samplerDescriptorSetLayout, pipelineLayout.descriptorSetLayout };

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 2;
    pipelineLayoutCreateInfo.pSetLayouts = dsLayouts;

    if (backendContext->vkFunctionTable.vkCreatePipelineLayout(backendContext->device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout.pipelineLayout) != VK_SUCCESS) {
        return FFX_ERROR_BACKEND_API_ERROR;
    }

    // create the shader module 
    VkShaderModuleCreateInfo shaderModuleCreateInfo = {};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.pCode = (uint32_t*)shaderBlob.data;
    shaderModuleCreateInfo.codeSize = shaderBlob.size;

    VkShaderModule shaderModule = nullptr;

    if (backendContext->vkFunctionTable.vkCreateShaderModule(backendContext->device, &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return FFX_ERROR_BACKEND_API_ERROR;
    }

    // fill out shader stage create info
    VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {};
    shaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageCreateInfo.pName = "main";
    shaderStageCreateInfo.module = shaderModule;

    // set wave64 if possible
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroupSizeCreateInfo = {};

    if (canForceWave64) {

        subgroupSizeCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
        subgroupSizeCreateInfo.requiredSubgroupSize = 64;

        shaderStageCreateInfo.pNext = &subgroupSizeCreateInfo;
    }

    // create the compute pipeline
    VkComputePipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stage = shaderStageCreateInfo;
    pipelineCreateInfo.layout = pipelineLayout.pipelineLayout;

    VkPipeline computePipeline = nullptr;
    if (backendContext->vkFunctionTable.vkCreateComputePipelines(backendContext->device, nullptr, 1, &pipelineCreateInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        return FFX_ERROR_BACKEND_API_ERROR;
    }

    backendContext->vkFunctionTable.vkDestroyShaderModule(backendContext->device, shaderModule, nullptr);

    outPipeline->pipeline = reinterpret_cast<FfxPipeline>(computePipeline);
    outPipeline->rootSignature = reinterpret_cast<FfxRootSignature>(&pipelineLayout);

    return FFX_OK;
}

FfxErrorCode ScheduleGpuJobVK(FfxFsr2Interface* backendInterface, const FfxGpuJobDescription* job)
{
    FFX_ASSERT(NULL != backendInterface);
    FFX_ASSERT(NULL != job);

    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;

    FFX_ASSERT(backendContext->gpuJobCount < FSR2_MAX_GPU_JOBS);

    backendContext->gpuJobs[backendContext->gpuJobCount] = *job;

    if (job->jobType == FFX_GPU_JOB_COMPUTE) {

        // needs to copy SRVs and UAVs in case they are on the stack only
        FfxComputeJobDescription* computeJob = &backendContext->gpuJobs[backendContext->gpuJobCount].computeJobDescriptor;
        const uint32_t numConstBuffers = job->computeJobDescriptor.pipeline.constCount;
        for (uint32_t currentRootConstantIndex = 0; currentRootConstantIndex < numConstBuffers; ++currentRootConstantIndex)
        {
            computeJob->cbs[currentRootConstantIndex].uint32Size = job->computeJobDescriptor.cbs[currentRootConstantIndex].uint32Size;
            memcpy(computeJob->cbs[currentRootConstantIndex].data, job->computeJobDescriptor.cbs[currentRootConstantIndex].data, computeJob->cbs[currentRootConstantIndex].uint32Size * sizeof(uint32_t));
        }
    }

    backendContext->gpuJobCount++;

    return FFX_OK;
}

void addBarrier(BackendContext_VK* backendContext, FfxResourceInternal* resource, FfxResourceStates newState)
{
    FFX_ASSERT(NULL != backendContext);
    FFX_ASSERT(NULL != resource);

    BackendContext_VK::Resource& ffxResource = backendContext->resources[resource->internalIndex];

    if (ffxResource.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
    {
        VkBuffer vkResource = ffxResource.bufferResource;
        VkBufferMemoryBarrier* barrier = &backendContext->bufferMemoryBarriers[backendContext->scheduledBufferBarrierCount];

        FfxResourceStates& curState = backendContext->resources[resource->internalIndex].state;

        barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier->pNext = nullptr;
        barrier->srcAccessMask = getVKAccessFlagsFromResourceState(curState);
        barrier->dstAccessMask = getVKAccessFlagsFromResourceState(newState);
        barrier->srcQueueFamilyIndex = 0;
        barrier->dstQueueFamilyIndex = 0;
        barrier->buffer = vkResource;
        barrier->offset = 0;
        barrier->size = VK_WHOLE_SIZE;

        backendContext->srcStageMask |= getVKPipelineStageFlagsFromResourceState(curState);
        backendContext->dstStageMask |= getVKPipelineStageFlagsFromResourceState(newState);

        curState = newState;

        ++backendContext->scheduledBufferBarrierCount;
    }
    else
    {
        VkImage vkResource = ffxResource.imageResource;
        VkImageMemoryBarrier* barrier = &backendContext->imageMemoryBarriers[backendContext->scheduledImageBarrierCount];

        FfxResourceStates& curState = backendContext->resources[resource->internalIndex].state;

        VkImageSubresourceRange range;
        range.aspectMask = backendContext->resources[resource->internalIndex].aspectFlags;
        range.baseMipLevel = 0;
        range.levelCount = backendContext->resources[resource->internalIndex].resourceDescription.mipCount;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier->pNext = nullptr;
        barrier->srcAccessMask = getVKAccessFlagsFromResourceState(curState);
        barrier->dstAccessMask = getVKAccessFlagsFromResourceState(newState);
        barrier->oldLayout = ffxResource.undefined ? VK_IMAGE_LAYOUT_UNDEFINED : getVKImageLayoutFromResourceState(curState);
        barrier->newLayout = getVKImageLayoutFromResourceState(newState);
        barrier->srcQueueFamilyIndex = 0;
        barrier->dstQueueFamilyIndex = 0;
        barrier->image = vkResource;
        barrier->subresourceRange = range;

        backendContext->srcStageMask |= getVKPipelineStageFlagsFromResourceState(curState);
        backendContext->dstStageMask |= getVKPipelineStageFlagsFromResourceState(newState);

        curState = newState;

        ++backendContext->scheduledImageBarrierCount;
    }

    if (ffxResource.undefined)
        ffxResource.undefined = false;
}

void flushBarriers(BackendContext_VK* backendContext, VkCommandBuffer vkCommandBuffer)
{
    FFX_ASSERT(NULL != backendContext);
    FFX_ASSERT(NULL != vkCommandBuffer);

    if (backendContext->scheduledImageBarrierCount > 0 || backendContext->scheduledBufferBarrierCount > 0)
    {
        backendContext->vkFunctionTable.vkCmdPipelineBarrier(vkCommandBuffer, backendContext->srcStageMask, backendContext->dstStageMask, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, backendContext->scheduledBufferBarrierCount, backendContext->bufferMemoryBarriers, backendContext->scheduledImageBarrierCount, backendContext->imageMemoryBarriers);
        backendContext->scheduledImageBarrierCount = 0;
        backendContext->scheduledBufferBarrierCount = 0;
        backendContext->srcStageMask = 0;
        backendContext->dstStageMask = 0;
    }
}

static FfxErrorCode executeGpuJobCompute(BackendContext_VK* backendContext, FfxGpuJobDescription* job, VkCommandBuffer vkCommandBuffer)
{
    uint32_t               imageInfoIndex = 0;
    uint32_t               bufferInfoIndex = 0;
    uint32_t               descriptorWriteIndex = 0;
    VkDescriptorImageInfo  imageInfos[FSR2_MAX_IMAGE_VIEWS];
    VkDescriptorBufferInfo bufferInfos[FSR2_MAX_UNIFORM_BUFFERS];
    VkWriteDescriptorSet   writeDatas[FSR2_MAX_IMAGE_VIEWS + FSR2_MAX_UNIFORM_BUFFERS];

    BackendContext_VK::PipelineLayout* pipelineLayout = reinterpret_cast<BackendContext_VK::PipelineLayout*>(job->computeJobDescriptor.pipeline.rootSignature);

    // bind uavs
    for (uint32_t uav = 0; uav < job->computeJobDescriptor.pipeline.uavCount; ++uav)
    {
        addBarrier(backendContext, &job->computeJobDescriptor.uavs[uav], FFX_RESOURCE_STATE_UNORDERED_ACCESS);

        BackendContext_VK::Resource ffxResource = backendContext->resources[job->computeJobDescriptor.uavs[uav].internalIndex];

        writeDatas[descriptorWriteIndex] = {};
        writeDatas[descriptorWriteIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDatas[descriptorWriteIndex].dstSet = pipelineLayout->descriptorSets[pipelineLayout->descriptorSetIndex];
        writeDatas[descriptorWriteIndex].descriptorCount = 1;
        writeDatas[descriptorWriteIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writeDatas[descriptorWriteIndex].pImageInfo = &imageInfos[imageInfoIndex];
        writeDatas[descriptorWriteIndex].dstBinding = job->computeJobDescriptor.pipeline.uavResourceBindings[uav].slotIndex;
        writeDatas[descriptorWriteIndex].dstArrayElement = 0;

        imageInfos[imageInfoIndex] = {};
        imageInfos[imageInfoIndex].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[imageInfoIndex].imageView = ffxResource.singleMipImageViews[job->computeJobDescriptor.uavMip[uav]];

        imageInfoIndex++;
        descriptorWriteIndex++;
    }

    // bind srvs
    for (uint32_t srv = 0; srv < job->computeJobDescriptor.pipeline.srvCount; ++srv)
    {
        addBarrier(backendContext, &job->computeJobDescriptor.srvs[srv], FFX_RESOURCE_STATE_COMPUTE_READ);

        BackendContext_VK::Resource ffxResource = backendContext->resources[job->computeJobDescriptor.srvs[srv].internalIndex];

        writeDatas[descriptorWriteIndex] = {};
        writeDatas[descriptorWriteIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDatas[descriptorWriteIndex].dstSet = pipelineLayout->descriptorSets[pipelineLayout->descriptorSetIndex];
        writeDatas[descriptorWriteIndex].descriptorCount = 1;
        writeDatas[descriptorWriteIndex].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writeDatas[descriptorWriteIndex].pImageInfo = &imageInfos[imageInfoIndex];
        writeDatas[descriptorWriteIndex].dstBinding = job->computeJobDescriptor.pipeline.srvResourceBindings[srv].slotIndex;
        writeDatas[descriptorWriteIndex].dstArrayElement = 0;

        imageInfos[imageInfoIndex] = {};
        imageInfos[imageInfoIndex].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[imageInfoIndex].imageView = ffxResource.allMipsImageView;

        imageInfoIndex++;
        descriptorWriteIndex++;
    }

    // update ubos
    for (uint32_t i = 0; i < job->computeJobDescriptor.pipeline.constCount; ++i)
    {
        writeDatas[descriptorWriteIndex] = {};
        writeDatas[descriptorWriteIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDatas[descriptorWriteIndex].dstSet = pipelineLayout->descriptorSets[pipelineLayout->descriptorSetIndex];
        writeDatas[descriptorWriteIndex].descriptorCount = 1;
        writeDatas[descriptorWriteIndex].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDatas[descriptorWriteIndex].pBufferInfo = &bufferInfos[bufferInfoIndex];
        writeDatas[descriptorWriteIndex].dstBinding = job->computeJobDescriptor.pipeline.cbResourceBindings[i].slotIndex;
        writeDatas[descriptorWriteIndex].dstArrayElement = 0;

        bufferInfos[bufferInfoIndex] = accquireDynamicUBO(backendContext, job->computeJobDescriptor.cbs[i].uint32Size * sizeof(uint32_t), job->computeJobDescriptor.cbs[i].data);

        bufferInfoIndex++;
        descriptorWriteIndex++;
    }

    // insert all the barriers 
    flushBarriers(backendContext, vkCommandBuffer);

    // update all uavs and srvs
    backendContext->vkFunctionTable.vkUpdateDescriptorSets(backendContext->device, descriptorWriteIndex, writeDatas, 0, nullptr);

    // bind pipeline
    backendContext->vkFunctionTable.vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, reinterpret_cast<VkPipeline>(job->computeJobDescriptor.pipeline.pipeline));

    // bind descriptor sets 
    VkDescriptorSet sets[] = {
        backendContext->samplerDescriptorSet,
        pipelineLayout->descriptorSets[pipelineLayout->descriptorSetIndex],
    };

    backendContext->vkFunctionTable.vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout->pipelineLayout, 0, 2, sets, 0, nullptr);

    // dispatch
    backendContext->vkFunctionTable.vkCmdDispatch(vkCommandBuffer, job->computeJobDescriptor.dimensions[0], job->computeJobDescriptor.dimensions[1], job->computeJobDescriptor.dimensions[2]);

    // move to another descriptor set for the next compute render job so that we don't overwrite descriptors in-use
    pipelineLayout->descriptorSetIndex++;

    if (pipelineLayout->descriptorSetIndex >= FSR2_MAX_QUEUED_FRAMES)
        pipelineLayout->descriptorSetIndex = 0;

    return FFX_OK;
}

static FfxErrorCode executeGpuJobCopy(BackendContext_VK* backendContext, FfxGpuJobDescription* job, VkCommandBuffer vkCommandBuffer)
{
    BackendContext_VK::Resource ffxResourceSrc = backendContext->resources[job->copyJobDescriptor.src.internalIndex];
    BackendContext_VK::Resource ffxResourceDst = backendContext->resources[job->copyJobDescriptor.dst.internalIndex];

    addBarrier(backendContext, &job->copyJobDescriptor.src, FFX_RESOURCE_STATE_COPY_SRC);
    addBarrier(backendContext, &job->copyJobDescriptor.dst, FFX_RESOURCE_STATE_COPY_DEST);
    flushBarriers(backendContext, vkCommandBuffer);

    if (ffxResourceSrc.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER && ffxResourceDst.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
    {
        VkBuffer vkResourceSrc = ffxResourceSrc.bufferResource;
        VkBuffer vkResourceDst = ffxResourceDst.bufferResource;

        VkBufferCopy bufferCopy = {};

        bufferCopy.dstOffset = 0;
        bufferCopy.srcOffset = 0;
        bufferCopy.size = ffxResourceSrc.resourceDescription.width;

        backendContext->vkFunctionTable.vkCmdCopyBuffer(vkCommandBuffer, vkResourceSrc, vkResourceDst, 1, &bufferCopy);
    }
    else if (ffxResourceSrc.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER && ffxResourceDst.resourceDescription.type != FFX_RESOURCE_TYPE_BUFFER)
    {
        VkBuffer vkResourceSrc = ffxResourceSrc.bufferResource;
        VkImage vkResourceDst = ffxResourceDst.imageResource;

        VkImageSubresourceLayers subresourceLayers = {};

        subresourceLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceLayers.baseArrayLayer = 0;
        subresourceLayers.layerCount = 1;
        subresourceLayers.mipLevel = 0;

        VkOffset3D offset = {};

        offset.x = 0;
        offset.y = 0;
        offset.z = 0;

        VkExtent3D extent = {};

        extent.width = ffxResourceDst.resourceDescription.width;
        extent.height = ffxResourceDst.resourceDescription.height;
        extent.depth = ffxResourceDst.resourceDescription.depth;

        VkBufferImageCopy bufferImageCopy = {};

        bufferImageCopy.bufferOffset = 0;
        bufferImageCopy.bufferRowLength = 0;
        bufferImageCopy.bufferImageHeight = 0;
        bufferImageCopy.imageSubresource = subresourceLayers;
        bufferImageCopy.imageOffset = offset;
        bufferImageCopy.imageExtent = extent;

        backendContext->vkFunctionTable.vkCmdCopyBufferToImage(vkCommandBuffer, vkResourceSrc, vkResourceDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferImageCopy);
    }
    else
    {
        VkImageCopy             imageCopies[FSR2_MAX_IMAGE_COPY_MIPS];
        VkImage vkResourceSrc = ffxResourceSrc.imageResource;
        VkImage vkResourceDst = ffxResourceDst.imageResource;

        for (uint32_t mip = 0; mip < ffxResourceSrc.resourceDescription.mipCount; mip++)
        {
            VkImageSubresourceLayers subresourceLayers = {};

            subresourceLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceLayers.baseArrayLayer = 0;
            subresourceLayers.layerCount = 1;
            subresourceLayers.mipLevel = mip;

            VkOffset3D offset = {};

            offset.x = 0;
            offset.y = 0;
            offset.z = 0;

            VkExtent3D extent = {};

            extent.width = ffxResourceSrc.resourceDescription.width / (mip + 1);
            extent.height = ffxResourceSrc.resourceDescription.height / (mip + 1);
            extent.depth = ffxResourceSrc.resourceDescription.depth / (mip + 1);

            VkImageCopy& copyRegion = imageCopies[mip];

            copyRegion.srcSubresource = subresourceLayers;
            copyRegion.srcOffset = offset;
            copyRegion.dstSubresource = subresourceLayers;
            copyRegion.dstOffset = offset;
            copyRegion.extent = extent;
        }

        backendContext->vkFunctionTable.vkCmdCopyImage(vkCommandBuffer, vkResourceSrc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkResourceDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ffxResourceSrc.resourceDescription.mipCount, imageCopies);
    }

    return FFX_OK;
}

static FfxErrorCode executeGpuJobClearFloat(BackendContext_VK* backendContext, FfxGpuJobDescription* job, VkCommandBuffer vkCommandBuffer)
{
    uint32_t idx = job->clearJobDescriptor.target.internalIndex;
    BackendContext_VK::Resource ffxResource = backendContext->resources[idx];

    if (ffxResource.resourceDescription.type != FFX_RESOURCE_TYPE_BUFFER)
    {
        addBarrier(backendContext, &job->clearJobDescriptor.target, FFX_RESOURCE_STATE_COPY_DEST);
        flushBarriers(backendContext, vkCommandBuffer);

        VkImage vkResource = ffxResource.imageResource;

        VkClearColorValue clearColorValue = {};

        clearColorValue.float32[0] = job->clearJobDescriptor.color[0];
        clearColorValue.float32[1] = job->clearJobDescriptor.color[1];
        clearColorValue.float32[2] = job->clearJobDescriptor.color[2];
        clearColorValue.float32[3] = job->clearJobDescriptor.color[3];

        VkImageSubresourceRange range;
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = ffxResource.resourceDescription.mipCount;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        backendContext->vkFunctionTable.vkCmdClearColorImage(vkCommandBuffer, vkResource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColorValue, 1, &range);
    }

    return FFX_OK;
}

FfxErrorCode ExecuteGpuJobsVK(FfxFsr2Interface* backendInterface, FfxCommandList commandList)
{
    FFX_ASSERT(NULL != backendInterface);

    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;

    FfxErrorCode errorCode = FFX_OK;

    // execute all renderjobs
    for (uint32_t i = 0; i < backendContext->gpuJobCount; ++i)
    {
        FfxGpuJobDescription* gpuJob = &backendContext->gpuJobs[i];
        VkCommandBuffer vkCommandBuffer = reinterpret_cast<VkCommandBuffer>(commandList);

        switch (gpuJob->jobType)
        {
        case FFX_GPU_JOB_CLEAR_FLOAT:
        {
            errorCode = executeGpuJobClearFloat(backendContext, gpuJob, vkCommandBuffer);
            break;
        }
        case FFX_GPU_JOB_COPY:
        {
            errorCode = executeGpuJobCopy(backendContext, gpuJob, vkCommandBuffer);
            break;
        }
        case FFX_GPU_JOB_COMPUTE:
        {
            errorCode = executeGpuJobCompute(backendContext, gpuJob, vkCommandBuffer);
            break;
        }
        default:;
        }
    }

    // check the execute function returned cleanly.
    FFX_RETURN_ON_ERROR(
        errorCode == FFX_OK,
        FFX_ERROR_BACKEND_API_ERROR);

    backendContext->gpuJobCount = 0;

    return FFX_OK;
}

FfxErrorCode DestroyResourceVK(FfxFsr2Interface* backendInterface, FfxResourceInternal resource)
{
    FFX_ASSERT(backendInterface != nullptr);

    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;

    if (resource.internalIndex != -1)
    {
        BackendContext_VK::Resource& res = backendContext->resources[resource.internalIndex];

        if (res.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
        {
            if (res.bufferResource)
            {
                backendContext->vkFunctionTable.vkDestroyBuffer(backendContext->device, res.bufferResource, NULL);
                res.bufferResource = nullptr;
            }
        }
        else
        {
            if (res.allMipsImageView)
            {
                backendContext->vkFunctionTable.vkDestroyImageView(backendContext->device, res.allMipsImageView, NULL);
                res.allMipsImageView = nullptr;
            }

            for (uint32_t i = 0; i < res.resourceDescription.mipCount; i++)
            {
                if (res.singleMipImageViews[i])
                {
                    backendContext->vkFunctionTable.vkDestroyImageView(backendContext->device, res.singleMipImageViews[i], NULL);
                    res.singleMipImageViews[i] = nullptr;
                }
            }

            if (res.imageResource)
            {
                backendContext->vkFunctionTable.vkDestroyImage(backendContext->device, res.imageResource, NULL);
                res.imageResource = nullptr;
            }
        }

        if (res.deviceMemory)
        {
            backendContext->vkFunctionTable.vkFreeMemory(backendContext->device, res.deviceMemory, NULL);
            res.deviceMemory = nullptr;
        }
    }

    return FFX_OK;
}

FfxErrorCode DestroyPipelineVK(FfxFsr2Interface* backendInterface, FfxPipelineState* pipeline)
{
    FFX_ASSERT(backendInterface != nullptr);
    if (!pipeline)
        return FFX_OK;

    BackendContext_VK* backendContext = (BackendContext_VK*)backendInterface->scratchBuffer;

    // destroy pipeline 
    VkPipeline computePipeline = reinterpret_cast<VkPipeline>(pipeline->pipeline);
    if (computePipeline) {
        backendContext->vkFunctionTable.vkDestroyPipeline(backendContext->device, computePipeline, nullptr);
        pipeline->pipeline = nullptr;
    }

    BackendContext_VK::PipelineLayout* pipelineLayout = reinterpret_cast<BackendContext_VK::PipelineLayout*>(pipeline->rootSignature);
    if (pipelineLayout) {
        // destroy descriptor sets 
        for (uint32_t i = 0; i < FSR2_MAX_QUEUED_FRAMES; i++)
            pipelineLayout->descriptorSets[i] = nullptr;

        // destroy descriptor set layout
        if (pipelineLayout->descriptorSetLayout)
        {
            backendContext->vkFunctionTable.vkDestroyDescriptorSetLayout(backendContext->device, pipelineLayout->descriptorSetLayout, nullptr);
            pipelineLayout->descriptorSetLayout = nullptr;
        }

        // destroy pipeline layout
        if (pipelineLayout->pipelineLayout)
        {
            backendContext->vkFunctionTable.vkDestroyPipelineLayout(backendContext->device, pipelineLayout->pipelineLayout, nullptr);
            pipelineLayout->pipelineLayout = nullptr;
        }
    }

    return FFX_OK;
}
