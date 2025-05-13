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


#ifndef XESS_DEBUG_H
#define XESS_DEBUG_H

#include "xess.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XESS_DEBUG_ENABLE_PROFILING
#define XESS_DEBUG_ENABLE_PROFILING (1U << 30)
#endif

/**
 * @brief XeSS network type.
 */
typedef enum _xess_network_model_t
{
    XESS_NETWORK_MODEL_KPSS = 0,
    XESS_NETWORK_MODEL_SPLAT = 1,
    XESS_NETWORK_MODEL_3 = 2,
    XESS_NETWORK_MODEL_4 = 3,
    XESS_NETWORK_MODEL_5 = 4,
    XESS_NETWORK_MODEL_6 = 5,
    XESS_NETWORK_MODEL_UNKNOWN = 0x7FFFFFFF,
} xess_network_model_t;

typedef enum _xess_dump_element_bits_t
{
    XESS_DUMP_INPUT_COLOR = 0x01,
    XESS_DUMP_INPUT_VELOCITY = 0x02,
    XESS_DUMP_INPUT_DEPTH = 0x04,
    XESS_DUMP_INPUT_EXPOSURE_SCALE = 0x08,
    XESS_DUMP_INPUT_RESPONSIVE_PIXEL_MASK = 0x10,
    XESS_DUMP_OUTPUT = 0x20,
    XESS_DUMP_HISTORY = 0x40,
    XESS_DUMP_EXECUTION_PARAMETERS = 0x80, ///< All parameters passed to xessExecute
    XESS_DUMP_ALL_INPUTS = XESS_DUMP_INPUT_COLOR | XESS_DUMP_INPUT_VELOCITY |
                           XESS_DUMP_INPUT_DEPTH | XESS_DUMP_INPUT_EXPOSURE_SCALE |
                           XESS_DUMP_INPUT_RESPONSIVE_PIXEL_MASK | XESS_DUMP_EXECUTION_PARAMETERS,
    XESS_DUMP_ALL = 0x7FFFFFFF,
} xess_dump_element_bits_t;

typedef uint32_t xess_dump_elements_mask_t;

XESS_PACK_B()
typedef struct _xess_dump_parameters_t
{
    /**
     * NULL-terminated ASCII string to *existing folder* where dump files should be written.
     * Library do not create the folder.
     * Files in provided folder will be overwritten.
     */
    const char *path;
    /** Frame index. Will be used as start for frame sequence. */
    uint32_t frame_idx;
    /** Frame count to dump. Few frames less may be dumped due to possible frames in flight in
     * application
     */
    uint32_t frame_count;
    /** Bitset showing set of elements that must be dumped. Element will be dumped if it exists
     * and corresponding bit is not 0.
     * Since it's meaningless to call Dump with empty set value of 0 will mean DUMP_ALL_INPUTS
     */
    xess_dump_elements_mask_t dump_elements_mask;
    /** In case of depth render target with TYPELESS format it is not always possible
     *  to interpret depth values during dumping process.
     */
} xess_dump_parameters_t;
XESS_PACK_E()

XESS_PACK_B()
typedef struct _xess_profiled_frame_data_t
{
    /** Execution index in context's instance. */
    uint64_t frame_index;
    /** Total labeled gpu duration records stored in gpu_duration* arrays.*/
    uint64_t gpu_duration_record_count;
    /** Pointer to an internal array of duration names.*/
    const char* const* gpu_duration_names;
    /** Pointer to an internal array of duration values. [seconds]*/
    const double* gpu_duration_values;
} xess_profiled_frame_data_t;
XESS_PACK_E()

XESS_PACK_B()
typedef struct _xess_profiling_data_t
{
    /** Total profiled frame records storage in frames/executions array.*/
    uint64_t frame_count;
    /** Pointers to an internal storage with per frame/execution data.*/
    xess_profiled_frame_data_t* frames;
    /** Flag indicating if more profiling data will be available when GPU finishes
     * executing submitted frames.
     * Useful to collect profiling data without forcing full CPU-GPU sync.
     * Zero value indicates no pending profiling data.
     */
    uint32_t any_profiling_data_in_flight;
} xess_profiling_data_t;
XESS_PACK_E()

/** @addtogroup xess-debug XeSS API debug exports
 * @{
 */

/**
 * @brief Select network to be used by XeSS
 *
 * Selects network model to use by XeSS.
 * After call to this function - XeSS init function *must* be called
 */
XESS_API xess_result_t xessSelectNetworkModel(xess_context_handle_t hContext, xess_network_model_t network);

/**
 * @brief Dumps sequence of frames to the provided folder
 *
 * Call to this function initiates a dump for selected elements.
 * XeSS SDK uses RAM cache to reduce dump overhead. Application should provide reasonable
 * value for @ref xess_dump_parameters_t::frame_count frames (about 50 megs needed per cached frame).
 * To enable several dumps per run application should provide correct
 * @ref xess_dump_parameters_t::frame_idx value. This value used as a start index for frame
 * dumping.
 * After call to this function - each call to @ref xessD3D12Execute will result in new frame dumped to
 * RAM cache.
 * After @ref xess_dump_parameters_t::frame_count frames application will be blocked on call to
 * @ref xessD3D12Execute in order to save cached frames to disk. This operation can take long time.
 * Repetitive calls to this function can result in XESS_RESULT_ERROR_OPERATION_IN_PROGRESS which means that
 * frame dump is in progress.
 *
 * @param hContext XeSS context
 * @param dump_parameters dump configuration
 * @return operation status
 */
XESS_API xess_result_t xessStartDump(xess_context_handle_t hContext, const xess_dump_parameters_t *dump_parameters);


/**
 * @brief Query XeSS model performance data for past executions. To enable performance collection,
 * context must be initialized with XESS_DEBUG_ENABLE_PROFILING flag added to xess_d3d12_init_params_t::initFlags
 * or xess_vk_init_params_t::initFlags.
 * If profiling is enabled, user must poll for profiling data after executing one or more command lists, otherwise
 * implementation will keep growing internal CPU buffers to accommodate all profiling data available.
 * Due to async nature of execution on GPU, data may not be available after submitting command lists to device queue.
 * It is advised to check `any_profiling_data_in_flight` flag in case all workloads has been submitted, but profiling
 * data for some frames is still not available.
 * Data pointed to by pProfilingData item(s) belongs to context instance and
 * is valid until next call to xessD3D12GetProfilingData or xessGetProfilingData for owning context.
 * @param hContext: The XeSS context handle.
 * @param pProfilingData: pointer to profiling data structure to be filled by implementation.
 * @return XeSS return status code.
*/
XESS_API xess_result_t xessGetProfilingData(xess_context_handle_t hContext, xess_profiling_data_t** pProfilingData);

/** @}*/

// Enum size checks. All enums must be 4 bytes
typedef char sizecheck__LINE__[ (sizeof(xess_network_model_t) == 4) ? 1 : -1];
typedef char sizecheck__LINE__[ (sizeof(xess_dump_element_bits_t) == 4) ? 1 : -1];

#ifdef __cplusplus
}
#endif

#endif
