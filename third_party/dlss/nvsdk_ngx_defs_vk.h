/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NVSDK_NGX_DEFS_VK_H
#define NVSDK_NGX_DEFS_VK_H
#pragma once

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum NVSDK_NGX_Resource_VK_Type
{
    NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW,
    NVSDK_NGX_RESOURCE_VK_TYPE_VK_BUFFER
} NVSDK_NGX_Resource_VK_Type;

////////////////////////////////////////////////////////////////////////////////
// NVSDK_NGX_ImageViewInfo_VK [Vulkan only]
// Contains ImageView-specific metadata.
// ImageView:
//    The VkImageView resource.
//
// Image:
//    The VkImage associated to this VkImageView.
//
// SubresourceRange:
//    The VkImageSubresourceRange associated to this VkImageView.
//
//  Format:
//    The format of the resource.
//
//  Width:
//     The width of the resource.
//
//  Height:
//     The height of the resource.
//
typedef struct NVSDK_NGX_ImageViewInfo_VK {
    VkImageView ImageView;
    VkImage Image;
    VkImageSubresourceRange SubresourceRange;
    VkFormat Format;
    unsigned int Width;
    unsigned int Height;
} NVSDK_NGX_ImageViewInfo_VK;

////////////////////////////////////////////////////////////////////////////////
// NVSDK_NGX_BufferInfo_VK [Vulkan only]
// Contains Buffer-specific metadata.
// Buffer
//    The VkBuffer resource.
//
// SizeInBytes:
//    The size of the resource (in bytes).
//
typedef struct NVSDK_NGX_BufferInfo_VK {
    VkBuffer Buffer;
    unsigned int SizeInBytes;
} NVSDK_NGX_BufferInfo_VK;

////////////////////////////////////////////////////////////////////////////////
// NVSDK_NGX_Resource_VK [Vulkan only]
//
// ImageViewInfo:
//    The VkImageView resource, and VkImageView-specific metadata. A NVSDK_NGX_Resource_VK can only have one of ImageViewInfo or BufferInfo.
//
// BufferInfo:
//    The VkBuffer Resource, and VkBuffer-specific metadata. A NVSDK_NGX_Resource_VK can only have one of ImageViewInfo or BufferInfo.
//
//  Type:
//    Whether or this resource is a VkImageView or a VkBuffer.
//
//  ReadWrite:
//     True if the resource is available for read and write access.
//          For VkBuffer resources: VkBufferUsageFlags includes VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT or VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
//          For VkImage resources: VkImageUsageFlags for associated VkImage includes VK_IMAGE_USAGE_STORAGE_BIT
//
typedef struct NVSDK_NGX_Resource_VK {
    union {
        NVSDK_NGX_ImageViewInfo_VK ImageViewInfo;
        NVSDK_NGX_BufferInfo_VK BufferInfo;
    } Resource;
    NVSDK_NGX_Resource_VK_Type Type;
    bool ReadWrite;
} NVSDK_NGX_Resource_VK;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NVSDK_NGX_DEFS_VK_H
