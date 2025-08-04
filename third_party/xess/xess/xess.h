/*******************************************************************************
 * Copyright (C) 2021 Intel Corporation
 * 
 * This software and the related documents are Intel copyrighted materials, and
 * your use of them is governed by the express license under which they were
 * provided to you ("License"). Unless the License provides otherwise, you may
 * not use, modify, copy, publish, distribute, disclose or transmit this
 * software or the related documents without Intel's prior written permission.
 * 
 * This software and the related documents are provided as is, with no express
 * or implied warranties, other than those that are expressly stated in the
 * License.
 ******************************************************************************/

#ifndef XESS_H
#define XESS_H

#ifdef XESS_SHARED_LIB
#ifdef _WIN32
#ifdef XESS_EXPORT_API
#define XESS_API __declspec(dllexport)
#else
#define XESS_API __declspec(dllimport)
#endif
#else
#define XESS_API __attribute__((visibility("default")))
#endif
#else
#define XESS_API
#endif

#if !defined _MSC_VER || (_MSC_VER >= 1929)
#define XESS_PRAGMA(x) _Pragma(#x)
#else
#define XESS_PRAGMA(x) __pragma(x)
#endif
#define XESS_PACK_B_X(x) XESS_PRAGMA(pack(push, x))
#define XESS_PACK_E() XESS_PRAGMA(pack(pop))
#define XESS_PACK_B() XESS_PACK_B_X(8)

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _xess_context_handle_t* xess_context_handle_t;

XESS_PACK_B()
/**
 * @brief XeSS version.
 * 
 * XeSS uses major.minor.patch version format and Numeric 90+ scheme for development stage builds.
 */
typedef struct _xess_version_t
{
    /** A major version increment indicates a new API and potentially a
     * break in functionality. */
    uint16_t major;
    /** A minor version increment indicates incremental changes such as
     * optional inputs or flags. This does not break existing functionality. */
    uint16_t minor;
    /** A patch version increment may include performance or quality tweaks or fixes for known issues.
     * There's no change in the interfaces.
     * Versions beyond 90 used for development builds to change the interface for the next release.
     */
    uint16_t patch;
    /** Reserved for future use. */
    uint16_t reserved;
} xess_version_t;
XESS_PACK_E()

XESS_PACK_B()
/**
 * @brief 2D variable.
 */
typedef struct _xess_2d_t
{
    uint32_t x;
    uint32_t y;
} xess_2d_t;
XESS_PACK_E()

/**
 * @brief 2D coordinates.
 */
typedef xess_2d_t xess_coord_t;

/**
 * @brief XeSS quality settings.
 */
typedef enum _xess_quality_settings_t
{
    XESS_QUALITY_SETTING_ULTRA_PERFORMANCE = 100,
    XESS_QUALITY_SETTING_PERFORMANCE = 101,
    XESS_QUALITY_SETTING_BALANCED = 102,
    XESS_QUALITY_SETTING_QUALITY = 103,
    XESS_QUALITY_SETTING_ULTRA_QUALITY = 104,
    XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS = 105,
    XESS_QUALITY_SETTING_AA = 106,
} xess_quality_settings_t;

/**
 * @brief XeSS initialization flags.
 */
typedef enum _xess_init_flags_t
{
    XESS_INIT_FLAG_NONE = 0,
    /** Use motion vectors at target resolution. */
    XESS_INIT_FLAG_HIGH_RES_MV = 1 << 0,
    /** Use inverted (increased precision) depth encoding */
    XESS_INIT_FLAG_INVERTED_DEPTH = 1 << 1,
    /** Use exposure texture to scale input color. */
    XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE = 1 << 2,
    /** Use responsive pixel mask texture. */
    XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK = 1 << 3,
    /** Use velocity in NDC */
    XESS_INIT_FLAG_USE_NDC_VELOCITY = 1 << 4,
    /** Use external descriptor heap */
    XESS_INIT_FLAG_EXTERNAL_DESCRIPTOR_HEAP = 1 << 5,
    /** Disable tonemapping for input and output */
    XESS_INIT_FLAG_LDR_INPUT_COLOR = 1 << 6,
    /** Remove jitter from input velocity*/
    XESS_INIT_FLAG_JITTERED_MV = 1 << 7,
    /** Enable automatic exposure calculation. */
    XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE = 1 << 8
} xess_init_flags_t;

