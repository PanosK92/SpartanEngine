/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

/*
CREDITS:

Developed by:
    Dmitrii Zhdan (dzhdan@nvidia.com)
    Tim Cheblokov (ttcheblokov@nvidia.com)

Contributions:
    Oles Shyshkovtsov, 4A GAMES
        Recurrent blurring concept
    Ivan Fedorov, NVIDIA
        Initial API design
    Ganesh Mamadapu, "Ganaboy2K", "DSLE" author
        Feedback and generic improvements based on NRD usage in "Dark Souls 2: PT"
    Justin McTavish Avola, "Aeternitae"
        Invaluable testing on AMD/Linux

Special thanks:
    Pawel Kozlowski, NVIDIA
    Evgeny Makarov, NVIDIA
    Ivan Povarov, NVIDIA
*/

#pragma once

#include <cstdint>
#include <cstddef>

#define NRD_VERSION_MAJOR 4
#define NRD_VERSION_MINOR 17
#define NRD_VERSION_BUILD 4
#define NRD_VERSION_DATE "2 May 2026"

#if defined(_WIN32)
    #define NRD_CALL __stdcall
#else
    #define NRD_CALL
#endif

#ifndef NRD_API
    #define NRD_API extern "C"
#endif

#include "NRDDescs.h"
#include "NRDSettings.h"

namespace nrd
{
    // Create and destroy
    NRD_API Result NRD_CALL CreateInstance(const InstanceCreationDesc& instanceCreationDesc, Instance*& instance);
    NRD_API void NRD_CALL DestroyInstance(Instance& instance);

    // Get
    NRD_API const LibraryDesc* NRD_CALL GetLibraryDesc();
    NRD_API const InstanceDesc* NRD_CALL GetInstanceDesc(const Instance& instance);

    // Typically needs to be called once per frame
    NRD_API Result NRD_CALL SetCommonSettings(Instance& instance, const CommonSettings& commonSettings);

    // Typically needs to be called at least once per denoiser (not necessarily on each frame)
    NRD_API Result NRD_CALL SetDenoiserSettings(Instance& instance, Identifier identifier, const void* denoiserSettings);

    // Retrieves dispatches for the list of identifiers (if they are parts of the instance)
    // IMPORTANT: returned memory is owned by the "instance" and will be overwritten by the next "GetComputeDispatches" call
    NRD_API Result NRD_CALL GetComputeDispatches(Instance& instance, const Identifier* identifiers, uint32_t identifiersNum, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescsNum);

    // Helpers
    NRD_API const char* GetResourceTypeString(ResourceType resourceType);
    NRD_API const char* GetDenoiserString(Denoiser denoiser);
}
