// This file is part of the FidelityFX SDK.
//
// Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.
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

#pragma once

#include "ffx_assert.h"
#include "ffx_types.h"
#include "ffx_error.h"

// Include the FSR2 resources defined in the HLSL code. This shared here to avoid getting out of sync.
#define FFX_CPU
#include "shaders/ffx_fsr2_resources.h"
#include "shaders/ffx_fsr2_common.h"

#if defined(__cplusplus)
extern "C" {
#endif // #if defined(__cplusplus)

FFX_FORWARD_DECLARE(FfxFsr2Interface);

/// An enumeration of all the passes which constitute the FSR2 algorithm.
///
/// FSR2 is implemented as a composite of several compute passes each
/// computing a key part of the final result. Each call to the 
/// <c><i>FfxFsr2ScheduleRenderJobFunc</i></c> callback function will
/// correspond to a single pass included in <c><i>FfxFsr2Pass</i></c>. For a
/// more comprehensive description of each pass, please refer to the FSR2
/// reference documentation.
///
/// Please note in some cases e.g.: <c><i>FFX_FSR2_PASS_ACCUMULATE</i></c>
/// and <c><i>FFX_FSR2_PASS_ACCUMULATE_SHARPEN</i></c> either one pass or the
/// other will be used (they are mutually exclusive). The choice of which will
/// depend on the way the <c><i>FfxFsr2Context</i></c> is created and the
/// precise contents of <c><i>FfxFsr2DispatchParamters</i></c> each time a call
/// is made to <c><i>ffxFsr2ContextDispatch</i></c>.
/// 
/// @ingroup FSR2
typedef enum FfxFsr2Pass {

    FFX_FSR2_PASS_PREPARE_INPUT_COLOR = 0,                              ///< A pass which prepares input colors for subsequent use.
    FFX_FSR2_PASS_DEPTH_CLIP = 1,                                       ///< A pass which performs depth clipping.
    FFX_FSR2_PASS_RECONSTRUCT_PREVIOUS_DEPTH = 2,                       ///< A pass which performs reconstruction of previous frame's depth.
    FFX_FSR2_PASS_LOCK = 3,                                             ///< A pass which calculates pixel locks.
    FFX_FSR2_PASS_ACCUMULATE = 4,                                       ///< A pass which performs upscaling.
    FFX_FSR2_PASS_ACCUMULATE_SHARPEN = 5,                               ///< A pass which performs upscaling when sharpening is used.
    FFX_FSR2_PASS_RCAS = 6,                                             ///< A pass which performs sharpening.
    FFX_FSR2_PASS_COMPUTE_LUMINANCE_PYRAMID = 7,                        ///< A pass which generates the luminance mipmap chain for the current frame.
    FFX_FSR2_PASS_GENERATE_REACTIVE = 8,                                ///< An optional pass to generate a reactive mask

    FFX_FSR2_PASS_COUNT                                                 ///< The number of passes performed by FSR2.
} FfxFsr2Pass;

/// A structure containing the description used to create a
/// <c><i>FfxPipeline</i></c> structure.
///
/// A pipeline is the name given to a shader and the collection of state that
/// is required to dispatch it. In the context of FSR2 and its architecture
/// this means that a <c><i>FfxPipelineDescription</i></c> will map to either a
/// monolithic object in an explicit API (such as a
/// <c><i>PipelineStateObject</i></c> in DirectX 12). Or a shader and some
/// ancillary API objects (in something like DirectX 11).
///
/// The <c><i>contextFlags</i></c> field contains a copy of the flags passed
/// to <c><i>ffxFsr2ContextCreate</i></c> via the <c><i>flags</i></c> field of
/// the <c><i>FfxFsr2InitializationParams</i></c> structure. These flags are
/// used to determine which permutation of a pipeline for a specific
/// <c><i>FfxFsr2Pass</i></c> should be used to implement the features required
/// by each application, as well as to acheive the best performance on specific
/// target hardware configurations.
/// 
/// When using one of the provided backends for FSR2 (such as DirectX 12 or
/// Vulkan) the data required to create a pipeline is compiled offline and
/// included into the backend library that you are using. For cases where the
/// backend interface is overriden by providing custom callback function
/// implementations care should be taken to respect the contents of the
/// <c><i>contextFlags</i></c> field in order to correctly support the options
/// provided by FSR2, and acheive best performance.
///
/// @ingroup FSR2
typedef struct FfxPipelineDescription {

    uint32_t                            contextFlags;                   ///< A collection of <c><i>FfxFsr2InitializationFlagBits</i></c> which were passed to the context.
    FfxFilterType*                      samplers;                       ///< Array of static samplers.
    size_t                              samplerCount;                   ///< The number of samples contained inside <c><i>samplers</i></c>.
    const uint32_t*                     rootConstantBufferSizes;        ///< Array containing the sizes of the root constant buffers (count of 32 bit elements).
    uint32_t                            rootConstantBufferCount;        ///< The number of root constants contained within <c><i>rootConstantBufferSizes</i></c>.
} FfxPipelineDescription;

/// Create (or reference) a device.
///
/// The callback function should either create a new device or (more likely) it
/// should return an already existing device and add a reference to it (for 
/// those APIs which implement reference counting, such as DirectX 11 and 12).
///
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [out] outDevice                          The device that is either created (or referenced).
///
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
///
/// @ingroup FSR2
typedef FfxErrorCode (*FfxFsr2CreateDeviceFunc)(
    FfxFsr2Interface* backendInterface,
    FfxDevice outDevice);

/// Get a list of capabilities of the device.
///
/// When creating an <c><i>FfxFsr2Context</i></c> it is desirable for the FSR2
/// core implementation to be aware of certain characteristics of the platform
/// that is being targetted. This is because some optimizations which FSR2
/// attempts to perform are more effective on certain classes of hardware than
/// others, or are not supported by older hardware. In order to avoid cases
/// where optimizations actually have the effect of decreasing performance, or
/// reduce the breadth of support provided by FSR2, FSR2 queries the
/// capabilities of the device to make such decisions.
///
/// For target platforms with fixed hardware support you need not implement
/// this callback function by querying the device, but instead may hardcore
/// what features are available on the platform.
///
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [out] outDeviceCapabilities              The device capabilities structure to fill out.
/// @param [in] device                              The device to query for capabilities.
///
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
/// 
/// @ingroup FSR2
typedef FfxErrorCode(*FfxFsr2GetDeviceCapabilitiesFunc)(
    FfxFsr2Interface* backendInterface,
    FfxDeviceCapabilities* outDeviceCapabilities,
    FfxDevice device);

/// Destroy (or dereference) a device.
///
/// This function is called when the <c><i>FfxFsr2Context</i></c> is destroyed.
///
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [in] device                              The <c><i>FfxDevice</i></c> object to be destroyed (or deferenced).
///
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
///
/// @ingroup FSR2
typedef FfxErrorCode(*FfxFsr2DestroyDeviceFunc)(
    FfxFsr2Interface* backendInterface,
    FfxDevice device);

/// Create a resource.
///
/// This callback is intended for the backend to create internal resources.
///
/// Please note: It is also possible that the creation of resources might
/// itself cause additional resources to be created by simply calling the
/// <c><i>FfxFsr2CreateResourceFunc</i></c> function pointer again. This is
/// useful when handling the initial creation of resources which must be
/// initialized. The flow in such a case would be an initial call to create the
/// CPU-side resource, another to create the GPU-side resource, and then a call
/// to schedule a copy render job to move the data between the two. Typically
/// this type of function call flow is only seen during the creation of an
/// <c><i>FfxFsr2Context</i></c>.
///
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [in] createResourceDescription           A pointer to a <c><i>FfxCreateResourceDescription</i></c>.
/// @param [out] outResource                        A pointer to a <c><i>FfxResource</i></c> object.
///
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
/// 
/// @ingroup FSR2
typedef FfxErrorCode (*FfxFsr2CreateResourceFunc)(
    FfxFsr2Interface* backendInterface,
    const FfxCreateResourceDescription* createResourceDescription,
    FfxResourceInternal* outResource);

/// Register a resource in the backend for the current frame.
///
/// Since FSR2 and the backend are not aware how many different
/// resources will get passed to FSR2 over time, it's not safe 
/// to register all resources simultaneously in the backend.
/// Also passed resources may not be valid after the dispatch call.
/// As a result it's safest to register them as FfxResourceInternal 
/// and clear them at the end of the dispatch call.
///
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [in] inResource                          A pointer to a <c><i>FfxResource</i></c>.
/// @param [out] outResource                        A pointer to a <c><i>FfxResourceInternal</i></c> object.
///
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
/// 
/// @ingroup FSR2
typedef FfxErrorCode(*FfxFsr2RegisterResourceFunc)(
    FfxFsr2Interface* backendInterface,
    const FfxResource* inResource,
    FfxResourceInternal* outResource);

/// Unregister all temporary FfxResourceInternal from the backend.
///
/// Unregister FfxResourceInternal referencing resources passed to 
/// a function as a parameter.
///
/// @param [in] backendInterface                    A pointer to the backend interface.
///
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
/// 
/// @ingroup FSR2
typedef FfxErrorCode(*FfxFsr2UnregisterResourcesFunc)(
    FfxFsr2Interface* backendInterface);

/// Retrieve a <c><i>FfxResourceDescription</i></c> matching a
/// <c><i>FfxResource</i></c> structure. 
///
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [in] resource                            A pointer to a <c><i>FfxResource</i></c> object.
///
/// @returns
/// A description of the resource.
///
/// @ingroup FSR2
typedef FfxResourceDescription (*FfxFsr2GetResourceDescriptionFunc)(
    FfxFsr2Interface* backendInterface,
    FfxResourceInternal resource);

/// Destroy a resource
///
/// This callback is intended for the backend to release an internal resource.
///
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [in] resource                            A pointer to a <c><i>FfxResource</i></c> object.
/// 
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
/// 
/// @ingroup FSR2
typedef FfxErrorCode (*FfxFsr2DestroyResourceFunc)(
    FfxFsr2Interface* backendInterface,
    FfxResourceInternal resource);

/// Create a render pipeline.
///
/// A rendering pipeline contains the shader as well as resource bindpoints
/// and samplers.
/// 
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [in] pass                                The identifier for the pass.
/// @param [in] pipelineDescription                 A pointer to a <c><i>FfxPipelineDescription</i></c> describing the pipeline to be created.
/// @param [out] outPipeline                        A pointer to a <c><i>FfxPipelineState</i></c> structure which should be populated.
/// 
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
/// 
/// @ingroup FSR2
typedef FfxErrorCode (*FfxFsr2CreatePipelineFunc)(
    FfxFsr2Interface* backendInterface,
    FfxFsr2Pass pass,
    const FfxPipelineDescription* pipelineDescription,
    FfxPipelineState* outPipeline);

/// Destroy a render pipeline.
///
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [out] pipeline                           A pointer to a <c><i>FfxPipelineState</i></c> structure which should be released.
/// 
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
/// 
/// @ingroup FSR2
typedef FfxErrorCode (*FfxFsr2DestroyPipelineFunc)(
    FfxFsr2Interface* backendInterface,
    FfxPipelineState* pipeline);

/// Schedule a render job to be executed on the next call of
/// <c><i>FfxFsr2ExecuteRenderJobsFunc</i></c>.
///
/// Render jobs can perform one of three different tasks: clear, copy or
/// compute dispatches.
///
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [in] job                                 A pointer to a <c><i>FfxRenderJobDescription</i></c> structure.
/// 
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
/// 
/// @ingroup FSR2
typedef FfxErrorCode (*FfxFsr2ScheduleRenderJobFunc)(
    FfxFsr2Interface* backendInterface,
    const FfxRenderJobDescription* job);

/// Execute scheduled render jobs on the <c><i>comandList</i></c> provided.
/// 
/// The recording of the graphics API commands should take place in this
/// callback function, the render jobs which were previously enqueued (via
/// callbacks made to <c><i>FfxFsr2ScheduleRenderJobFunc</i></c>) should be
/// processed in the order they were received. Advanced users might choose to
/// reorder the rendering jobs, but should do so with care to respect the
/// resource dependencies.
/// 
/// Depending on the precise contents of <c><i>FfxFsr2DispatchDescription</i></c> a
/// different number of render jobs might have previously been enqueued (for
/// example if sharpening is toggled on and off).
/// 
/// @param [in] backendInterface                    A pointer to the backend interface.
/// @param [in] commandList                         A pointer to a <c><i>FfxCommandList</i></c> structure.
/// 
/// @retval
/// FFX_OK                                          The operation completed successfully.
/// @retval
/// Anything else                                   The operation failed.
/// 
/// @ingroup FSR2
typedef FfxErrorCode (*FfxFsr2ExecuteRenderJobsFunc)(
    FfxFsr2Interface* backendInterface,
    FfxCommandList commandList);

/// A structure encapsulating the interface between the core implentation of
/// the FSR2 algorithm and any graphics API that it should ultimately call.
/// 
/// This set of functions serves as an abstraction layer between FSR2 and the
/// API used to implement it. While FSR2 ships with backends for DirectX12 and
/// Vulkan, it is possible to implement your own backend for other platforms or
/// which sits ontop of your engine's own abstraction layer. For details on the
/// expectations of what each function should do you should refer the
/// description of the following function pointer types:
/// 
///     <c><i>FfxFsr2CreateDeviceFunc</i></c>
///     <c><i>FfxFsr2GetDeviceCapabilitiesFunc</i></c>
///     <c><i>FfxFsr2DestroyDeviceFunc</i></c>
///     <c><i>FfxFsr2CreateResourceFunc</i></c>
///     <c><i>FfxFsr2GetResourceDescriptionFunc</i></c>
///     <c><i>FfxFsr2DestroyResourceFunc</i></c>
///     <c><i>FfxFsr2CreatePipelineFunc</i></c>
///     <c><i>FfxFsr2DestroyPipelineFunc</i></c>
///     <c><i>FfxFsr2ScheduleRenderJobFunc</i></c>
///     <c><i>FfxFsr2ExecuteRenderJobsFunc</i></c>
///
/// Depending on the graphics API that is abstracted by the backend, it may be
/// required that the backend is to some extent stateful. To ensure that
/// applications retain full control to manage the memory used by FSR2, the
/// <c><i>scratchBuffer</i></c> and <c><i>scratchBufferSize</i></c> fields are
/// provided. A backend should provide a means of specifying how much scratch
/// memory is required for its internal implementation (e.g: via a function
/// or constant value). The application is that responsible for allocating that
/// memory and providing it when setting up the FSR2 backend. Backends provided
/// with FSR2 do not perform dynamic memory allocations, and instead
/// suballocate all memory from the scratch buffers provided.
///
/// The <c><i>scratchBuffer</i></c> and <c><i>scratchBufferSize</i></c> fields
/// should be populated according to the requirements of each backend. For
/// example, if using the DirectX 12 backend you should call the 
/// <c><i>ffxFsr2GetScratchMemorySizeDX12</i></c> function. It is not required
/// that custom backend implementations use a scratch buffer.
///
/// @ingroup FSR2
typedef struct FfxFsr2Interface {

    FfxFsr2CreateDeviceFunc                 fpCreateDevice;                 ///< A callback function to create (or reference) a device.
    FfxFsr2GetDeviceCapabilitiesFunc        fpGetDeviceCapabilities;        ///< A callback function to query device capabilites.
    FfxFsr2DestroyDeviceFunc                fpDestroyDevice;                ///< A callback function to destroy (or dereference) a device.
    FfxFsr2CreateResourceFunc               fpCreateResource;               ///< A callback function to create a resource.
    FfxFsr2RegisterResourceFunc             fpRegisterResource;             ///< A callback function to register an external resource.
    FfxFsr2UnregisterResourcesFunc          fpUnregisterResources;          ///< A callback function to unregister external resource.
    FfxFsr2GetResourceDescriptionFunc       fpGetResourceDescription;       ///< A callback function to retrieve a resource description.
    FfxFsr2DestroyResourceFunc              fpDestroyResource;              ///< A callback function to destroy a resource.
    FfxFsr2CreatePipelineFunc               fpCreatePipeline;               ///< A callback function to create a render or compute pipeline.
    FfxFsr2DestroyPipelineFunc              fpDestroyPipeline;              ///< A callback function to destroy a render or compute pipeline.
    FfxFsr2ScheduleRenderJobFunc            fpScheduleRenderJob;            ///< A callback function to schedule a render job.
    FfxFsr2ExecuteRenderJobsFunc            fpExecuteRenderJobs;            ///< A callback function to execute all queued render jobs.

    void*                                   scratchBuffer;                  ///< A preallocated buffer for memory utilized internally by the backend.
    size_t                                  scratchBufferSize;              ///< Size of the buffer pointed to by <c><i>scratchBuffer</i></c>.
} FfxFsr2Interface;

#if defined(__cplusplus)
}
#endif // #if defined(__cplusplus)