XESS_PACK_B()
/**
 * @brief Properties for internal XeSS resources.
 */
typedef struct _xess_properties_t
{
    /** Required amount of descriptors for XeSS */
    uint32_t requiredDescriptorCount;
    /** The heap size required by XeSS for temporary buffer storage. */
    uint64_t tempBufferHeapSize;
    /** The heap size required by XeSS for temporary texture storage. */
    uint64_t tempTextureHeapSize;
} xess_properties_t;
XESS_PACK_E()

/**
 * @brief  XeSS return codes.
 */
typedef enum _xess_result_t
{
    /** Warning. Folder to store dump data doesn't exist. Write operation skipped.*/
    XESS_RESULT_WARNING_NONEXISTING_FOLDER = 1,
    /** An old or outdated driver. */
    XESS_RESULT_WARNING_OLD_DRIVER = 2,
    /** XeSS operation was successful. */
    XESS_RESULT_SUCCESS = 0,
    /** XeSS not supported on the GPU. An SM 6.4 capable GPU is required. */
    XESS_RESULT_ERROR_UNSUPPORTED_DEVICE = -1,
    /** An unsupported driver. */
    XESS_RESULT_ERROR_UNSUPPORTED_DRIVER = -2,
    /** Execute called without initialization. */
    XESS_RESULT_ERROR_UNINITIALIZED = -3,
    /** Invalid argument such as descriptor handles. */
    XESS_RESULT_ERROR_INVALID_ARGUMENT = -4,
    /** Not enough available GPU memory. */
    XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY = -5,
    /** Device function such as resource or descriptor creation. */
    XESS_RESULT_ERROR_DEVICE = -6,
    /** The function is not implemented */
    XESS_RESULT_ERROR_NOT_IMPLEMENTED = -7,
    /** Invalid context. */
    XESS_RESULT_ERROR_INVALID_CONTEXT = -8,
    /** Operation not finished yet. */
    XESS_RESULT_ERROR_OPERATION_IN_PROGRESS = -9,
    /** Operation not supported in current configuration. */
    XESS_RESULT_ERROR_UNSUPPORTED = -10,
    /** The library cannot be loaded. */
    XESS_RESULT_ERROR_CANT_LOAD_LIBRARY = -11,
    /** Call to function done in invalid order. */
    XESS_RESULT_ERROR_WRONG_CALL_ORDER = -12,

    /** Unknown internal failure */
    XESS_RESULT_ERROR_UNKNOWN = -1000,
} xess_result_t;

/**
 * @brief XeSS logging level
 */
typedef enum _xess_logging_level_t
{
    XESS_LOGGING_LEVEL_DEBUG = 0,
    XESS_LOGGING_LEVEL_INFO = 1,
    XESS_LOGGING_LEVEL_WARNING = 2,
    XESS_LOGGING_LEVEL_ERROR = 3
} xess_logging_level_t;

/**
 * A logging callback provided by the application. This callback can be called from other threads.
 * Message pointer are only valid inside function and may be invalid right after return call.
 * Message is a null-terminated utf-8 string
 */
 typedef void (*xess_app_log_callback_t)(const char *message, xess_logging_level_t loggingLevel);

#ifndef XESS_TYPES_ONLY

/** @addtogroup xess XeSS API exports
 * @{
 */
  
/**
 * @brief Gets the XeSS version. This is baked into the XeSS SDK release.
 * @param[out] pVersion Returned XeSS version.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessGetVersion(xess_version_t* pVersion);

/**
 * @brief Gets the version of the loaded Intel XeFX library. When running on Intel platforms 
 * this function will return the version of the loaded Intel XeFX library, for other 
 * platforms 0.0.0 will be returned.
 * @param hContext The XeSS context handle.
 * @param[out] pVersion Returned Intel XeFX library version.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessGetIntelXeFXVersion(xess_context_handle_t hContext, 
    xess_version_t* pVersion);

/**
 * @brief Gets XeSS internal resources properties.
 * @param hContext The XeSS context handle.
 * @param pOutputResolution Output resolution to calculate properties for.
 * @param[out] pBindingProperties Returned properties.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessGetProperties(xess_context_handle_t hContext,
    const xess_2d_t* pOutputResolution, xess_properties_t* pBindingProperties);

/**
 * @brief Get the input resolution for a specified output resolution for a given quality setting.
 * XeSS expects all the input buffers except motion vectors to be in the returned resolution.
 * Motion vectors can be either in output resolution (XESS_INIT_FLAG_HIGH_RES_MV) or
 * returned resolution (default).
 *
 * @param hContext The XeSS context handle.
 * @param pOutputResolution Output resolution to calculate input resolution for.
 * @param qualitySettings Desired quality setting.
 * @param[out] pInputResolution Required input resolution.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessGetInputResolution(xess_context_handle_t hContext,
    const xess_2d_t* pOutputResolution, xess_quality_settings_t qualitySettings,
    xess_2d_t* pInputResolution);

/**
 * @brief Get the optimal input resolution and possible range for a specified output resolution for a given quality setting.
 * XeSS expects all the input buffers except motion vectors to be in the returned resolution range 
 * and all input buffers to be in the same resolution.
 * Motion vectors can be either in output resolution (XESS_INIT_FLAG_HIGH_RES_MV) or
 * in the same resolution as other input buffers (by default).
 *
 * @note Aspect ratio of the input resolution must be the same as for the output resolution.
 *
 * @param hContext The XeSS context handle.
 * @param pOutputResolution Output resolution to calculate input resolution for.
 * @param qualitySettings Desired quality setting.
 * @param[out] pInputResolutionOptimal Optimal input resolution.
 * @param[out] pInputResolutionMin Required minimal input resolution.
 * @param[out] pInputResolutionMax Required maximal input resolution.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessGetOptimalInputResolution(xess_context_handle_t hContext,
    const xess_2d_t* pOutputResolution, xess_quality_settings_t qualitySettings,
    xess_2d_t* pInputResolutionOptimal, xess_2d_t* pInputResolutionMin, xess_2d_t* pInputResolutionMax);

/**
 * @brief Gets jitter scale value.
 * @param hContext The XeSS context handle.
 * @param[out] pX Jitter scale pointer for the X axis.
 * @param[out] pY Jitter scale pointer for the Y axis.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessGetJitterScale(xess_context_handle_t hContext, float* pX, float* pY);

/**
 * @brief Gets velocity scale value.
 * @param hContext The XeSS context handle.
 * @param[out] pX Velocity scale pointer for the X axis.
 * @param[out] pY Velocity scale pointer for the Y axis.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessGetVelocityScale(xess_context_handle_t hContext, float* pX, float* pY);

/**
 * @brief Destroys the XeSS context.
 * The user must ensure that any pending command lists are completed before destroying the context.
 * @param hContext: The XeSS context handle.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessDestroyContext(xess_context_handle_t hContext);

/**
 * @brief Sets jitter scale value
 *
 * @param hContext The XeSS context handle.
 * @param x scale for the X axis
 * @param y scale for the Y axis
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessSetJitterScale(xess_context_handle_t hContext, float x, float y);

/**
 * @brief Sets velocity scale value
 *
 * @param hContext The XeSS context handle.
 * @param x scale for the X axis
 * @param y scale for the Y axis
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessSetVelocityScale(xess_context_handle_t hContext, float x, float y);

/**
 * @brief Sets exposure scale value
 *
 * This value will be applied on top of any passed exposure value or automatically calculated exposure.
 *
 * @param hContext The XeSS context handle.
 * @param scale scale value.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessSetExposureMultiplier(xess_context_handle_t hContext, float scale);

/**
 * @brief Gets exposure scale value
 *
 * @param hContext The XeSS context handle.
 * @param[out] pScale Exposure scale pointer.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessGetExposureMultiplier(xess_context_handle_t hContext, float *pScale);

/**
 * @brief Sets maximum value for responsive mask
 *
 * This value used to clip responsive mask values. Final responsive mask value calculated as
 * clip(responsive_mask, 0.0, max_value). Value must be within range [0.0; 1.0]
 *
 * @param hContext The XeSS context handle.
 * @param value maximum clip value.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessSetMaxResponsiveMaskValue(xess_context_handle_t hContext, float value);

/**
 * @brief Gets maximum value for responsive mask
 *
 * @param hContext The XeSS context handle.
 * @param[out] pValue maximum clip value pointer.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessGetMaxResponsiveMaskValue(xess_context_handle_t hContext, float *pValue);

/**
 * @brief Sets logging callback
 *
 * @param hContext The XeSS context handle.
 * @param loggingLevel Minimum logging level for logging callback.
 * @param loggingCallback Logging callback
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessSetLoggingCallback(xess_context_handle_t hContext,
    xess_logging_level_t loggingLevel, xess_app_log_callback_t loggingCallback);

/**
 * @brief Indicates if the installed driver supports best XeSS experience.
 *
 * @param hContext The XeSS context handle.
 * @return xessIsOptimalDriver returns XESS_RESULT_SUCCESS, or XESS_RESULT_WARNING_OLD_DRIVER
 * if installed driver may result in degraded performance or visual quality.  
 * xessD3D12CreateContext(..) will return XESS_RESULT_ERROR_UNSUPPORTED_DRIVER if driver does
 * not support XeSS  at all.
 */
XESS_API xess_result_t xessIsOptimalDriver(xess_context_handle_t hContext);

/**
 * @brief Forces usage of legacy (pre 1.3.0) scale factors
 *
 * Following scale factors will be applied:
 * @li XESS_QUALITY_SETTING_ULTRA_PERFORMANCE: 3.0
 * @li XESS_QUALITY_SETTING_PERFORMANCE: 2.0
 * @li XESS_QUALITY_SETTING_BALANCED: 1.7
 * @li XESS_QUALITY_SETTING_QUALITY: 1.5
 * @li XESS_QUALITY_SETTING_ULTRA_QUALITY: 1.3
 * @li XESS_QUALITY_SETTING_AA: 1.0
 * In order to apply new scale factors application should call xessGetOptimalInputResolution and
 * initialization function (xess*Init)
 *
 * @param hContext The XeSS context handle.
 * @param force if set to true legacy scale factors will be forced, if set to false - scale factors
 * will be selected by XeSS
 *
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessForceLegacyScaleFactors(xess_context_handle_t hContext, bool force);

/**
 * @brief Returns current state of pipeline build
 * This function can only be called after xess*BuildPipelines and
 * before corresponding xess*Init.
 * This call returns XESS_RESULT_SUCCESS if pipelines already built, and
 * XESS_RESULT_ERROR_OPERATION_IN_PROGRESS if pipline build is in progress.
 * If function called before @ref xess*BuildPipelines or after @ref xess*Init -
 * XESS_RESULT_ERROR_WRONG_CALL_ORDER will be returned.
 *
 * @param hContext The XeSS context handle.
 * @return XESS_RESULT_SUCCESS if pipelines already built.
 *         XESS_RESULT_ERROR_OPERATION_IN_PROGRESS if pipeline build are in progress.
 *         XESS_RESULT_ERROR_WRONG_CALL_ORDER if the function is called out of order.
 */
XESS_API xess_result_t xessGetPipelineBuildStatus(xess_context_handle_t hContext);

/** @}*/
  
#endif

// Enum size checks. All enums must be 4 bytes
typedef char sizecheck__LINE__[ (sizeof(xess_quality_settings_t) == 4) ? 1 : -1];
typedef char sizecheck__LINE__[ (sizeof(xess_init_flags_t) == 4) ? 1 : -1];
typedef char sizecheck__LINE__[ (sizeof(xess_result_t) == 4) ? 1 : -1];
typedef char sizecheck__LINE__[ (sizeof(xess_logging_level_t) == 4) ? 1 : -1];


#ifdef __cplusplus
}
#endif

#endif
