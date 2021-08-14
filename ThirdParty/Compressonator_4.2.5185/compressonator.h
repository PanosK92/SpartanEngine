//=====================================================================
// Copyright (c) 2007-2021    Advanced Micro Devices, Inc. All rights reserved.
// Copyright (c) 2004-2006    ATI Technologies Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// \file Compressonator.h
//
//=====================================================================

#ifndef COMPRESSONATOR_H
#define COMPRESSONATOR_H

#define AMD_COMPRESS_VERSION_MAJOR 4  // The major version number of this release.
#define AMD_COMPRESS_VERSION_MINOR 2  // The minor version number of this release.

#include <stdint.h>
#include <vector>
#include <stddef.h>

#ifndef ASPM_GPU
namespace CMP {
// Basic types.
typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t  LONG;
typedef bool          BOOL;
#ifdef __linux__
typedef int32_t*      DWORD_PTR;
#else
typedef size_t        DWORD_PTR;
#endif
typedef unsigned int  UINT;

}  // namespace CMP

typedef CMP::DWORD              CMP_DWORD;
typedef CMP::WORD               CMP_WORD;
typedef CMP::BOOL               CMP_BOOL;
typedef CMP::DWORD_PTR          CMP_DWORD_PTR;
typedef CMP::BYTE               CMP_BYTE;

typedef long                 CMP_LONG;
typedef int                  CMP_INT;
typedef unsigned int         CMP_UINT;
typedef void                 CMP_VOID;
typedef float                CMP_FLOAT;
typedef char                 CMP_SBYTE;
typedef char                 CMP_CHAR;
typedef short                CMP_HALFSHORT;
typedef std::vector<uint8_t> CMP_VEC8;
typedef double               CMP_DOUBLE;


#if defined(WIN32) || defined(_WIN64)
#define CMP_API __cdecl
#else
#define CMP_API
#endif

// Texture format.
typedef enum {
    CMP_FORMAT_Unknown   = 0,         // Undefined texture format.

    // Channel Component formats --------------------------------------------------------------------------------
    CMP_FORMAT_RGBA_8888_S,   // RGBA format with signed 8-bit fixed channels.
    CMP_FORMAT_ARGB_8888_S,   // ARGB format with signed 8-bit fixed channels.
    CMP_FORMAT_ARGB_8888,             // ARGB format with 8-bit fixed channels.
    CMP_FORMAT_ABGR_8888,             // ABGR format with 8-bit fixed channels.
    CMP_FORMAT_RGBA_8888,             // RGBA format with 8-bit fixed channels.
    CMP_FORMAT_BGRA_8888,             // BGRA format with 8-bit fixed channels.
    CMP_FORMAT_RGB_888,               // RGB format with 8-bit fixed channels.
    CMP_FORMAT_RGB_888_S,             // RGB format with 8-bit fixed channels.
    CMP_FORMAT_BGR_888,               // BGR format with 8-bit fixed channels.
    CMP_FORMAT_RG_8_S,                // Two component format with signed 8-bit fixed channels.
    CMP_FORMAT_RG_8,                  // Two component format with 8-bit fixed channels.
    CMP_FORMAT_R_8_S,                 // Single component format with signed 8-bit fixed channel.
    CMP_FORMAT_R_8,                   // Single component format with 8-bit fixed channel.
    CMP_FORMAT_ARGB_2101010,          // ARGB format with 10-bit fixed channels for color & a 2-bit fixed channel for alpha.
    CMP_FORMAT_ARGB_16,               // ARGB format with 16-bit fixed channels.
    CMP_FORMAT_ABGR_16,               // ABGR format with 16-bit fixed channels.
    CMP_FORMAT_RGBA_16,               // RGBA format with 16-bit fixed channels.
    CMP_FORMAT_BGRA_16,               // BGRA format with 16-bit fixed channels.
    CMP_FORMAT_RG_16,                 // Two component format with 16-bit fixed channels.
    CMP_FORMAT_R_16,                  // Single component format with 16-bit fixed channels.
    CMP_FORMAT_RGBE_32F,              // RGB format with 9-bit floating point each channel and shared 5 bit exponent
    CMP_FORMAT_ARGB_16F,              // ARGB format with 16-bit floating-point channels.
    CMP_FORMAT_ABGR_16F,              // ABGR format with 16-bit floating-point channels.
    CMP_FORMAT_RGBA_16F,              // RGBA format with 16-bit floating-point channels.
    CMP_FORMAT_BGRA_16F,              // BGRA format with 16-bit floating-point channels.
    CMP_FORMAT_RG_16F,                // Two component format with 16-bit floating-point channels.
    CMP_FORMAT_R_16F,                 // Single component with 16-bit floating-point channels.
    CMP_FORMAT_ARGB_32F,              // ARGB format with 32-bit floating-point channels.
    CMP_FORMAT_ABGR_32F,              // ABGR format with 32-bit floating-point channels.
    CMP_FORMAT_RGBA_32F,              // RGBA format with 32-bit floating-point channels.
    CMP_FORMAT_BGRA_32F,              // BGRA format with 32-bit floating-point channels.
    CMP_FORMAT_RGB_32F,               // RGB format with 32-bit floating-point channels.
    CMP_FORMAT_BGR_32F,               // BGR format with 32-bit floating-point channels.
    CMP_FORMAT_RG_32F,                // Two component format with 32-bit floating-point channels.
    CMP_FORMAT_R_32F,                 // Single component with 32-bit floating-point channels.

    // Compression formats ------------ GPU Mapping DirectX, Vulkan and OpenGL formats and comments --------
    CMP_FORMAT_ASTC,                  // DXGI_FORMAT_UNKNOWN   VK_FORMAT_ASTC_4x4_UNORM_BLOCK to VK_FORMAT_ASTC_12x12_UNORM_BLOCK
    CMP_FORMAT_ATI1N,                 // DXGI_FORMAT_BC4_UNORM VK_FORMAT_BC4_UNORM_BLOCK GL_COMPRESSED_RED_RGTC1 Single component compression format using the same technique as DXT5 alpha. Four bits per pixel.
    CMP_FORMAT_ATI2N,                 // DXGI_FORMAT_BC5_UNORM VK_FORMAT_BC5_UNORM_BLOCK GL_COMPRESSED_RG_RGTC2 Two component compression format using the same technique as DXT5 alpha. Designed for compression of tangent space normal maps. Eight bits per pixel.
    CMP_FORMAT_ATI2N_XY,              // DXGI_FORMAT_BC5_UNORM VK_FORMAT_BC5_UNORM_BLOCK GL_COMPRESSED_RG_RGTC2 Two component compression format using the same technique as DXT5 alpha. The same as ATI2N but with the channels swizzled. Eight bits per pixel.
    CMP_FORMAT_ATI2N_DXT5,            // DXGI_FORMAT_BC5_UNORM VK_FORMAT_BC5_UNORM_BLOCK GL_COMPRESSED_RG_RGTC2 ATI2N like format using DXT5. Intended for use on GPUs that do not natively support ATI2N. Eight bits per pixel.
    CMP_FORMAT_ATC_RGB,               // CMP - a compressed RGB format.
    CMP_FORMAT_ATC_RGBA_Explicit,     // CMP - a compressed ARGB format with explicit alpha.
    CMP_FORMAT_ATC_RGBA_Interpolated, // CMP - a compressed ARGB format with interpolated alpha.
    CMP_FORMAT_BC1,                   // DXGI_FORMAT_BC1_UNORM GL_COMPRESSED_RGBA_S3TC_DXT1_EXT A four component opaque (or 1-bit alpha) compressed texture format for Microsoft DirectX10. Identical to DXT1.  Four bits per pixel.
    CMP_FORMAT_BC2,                   // DXGI_FORMAT_BC2_UNORM VK_FORMAT_BC2_UNORM_BLOCK GL_COMPRESSED_RGBA_S3TC_DXT3_EXT A four component compressed texture format with explicit alpha for Microsoft DirectX10. Identical to DXT3. Eight bits per pixel.
    CMP_FORMAT_BC3,                   // DXGI_FORMAT_BC3_UNORM VK_FORMAT_BC3_UNORM_BLOCK GL_COMPRESSED_RGBA_S3TC_DXT5_EXT A four component compressed texture format with interpolated alpha for Microsoft DirectX10. Identical to DXT5. Eight bits per pixel.
    CMP_FORMAT_BC4,                   // DXGI_FORMAT_BC4_UNORM VK_FORMAT_BC4_UNORM_BLOCK GL_COMPRESSED_RED_RGTC1 A single component compressed texture format for Microsoft DirectX10. Identical to ATI1N. Four bits per pixel.
    CMP_FORMAT_BC4_S,                 // DXGI_FORMAT_BC4_SNORM VK_FORMAT_BC4_SNORM_BLOCK GL_COMPRESSED_SIGNED_RED_RGTC1 A single component compressed texture format for Microsoft DirectX10. Identical to ATI1N. Four bits per pixel.
    CMP_FORMAT_BC5,                   // DXGI_FORMAT_BC5_UNORM VK_FORMAT_BC5_UNORM_BLOCK GL_COMPRESSED_RG_RGTC2 A two component compressed texture format for Microsoft DirectX10. Identical to ATI2N_XY. Eight bits per pixel.
    CMP_FORMAT_BC5_S,                 // DXGI_FORMAT_BC5_SNORM VK_FORMAT_BC5_SNORM_BLOCK GL_COMPRESSED_RGBA_BPTC_UNORM A two component compressed texture format for Microsoft DirectX10. Identical to ATI2N_XY. Eight bits per pixel.
    CMP_FORMAT_BC6H,                  // DXGI_FORMAT_BC6H_UF16 VK_FORMAT_BC6H_UFLOAT_BLOCK GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT BC6H compressed texture format (UF)
    CMP_FORMAT_BC6H_SF,               // DXGI_FORMAT_BC6H_SF16 VK_FORMAT_BC6H_SFLOAT_BLOCK GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT   BC6H compressed texture format (SF)
    CMP_FORMAT_BC7,                   // DXGI_FORMAT_BC7_UNORM VK_FORMAT_BC7_UNORM_BLOCK GL_COMPRESSED_RGBA_BPTC_UNORM BC7  compressed texture format
    CMP_FORMAT_DXT1,                  // DXGI_FORMAT_BC1_UNORM VK_FORMAT_BC1_RGB_UNORM_BLOCK GL_COMPRESSED_RGBA_S3TC_DXT1_EXT An DXTC compressed texture matopaque (or 1-bit alpha). Four bits per pixel.
    CMP_FORMAT_DXT3,                  // DXGI_FORMAT_BC2_UNORM VK_FORMAT_BC2_UNORM_BLOCK GL_COMPRESSED_RGBA_S3TC_DXT3_EXT    DXTC compressed texture format with explicit alpha. Eight bits per pixel.
    CMP_FORMAT_DXT5,                  // DXGI_FORMAT_BC3_UNORM VK_FORMAT_BC3_UNORM_BLOCK GL_COMPRESSED_RGBA_S3TC_DXT5_EXT    DXTC compressed texture format with interpolated alpha. Eight bits per pixel.
    CMP_FORMAT_DXT5_xGBR,             // DXGI_FORMAT_UNKNOWN DXT5 with the red component swizzled into the alpha channel. Eight bits per pixel.
    CMP_FORMAT_DXT5_RxBG,             // DXGI_FORMAT_UNKNOWN swizzled DXT5 format with the green component swizzled into the alpha channel. Eight bits per pixel.
    CMP_FORMAT_DXT5_RBxG,             // DXGI_FORMAT_UNKNOWN swizzled DXT5 format with the green component swizzled into the alpha channel & the blue component swizzled into the green channel. Eight bits per pixel.
    CMP_FORMAT_DXT5_xRBG,             // DXGI_FORMAT_UNKNOWN swizzled DXT5 format with the green component swizzled into the alpha channel & the red component swizzled into the green channel. Eight bits per pixel.
    CMP_FORMAT_DXT5_RGxB,             // DXGI_FORMAT_UNKNOWN swizzled DXT5 format with the blue component swizzled into the alpha channel. Eight bits per pixel.
    CMP_FORMAT_DXT5_xGxR,             // two-component swizzled DXT5 format with the red component swizzled into the alpha channel & the green component in the green channel. Eight bits per pixel.
    CMP_FORMAT_ETC_RGB,               // DXGI_FORMAT_UNKNOWN VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK GL_COMPRESSED_RGB8_ETC2  backward compatible
    CMP_FORMAT_ETC2_RGB,              // DXGI_FORMAT_UNKNOWN VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK GL_COMPRESSED_RGB8_ETC2
    CMP_FORMAT_ETC2_SRGB,             // DXGI_FORMAT_UNKNOWN VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK GL_COMPRESSED_SRGB8_ETC2
    CMP_FORMAT_ETC2_RGBA,             // DXGI_FORMAT_UNKNOWN VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK GL_COMPRESSED_RGBA8_ETC2_EAC
    CMP_FORMAT_ETC2_RGBA1,            // DXGI_FORMAT_UNKNOWN VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2
    CMP_FORMAT_ETC2_SRGBA,            // DXGI_FORMAT_UNKNOWN VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC
    CMP_FORMAT_ETC2_SRGBA1,           // DXGI_FORMAT_UNKNOWN VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2
    CMP_FORMAT_PVRTC,                 //
#ifdef USE_APC
    CMP_FORMAT_APC,                  //< APC Texture Compressor
#endif
    // Transcoder formats - ------------------------------------------------------------
    CMP_FORMAT_GTC,                   //< GTC   Fast Gradient Texture Compressor
    CMP_FORMAT_BASIS,                 //< BASIS compression

    // End of list
    CMP_FORMAT_MAX = CMP_FORMAT_BASIS
} CMP_FORMAT;

// Compress error codes
typedef enum {
    CMP_OK = 0,                            // Ok.
    CMP_ABORTED,                           // The conversion was aborted.
    CMP_ERR_INVALID_SOURCE_TEXTURE,        // The source texture is invalid.
    CMP_ERR_INVALID_DEST_TEXTURE,          // The destination texture is invalid.
    CMP_ERR_UNSUPPORTED_SOURCE_FORMAT,     // The source format is not a supported format.
    CMP_ERR_UNSUPPORTED_DEST_FORMAT,       // The destination format is not a supported format.
    CMP_ERR_UNSUPPORTED_GPU_ASTC_DECODE,   // The gpu hardware is not supported.
    CMP_ERR_UNSUPPORTED_GPU_BASIS_DECODE,  // The gpu hardware is not supported.
    CMP_ERR_SIZE_MISMATCH,                 // The source and destination texture sizes do not match.
    CMP_ERR_UNABLE_TO_INIT_CODEC,          // Compressonator was unable to initialize the codec needed for conversion.
    CMP_ERR_UNABLE_TO_INIT_DECOMPRESSLIB,  // GPU_Decode Lib was unable to initialize the codec needed for decompression .
    CMP_ERR_UNABLE_TO_INIT_COMPUTELIB,     // Compute Lib was unable to initialize the codec needed for compression.
    CMP_ERR_CMP_DESTINATION,               // Error in compressing destination texture
    CMP_ERR_MEM_ALLOC_FOR_MIPSET,          // Memory Error: allocating MIPSet compression level data buffer
    CMP_ERR_UNKNOWN_DESTINATION_FORMAT,    // The destination Codec Type is unknown! In SDK refer to GetCodecType()
    CMP_ERR_FAILED_HOST_SETUP,             // Failed to setup Host for processing
    CMP_ERR_PLUGIN_FILE_NOT_FOUND,         // The required plugin library was not found
    CMP_ERR_UNABLE_TO_LOAD_FILE,           // The requested file was not loaded
    CMP_ERR_UNABLE_TO_CREATE_ENCODER,      // Request to create an encoder failed
    CMP_ERR_UNABLE_TO_LOAD_ENCODER,        // Unable to load an encode library
    CMP_ERR_NOSHADER_CODE_DEFINED,         // No shader code is available for the requested framework
    CMP_ERR_GPU_DOESNOT_SUPPORT_COMPUTE,   // The GPU device selected does not support compute
    CMP_ERR_NOPERFSTATS,                   // No Performance Stats are available
    CMP_ERR_GPU_DOESNOT_SUPPORT_CMP_EXT,   // The GPU does not support the requested compression extension!
    CMP_ERR_GAMMA_OUTOFRANGE,              // Gamma value set for processing is out of range
    CMP_ERR_PLUGIN_SHAREDIO_NOT_SET,       // The plugin C_PluginSetSharedIO call was not set and is required for this plugin to operate
    CMP_ERR_UNABLE_TO_INIT_D3DX,           // Unable to initialize DirectX SDK or get a specific DX API
    CMP_FRAMEWORK_NOT_INITIALIZED,         // CMP_InitFramework failed or not called.
    CMP_ERR_GENERIC                        // An unknown error occurred.
} CMP_ERROR;

//======================================== Interfaces used in v3.2 and higher (Host Libs) ========================================
// An enum selecting the different GPU driver types.
typedef enum {
    CMP_UNKNOWN = 0,
    CMP_CPU     = 1,   //Use CPU Only, encoders defined CMP_CPUEncode or Compressonator lib will be used
    CMP_HPC     = 2,   //Use CPU High Performance Compute Encoders with SPMD support defined in CMP_CPUEncode)
    CMP_GPU_OCL = 3,   //Use GPU Kernel Encoders to compress textures using OpenCL Framework
    CMP_GPU_DXC = 4,   //Use GPU Kernel Encoders to compress textures using DirectX Compute Framework
    CMP_GPU_VLK = 5,   //Use GPU Kernel Encoders to compress textures using Vulkan Compute Framework
    CMP_GPU_HW  = 6    //Use GPU HW to encode textures , using gl extensions
} CMP_Compute_type;

struct ComputeOptions {
//public: data settings
    bool force_rebuild;                     //Force the GPU host framework to rebuild shaders
//private: data settings: Do not use or set these
    void *plugin_compute;                   // Ref to Encoder codec plugin: For Internal use (will be removed!)
};

typedef enum CMPComputeExtensions {
    CMP_COMPUTE_FP16        = 0x0001,       // Enable Packed Math Option for GPU
    CMP_COMPUTE_MAX_ENUM    = 0x7FFF
} CMP_ComputeExtensions;

struct KernelPerformanceStats {
    CMP_FLOAT   m_computeShaderElapsedMS;       // Total Elapsed Shader Time to process all the blocks
    CMP_INT     m_num_blocks;                   // Number of Texel (Typically 4x4) blocks
    CMP_FLOAT   m_CmpMTxPerSec;                 // Number of Mega Texels processed per second
};

struct KernelDeviceInfo {
    CMP_CHAR      m_deviceName[256];     // Device name (CPU or GPU)
    CMP_CHAR      m_version[128];        // Kernel pipeline version number (CPU or GPU)
    CMP_INT       m_maxUCores;           // Max Unit device CPU cores or GPU compute units (CU)
    // AMD GCN::One compute unit combines 64 shader processors
    // with 4 Texture Mapping units (TMU)
};

struct KernelOptions {
    CMP_ComputeExtensions   Extensions; // Compute extentions to use, set to 0 if you are not using any extensions
    CMP_DWORD  height;                  // Height of the encoded texture.
    CMP_DWORD  width;                   // Width of the encoded texture.
    CMP_FLOAT  fquality;                // Set the quality used for encoders 0.05 is the lowest and 1.0 for highest.
    CMP_FORMAT format;                  // Encoder codec format to use for processing
    CMP_FORMAT srcformat;               // Format of source data
    CMP_Compute_type encodeWith;        // Host Type : default is HPC, options are [HPC or GPU]
    CMP_INT    threads;                 // requested number of threads to use (1= single) max is 128 for HPC and 0 is auto (usually 2 per CPU core)
    CMP_BOOL   getPerfStats;            // Set to true if you want to get Performance Stats
    KernelPerformanceStats  perfStats;  // Data storage for the performance stats obtained from GPU or CPU while running encoder processing
    CMP_BOOL   getDeviceInfo;           // Set to true if you want to get target Device Info
    KernelDeviceInfo deviceInfo;        // Data storage for the target device
    CMP_BOOL   genGPUMipMaps;           // When ecoding with GPU HW use it to generate Compressed MipMap images, valid only if source has no miplevels
    CMP_INT    miplevels;               // When using GPU HW, generate upto this requested miplevel.
    CMP_BOOL   useSRGBFrames;           // Use SRGB frame buffer when generating HW based mipmaps (Default Gamma corretion will be set by HW)
                                        // if the source is SNORM then this option is enabled regardless of setting

    // The following applies to CMP_FORMAT format options
    union {
        CMP_BYTE encodeoptions[32];     // Aligned data block for encoder options

        // Options for BC15 which is a subset of low level : CMP_BC15Options, ref: SetUserBC15EncoderOptions() for settings
        struct { 
                CMP_BOOL   useChannelWeights;
                CMP_FLOAT  channelWeights[3];
                CMP_BOOL   useAdaptiveWeights;
                CMP_BOOL   useAlphaThreshold;
                CMP_INT    alphaThreshold;
                CMP_BOOL   useRefinementSteps;
                CMP_INT    refinementSteps;
        } bc15;
    };

//-------------------------------------------------------------------------------
//private: data settings: Do not use it will be removed from this interface!
//-------------------------------------------------------------------------------
    CMP_UINT   size;                    // Size of *data
    void    *data;                      // Data to pass down from CPU to kernel
    void    *dataSVM;                   // Data allocated as Shared by CPU and GPU (used only when code is running in 64bit and devices support SVM)
    char    *srcfile;                   // Shader source file location
};


//======================================== Interfaces used in Compressonator Lib =====================================================
#ifndef CMP_MAKEFOURCC
#define CMP_MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
   ((CMP_DWORD)(CMP_BYTE)(ch0) | ((CMP_DWORD)(CMP_BYTE)(ch1) << 8) |   \
   ((CMP_DWORD)(CMP_BYTE)(ch2) << 16) | ((CMP_DWORD)(CMP_BYTE)(ch3) << 24 ))
#endif

#define MS_FLAG_Default                0x0000
#define MS_FLAG_AlphaPremult           0x0001
#define MS_FLAG_DisableMipMapping      0x0002
#define AMD_MAX_CMDS        20
#define AMD_MAX_CMD_STR     32
#define AMD_MAX_CMD_PARAM   16

typedef struct {
    CMP_CHAR strCommand[AMD_MAX_CMD_STR];
    CMP_CHAR strParameter[AMD_MAX_CMD_PARAM];
} AMD_CMD_SET;

// An enum selecting the speed vs. quality trade-off.
typedef enum {
    CMP_Speed_Normal,     // Highest quality mode
    CMP_Speed_Fast,       // Slightly lower quality but much faster compression mode - DXTn & ATInN only
    CMP_Speed_SuperFast,  // Slightly lower quality but much, much faster compression mode - DXTn & ATInN only
} CMP_Speed;

// An enum selecting the different GPU driver types.
typedef enum {
    GPUDecode_OPENGL = 0,  // Use OpenGL   to decode Textures (default)
    GPUDecode_DIRECTX,     // Use DirectX  to decode Textures
    GPUDecode_VULKAN,      // Use Vulkan  to decode Textures
    GPUDecode_INVALID
} CMP_GPUDecode;

// mesh optimization feature is only available on windows OS
#ifdef _WIN32
#define USE_3DMESH_OPTIMIZE                         // Mesh Optimize
#endif


// CMP_PrintInfo
// function for printing std out info to users.
typedef void (CMP_API* CMP_PrintInfoStr)(const char* InfoStr );


// User options and setting used for processing
typedef struct {
    CMP_DWORD dwSize;                   // The size of this structure.

                                        // New to v4.2
    CMP_BOOL  bUseRefinementSteps;      // Used by BC1,BC2, BC3 codecs for improved quality, this setting will increase encoding time for better quality results
    CMP_INT   nRefinementSteps;         // Currently only 1 step is implemneted

                                        // v4.1 and older settings
    CMP_BOOL  bUseChannelWeighting;     // Use channel weightings. With swizzled formats the weighting applies to the data within the specified channel not the channel itself.
                                        // channel weigthing is not implemented for BC6H and BC7
    CMP_FLOAT fWeightingRed;            //    The weighting of the Red or X Channel.
    CMP_FLOAT fWeightingGreen;          //    The weighting of the Green or Y Channel.
    CMP_FLOAT fWeightingBlue;           //    The weighting of the Blue or Z Channel.
    CMP_BOOL  bUseAdaptiveWeighting;    //    Adapt weighting on a per-block basis.
    CMP_BOOL  bDXT1UseAlpha;            // Encode single-bit alpha data. Only valid when compressing to DXT1 & BC1.
    CMP_BOOL  bUseGPUDecompress;        // Use GPU to decompress. Decode API can be changed by specified in DecodeWith parameter. Default is OpenGL.
    CMP_BOOL  bUseCGCompress;           // Use SPMD/GPU to compress. Encode API can be changed by specified in EncodeWith parameter. Default is OpenCL.
    CMP_BYTE  nAlphaThreshold;          // The alpha threshold to use when compressing to DXT1 & BC1 with bDXT1UseAlpha. Texels with an alpha value less than the threshold are treated as transparent.
                                        // Note: When nCompressionSpeed is not set to Normal AphaThreshold is ignored for DXT1 & BC1
    CMP_BOOL  bDisableMultiThreading;   // Disable multi-threading of the compression. This will slow the compression but can be useful if you're managing threads in your application.
                                        // if set BC7 dwnumThreads will default to 1 during encoding and then return back to its original value when done.
    CMP_Speed nCompressionSpeed;        // The trade-off between compression speed & quality.
                                        // Notes:
                                        // 1. This value is ignored for BC6H and BC7 (for BC7 the compression speed depends on fquaility value)
                                        // 2. For 64 bit DXT1 to DXT5 and BC1 to BC5 nCompressionSpeed is ignored and set to Noramal Speed
                                        // 3. To force the use of nCompressionSpeed setting regarless of Note 2 use fQuality at 0.05
    CMP_GPUDecode    nGPUDecode;        // This value is set using DecodeWith argument (OpenGL, DirectX) default is OpenGL
    CMP_Compute_type nEncodeWith;       // This value is set using EncodeWith argument, currently only OpenCL is used
    CMP_DWORD        dwnumThreads;      // Number of threads to initialize for BC7 encoding (Max up to 128). Default set to auto,
    CMP_FLOAT        fquality;          // Quality of encoding. This value ranges between 0.0 and 1.0. BC7 & BC6 default is 0.05, others codecs are set at 1.0
                                        // setting fquality above 0.0 gives the fastest, lowest quality encoding, 1.0 is the slowest, highest quality encoding. Default set to a low value of 0.05
    CMP_BOOL brestrictColour;           // This setting is a quality tuning setting for BC7 which may be necessary for convenience in some applications. Default set to false
                                        // if  set and the block does not need alpha it instructs the code not to use modes that have combined colour + alpha - this
                                        // avoids the possibility that the encoder might choose an alpha other than 1.0 (due to parity) and cause something to
                                        // become accidentally slightly transparent (it's possible that when encoding 3-component texture applications will assume that
                                        // the 4th component can safely be assumed to be 1.0 all the time.)
    CMP_BOOL brestrictAlpha;            // This setting is a quality tuning setting for BC7 which may be necessary for some textures. Default set to false,
                                        // if set it will also apply restriction to blocks with alpha to avoid issues with punch-through or thresholded alpha encoding
    CMP_DWORD dwmodeMask;               // Mode to set BC7 to encode blocks using any of 8 different block modes in order to obtain the highest quality. Default set to 0xFF)
                                        // You can combine the bits to test for which modes produce the best image quality.
                                        // The mode that produces the best image quality above a set quality level (fquality) is used and subsequent modes set in the mask
                                        // are not tested, this optimizes the performance of the compression versus the required quality.
                                        // If you prefer to check all modes regardless of the quality then set the fquality to a value of 0
    int         NumCmds;                // Count of the number of command value pairs in CmdSet[].  Max value that can be set is AMD_MAX_CMDS = 20 on this release
    AMD_CMD_SET CmdSet[AMD_MAX_CMDS];   // Extended command options that can be set for the specified codec\n
                                        // Example to set the number of threads and quality used for compression\n
                                        //        CMP_CompressOptions Options;\n
                                        //        memset(Options,0,sizeof(CMP_CompressOptions));\n
                                        //        Options.dwSize = sizeof(CMP_CompressOptions)\n
                                        //        Options.CmdSet[0].strCommand   = "NumThreads"\n
                                        //        Options.CmdSet[0].strParameter = "8";\n
                                        //        Options.CmdSet[1].strCommand   = "Quality"\n
                                        //        Options.CmdSet[1].strParameter = "1.0";\n
                                        //        Options.NumCmds = 2;\n
    CMP_FLOAT fInputDefog;              // ToneMap properties for float type image send into non float compress algorithm.
    CMP_FLOAT fInputExposure;           //
    CMP_FLOAT fInputKneeLow;            //
    CMP_FLOAT fInputKneeHigh;           //
    CMP_FLOAT fInputGamma;              //

    CMP_INT iCmpLevel;                  // < draco setting: compression level (range 0-10: higher mean more compressed) - default 7
    CMP_INT iPosBits;                   // quantization bits for position - default 14
    CMP_INT iTexCBits;                  // quantization bits for texture coordinates - default 12
    CMP_INT iNormalBits;                // quantization bits for normal - default 10
    CMP_INT iGenericBits;               // quantization bits for generic - default 8

#ifdef USE_3DMESH_OPTIMIZE
    CMP_INT iVcacheSize;                // For mesh vertices optimization, hardware vertex cache size. (value range 1- no limit as it allows users to simulate hardware cache size to find the most optimum size)- default is enabled with cache size = 16
    CMP_INT iVcacheFIFOSize;            // For mesh vertices optimization, hardware vertex cache size. (value range 1- no limit as it allows users to simulate hardware cache size to find the most optimum size)- default is disabled.
    CMP_FLOAT   fOverdrawACMR;          // For mesh overdraw optimization,  optimize overdraw with ACMR (average cache miss ratio) threshold value specified (value range 1-3) - default is enabled with ACMR value = 1.05 (i.e. 5% worse)
    CMP_INT iSimplifyLOD;               // simplify mesh using LOD (Level of Details) value specified.(value range 1- no limit as it allows users to simplify the mesh until the level they desired. Higher level means less triangles drawn, less details.)
    bool bVertexFetch;                  // optimize vertices fetch . boolean value 0 - disabled, 1-enabled. -default is enabled.
#endif

    CMP_FORMAT SourceFormat;
    CMP_FORMAT DestFormat;
    CMP_BOOL   format_support_hostEncoder;  // Temp setting used while encoding with gpu or hpc plugins

    // User Print Info interface
    CMP_PrintInfoStr m_PrintInfoStr;

    // User Info for Performance Query on GPU or CPU Encoder Processing
    CMP_BOOL   getPerfStats;            // Set to true if you want to get Performance Stats
    KernelPerformanceStats  perfStats;  // Data storage for the performance stats obtained from GPU or CPU while running encoder processing
    CMP_BOOL   getDeviceInfo;           // Set to true if you want to get target device info
    KernelDeviceInfo deviceInfo;        // Data storage for the performance stats obtained from GPU or CPU while running encoder processing
    CMP_BOOL   genGPUMipMaps;           // When ecoding with GPU HW use it to generate MipMap images, valid only when miplevels is set else default is toplevel 1
    CMP_BOOL   useSRGBFrames;           // when using GPU HW for encoding and mipmap generation use SRGB frames, default is RGB
    CMP_INT    miplevels;               // miplevels to use when GPU is used to generate them
} CMP_CompressOptions;

//===================================
// Definitions for CMP MipSet
//===================================

/// The format of data in the channels of texture.
typedef enum
{
    CF_8bit       = 0,   // 8-bit integer data.
    CF_Float16    = 1,   // 16-bit float data.
    CF_Float32    = 2,   // 32-bit float data.
    CF_Compressed = 3,   // Compressed data.
    CF_16bit      = 4,   // 16-bit integer data.
    CF_2101010    = 5,   // 10-bit integer data in the color channels & 2-bit integer data in the alpha channel.
    CF_32bit      = 6,   // 32-bit integer data.
    CF_Float9995E = 7,   // 32-bit partial precision float.
    CF_YUV_420    = 8,   // YUV Chroma formats
    CF_YUV_422    = 9,   // YUV Chroma formats
    CF_YUV_444    = 10,  // YUV Chroma formats
    CF_YUV_4444   = 11,  // YUV Chroma formats
} CMP_ChannelFormat;

typedef CMP_ChannelFormat   ChannelFormat;

// The type of data the texture represents.
typedef enum {
    TDT_XRGB        = 0,  // An RGB texture padded to DWORD width.
    TDT_ARGB        = 1,  // An ARGB texture.
    TDT_NORMAL_MAP  = 2,  // A normal map.
    TDT_R           = 3,  // A single component texture.
    TDT_RG          = 4,  // A two component texture.
    TDT_YUV_SD      = 5,  // An YUB Standard Definition texture.
    TDT_YUV_HD      = 6,  // An YUB High Definition texture.
    TDT_RGB         = 7,  // An RGB texture
} CMP_TextureDataType;

typedef CMP_TextureDataType TextureDataType;

// The type of the texture.
typedef enum {
    TT_2D               = 0,  // A regular 2D texture. data stored linearly (rgba,rgba,...rgba)
    TT_CubeMap          = 1,  // A cubemap texture.
    TT_VolumeTexture    = 2,  // A volume texture.
    TT__2D_Block        = 3,  // 2D texture data stored as [Height][Width] blocks as individual channels using cmp_rgb_t or cmp_yuv_t
    TT_Unknown          = 4,  // Unknown type of texture : No data is stored for this type
} CMP_TextureType;

typedef CMP_TextureType TextureType;

typedef struct {
    union {
        CMP_BYTE  rgba[4];  // The color as an array of components.
        CMP_DWORD asDword;  // The color as a DWORD.
    };
} CMP_COLOR;

typedef struct
{
    int nFilterType;  // This is either CPU Box Filter or GPU Based DXD3X Filters

    // Setting that applies to a MIP Map Filters
    unsigned long dwMipFilterOptions;  // Selects options for the Filter Type
    int           nMinSize;            // Minimum MipMap Level requested
    float         fGammaCorrection;    // Apply Gamma correction to RGB channels, using this value as a power exponent,value of 0 or 1 = no correction

    // Setting that applies to a CAS Filter
    float fSharpness;  // Uses Fidelity Fx CAS sharpness, default 0 No sharpness set
    int   destWidth;   // Scale source texture width to destWidth default 0 no scaleing
    int   destHeight;  // Scale source texture height to destHeight default 0 no scalwing
    bool  useSRGB;     // if set true process image as SRGB else use linear color space. Default is false

} CMP_CFilterParams;


typedef enum
{
    CMP_VISION_DEFAULT = 0,     // Run image analysis or processing options, Align,Crop,SSIM, PSNR, ...
    CMP_VISION_LSTD    = 1,     // Run Laplacian operator and calculate standard deviation values
} CMP_VISION_PROCESS;

typedef struct
{
    CMP_VISION_PROCESS nProcessType;  // Type of image processing to perform
    CMP_BOOL Auto;         // Use Auto stting to align and crop images
    CMP_BOOL AlignImages;  // Align the Test image with the source image
    CMP_BOOL ShowImages;   // Display processed images
    CMP_BOOL SaveMatch;    // Save auto match image
    CMP_BOOL SaveImages;   // Save processed images
    CMP_BOOL SSIM;         // Run SSIM on test image
    CMP_BOOL PSNR;         // Run PSNR on test image
    CMP_BOOL ImageDiff;    // Run Image Diff
    CMP_BOOL CropImages;   // Crop the Test image with the source image using Crop %
    CMP_INT  Crop;         // Crop images within a set % range
} CMP_CVisionProcessOptions;

typedef struct
{
    CMP_INT   result;       // Return 0 is success else error value
    CMP_INT   imageSize;    // 0: if Source and Test Images are aligned with width & height 
                            // 1: Images were auto resized prior to processing
                            // 2: Images are not the same size 
    CMP_FLOAT srcLSTD;      // Laplacian Standard Deviation if the source sample
    CMP_FLOAT tstLSTD;      // Laplacian Standard Deviation if the test   sample
    CMP_FLOAT normLSTD;     // Normalized Laplacian Standard Deviation = tstLSTD / srcLSTD
    CMP_FLOAT SSIM;         // Simularity Index of Test Sample compared to the source
    CMP_FLOAT PSNR;         // Simularity Index of Test Sample compared to the source
} CMP_CVisionProcessResults;

// A MipLevel is the fundamental unit for containing texture data.
// \remarks
// One logical mip level can be composed of many MipLevels, see the documentation of MipSet for explanation.
// \sa \link TC_AppAllocateMipLevelData() TC_AppAllocateMipLevelData \endlink,
// \link TC_AppAllocateCompressedMipLevelData() TC_AppAllocateCompressedMipLevelData \endlink,
// \link MipSet \endlink
typedef struct {
    CMP_INT         m_nWidth;         // Width of the data in pixels.
    CMP_INT         m_nHeight;        // Height of the data in pixels.
    CMP_DWORD       m_dwLinearSize;   // Size of the data in bytes.
    union {
        CMP_SBYTE*      m_psbData;        // pointer signed 8  bit.data blocks
        CMP_BYTE*       m_pbData;         // pointer unsigned 8  bit.data blocks
        CMP_WORD*       m_pwData;         // pointer unsigned 16 bit.data blocks
        CMP_COLOR*      m_pcData;         // pointer to a union (array of 4 unsigned 8 bits or one 32 bit) data blocks
        CMP_FLOAT*      m_pfData;         // pointer to 32-bit signed float data blocks
        CMP_HALFSHORT*  m_phfsData;       // pointer to 16 bit short  data blocks
        CMP_DWORD*      m_pdwData;        // pointer to 32 bit data blocks
        CMP_VEC8*       m_pvec8Data;      // std::vector unsigned 8 bits data blocks
    };
} CMP_MipLevel;

typedef CMP_MipLevel  MipLevel;

typedef CMP_MipLevel* CMP_MipLevelTable; // A pointer to a set of MipLevels.

// Each texture and all its mip-map levels are encapsulated in a MipSet.
// Do not depend on m_pMipLevelTable being there, it is an implementation detail that you see only because there is no easy cross-complier
// way of passing data around in internal classes.
//
// For 2D textures there are m_nMipLevels MipLevels.
// Cube maps have multiple faces or sides for each mip-map level . Instead of making a totally new data type, we just made each one of these faces be represented by a MipLevel, even though the terminology can be a bit confusing at first. So if your cube map consists of 6 faces for each mip-map level, then your first mip-map level will consist of 6 MipLevels, each having the same m_nWidth, m_nHeight. The next mip-map level will have half the m_nWidth & m_nHeight as the previous, but will be composed of 6 MipLevels still.
// A volume texture is a 3D texture. Again, instead of creating a new data type, we chose to make use of multiple MipLevels to create a single mip-map level of a volume texture. So a single mip-map level of a volume texture will consist of many MipLevels, all having the same m_nWidth and m_nHeight. The next mip-map level will have m_nWidth and m_nHeight half of the previous mip-map level's (to a minimum of 1) and will be composed of half as many MipLevels as the previous mip-map level (the first mip-map level takes this number from the MipSet it's part of), to a minimum of one.

typedef struct {
    CMP_INT           m_nWidth;            // User Setting: Width in pixels of the topmost mip-map level of the mip-map set. Initialized by TC_AppAllocateMipSet.
    CMP_INT           m_nHeight;           // User Setting: Height in pixels of the topmost mip-map level of the mip-map set. Initialized by TC_AppAllocateMipSet.
    CMP_INT           m_nDepth;            // User Setting: Depth in MipLevels of the topmost mip-map level of the mip-map set. Initialized by TC_AppAllocateMipSet. See Remarks.
    CMP_FORMAT        m_format;            // User Setting: Format for this MipSet

    // set by various API for internal use and user ref
    ChannelFormat     m_ChannelFormat;     // A texture is usually composed of channels, such as RGB channels for a texture with red green and blue image data. m_ChannelFormat indicates the representation of each of these channels. So a texture where each channel is an 8 bit integer would have CF_8bit for this. A compressed texture would use CF_Compressed.
    TextureDataType   m_TextureDataType;   // An indication of the type of data that the texture contains. A texture with just RGB values would use TDT_XRGB, while a texture that also uses the alpha channel would use TDT_ARGB.
    TextureType       m_TextureType;       // Indicates whether the texture is 2D, a cube map, or a volume texture. Used to determine how to treat MipLevels, among other things.
    CMP_UINT          m_Flags;             // Flags that mip-map set.
    CMP_BYTE          m_CubeFaceMask;      // A mask of MS_CubeFace values indicating which cube-map faces are present.
    CMP_DWORD         m_dwFourCC;          // The FourCC for this mip-map set. 0 if the mip-map set is uncompressed. Generated using MAKEFOURCC (defined in the Platform SDK or DX SDK).
    CMP_DWORD         m_dwFourCC2;         // An extra FourCC used by The Compressonator internally. Our DDS plugin saves/loads m_dwFourCC2 from pDDSD ddpfPixelFormat.dwPrivateFormatBitCount (since it's not really used by anything else) whether or not it is 0. Generated using MAKEFOURCC (defined in the Platform SDK or DX SDK). The FourCC2 field is currently used to allow differentiation between the various swizzled DXT5 formats. These formats must have a FourCC of DXT5 to be supported by the DirectX runtime but The Compressonator needs to know the swizzled FourCC to correctly display the texture.
    CMP_INT           m_nMaxMipLevels;     // Set by The Compressonator when you call TC_AppAllocateMipSet based on the width, height, depth, and textureType values passed in. Is really the maximum number of mip-map levels possible for that texture including the topmost mip-map level if you integer divide width height and depth by 2, rounding down but never falling below 1 until all three of them are 1. So a 5x10 2D texture would have a m_nMaxMipLevels of 4 (5x10  2x5  1x2  1x1).
    CMP_INT           m_nMipLevels;        // The number of mip-map levels in the mip-map set that actually have data. Always less than or equal to m_nMaxMipLevels. Set to 0 after TC_AppAllocateMipSet.
    CMP_FORMAT        m_transcodeFormat;   // For universal format: Sets the target data format for data processing and analysis
    CMP_BOOL          m_compressed;        // New Flags if data is compressed (example Block Compressed data in form of BCxx)
    CMP_FORMAT        m_isDeCompressed;    // The New MipSet is a decompressed result from a prior Compressed MipSet Format specified
    CMP_BOOL          m_swizzle;           // Flag is used by image load and save to indicate channels were swizzled from the origial source
    CMP_BYTE          m_nBlockWidth;       // Width in pixels of the Compression Block that is to be processed default for ASTC is 4
    CMP_BYTE          m_nBlockHeight;      // Height in pixels of the Compression Block that is to be processed default for ASTC is 4
    CMP_BYTE          m_nBlockDepth;       // Depth in pixels of the Compression Block that is to be processed default for ASTC is 1
    CMP_BYTE          m_nChannels;         // Number of channels used min is 1 max is 4, 0 defaults to 1
    CMP_BYTE          m_isSigned;          // channel data is signed (has + and - data values)

    // set by various API for internal use. These values change when processing MipLevels
    CMP_DWORD         dwWidth;             // set by various API for ref,Width of the current active miplevel. if toplevel mipmap then value is same as m_nWidth
    CMP_DWORD         dwHeight;            // set by various API for ref,Height of the current active miplevel. if toplevel mipmap then value is same as m_nHeight
    CMP_DWORD         dwDataSize;          // set by various API for ref,Size of the current active miplevel allocated texture data.
    CMP_BYTE*         pData;               // set by various API for ref,Pointer to the current active miplevel texture data: used in MipLevelTable

    // Structure to hold all mip levels buffers
    CMP_MipLevelTable* m_pMipLevelTable;   // set by various API for ref, This is an implementation dependent way of storing the MipLevels that this mip-map set contains. Do not depend on it, use TC_AppGetMipLevel to access a mip-map set's MipLevels.
    void*              m_pReservedData;    // Reserved for ArchitectMF ImageLoader

    // Reserved for internal data tracking
    CMP_INT            m_nIterations;

    // Tracking for HW based mipmap compression
    CMP_INT            m_atmiplevel;
    CMP_INT            m_atfaceorslice;
} CMP_MipSet;

typedef CMP_MipSet   MipSet;

// The structure describing a texture.
typedef struct {
    CMP_DWORD  dwSize;          // Size of this structure.
    CMP_DWORD  dwWidth;         // Width of the texture.
    CMP_DWORD  dwHeight;        // Height of the texture.
    CMP_DWORD  dwPitch;         // Distance to start of next line,
    // necessary only for uncompressed textures.
    CMP_FORMAT format;          // Format of the texture.
    CMP_FORMAT transcodeFormat; // if the "format" is CMP_FORMAT_BASIS; A optional target
    // format can be set here (default is BC1),
    // it can also be conditionally set runtime
    CMP_BYTE   nBlockHeight;    // if the source is a compressed format,
    // specify its block dimensions (Default nBlockHeight = 4).
    CMP_BYTE   nBlockWidth;     // (Default nBlockWidth = 4)
    CMP_BYTE   nBlockDepth;     // For ASTC this is the z setting. (Default nBlockDepth = 1)
    CMP_DWORD  dwDataSize;      // Size of the current pData texture data
    CMP_BYTE*  pData;           // Pointer to the texture data to process, this can be the
    // image source or a specific MIP level
    CMP_VOID*  pMipSet;         // Pointer to a MipSet structure, typically used by Load Texture
    // and Save Texture. Users can access any MIP level or cube map
    // buffer using MIP Level access API and this pointer.
} CMP_Texture;


//==================================================
//API Definitions for Compressonator v3.1
//==================================================
// Number of image components
#define BC_COMPONENT_COUNT 4

// Number of bytes in a BC7 Block
#define BC_BLOCK_BYTES (4 * 4)

// Number of pixels in a BC7 block
#define BC_BLOCK_PIXELS BC_BLOCK_BYTES

// This defines the ordering in which components should be packed into
// the block for encoding
typedef enum _BC_COMPONENT {
    BC_COMP_RED   = 0,
    BC_COMP_GREEN = 1,
    BC_COMP_BLUE  = 2,
    BC_COMP_ALPHA = 3
} BC_COMPONENT;

typedef enum _BC_ERROR {
    BC_ERROR_NONE,
    BC_ERROR_LIBRARY_NOT_INITIALIZED,
    BC_ERROR_LIBRARY_ALREADY_INITIALIZED,
    BC_ERROR_INVALID_PARAMETERS,
    BC_ERROR_OUT_OF_MEMORY,
} BC_ERROR;

class BC7BlockEncoder;
class BC6HBlockEncoder;


#ifdef __cplusplus
extern "C"
{
#endif
extern CMP_INT CMP_MaxFacesOrSlices(const CMP_MipSet* pMipSet, CMP_INT nMipLevel);

//=================================================================================
//
// InitializeBCLibrary() - Startup the BC6H or BC7 library
//
// Must be called before any other library methods are valid
//
BC_ERROR CMP_API CMP_InitializeBCLibrary();

//
// ShutdownBCLibrary - Shutdown the BC6H or BC7 library
//
BC_ERROR CMP_API CMP_ShutdownBCLibrary();

typedef struct {
    CMP_WORD dwMask;          // User can enable or disable specific modes default is 0xFFFF
    float   fExposure;       // Sets the image lighter (using larger values) or darker (using lower values) default is 0.95
    bool     bIsSigned;       // Specify if half floats are signed or unsigned BC6H_UF16 or BC6H_SF16
    float   fQuality;        // Reserved: not used in BC6H at this time
    bool     bUsePatternRec;  // Reserved: for new algorithm to use mono pattern shape matching based on two pixel planes
} CMP_BC6H_BLOCK_PARAMETERS;

//
// CMP_CreateBC6HEncoder() - Creates an encoder object with the specified quality and settings for BC6H codec
// CMP_CreateBC7Encoder()  - Creates an encoder object with the specified quality and settings for BC7  codec
//
// Library must be initialized before calling this function.
//
// Arguments and Settings:
//
//      quality       - Quality of encoding. This value ranges between 0.0 and 1.0. (Valid only for BC7 in this release) default is 0.01
//                      0.0 gives the fastest, lowest quality encoding, 1.0 is the slowest, highest quality encoding
//                      In general even quality level 0.0 will give very good results on the vast majority of images
//                      Higher quality settings may be needed for some difficult images (e.g. normal maps) to give good results
//                      Encoding time will increase significantly at high quality levels. Quality levels around 0.8 will
//                      give very close to the highest possible quality, increasing the level above this will cause large
//                      increases in encoding time for very marginal gains in quality
//
//      performance   - Perfromance of encoding. This value ranges between 0.0 and 1.0. (Valid only for BC7 in this release) Typical default is 1.0
//                      Encoding time can be reduced by incresing this value for a given Quality level. Lower values will improve overall quality with
//                        optimal setting been performed at a value of 0.
//
//      restrictColor - (for BC7) This setting is a quality tuning setting which may be necessary for convenience in some applications.
//                      BC7 can be used for encoding data with up to four-components (e.g. ARGB), but the output of a BC7 decoder
//                        is effectively always 4-components, even if the original input contained less
//                      If BC7 is used to encode three-component data (e.g. RGB) then the encoder generally assumes that it doesn't matter what
//                      ends up in the 4th component of the data, however some applications might be written in such a way that they
//                      expect the 4th component to always be 1.0 (this might, for example, allow mixing of textures with and without
//                      alpha channels without special handling). In this example case the default behaviour of the encoder might cause some
//                      unexpected results, as the alpha channel is not guaranteed to always contain exactly 1.0 (since some error may be distributed
//                      into the 4th channel)
//                      If the restrictColor flag is set then for any input blocks where the 4th component is always 1.0 (255) the encoder will
//                      restrict itself to using encodings where the reconstructed 4th component is also always guaranteed to contain 1.0 (255)
//                      This may cause a very slight loss in overall quality measured in absolute RMS error, but this will generally be negligible
//
//      restrictAlpha - (for BC7) This setting is a quality tuning setting which may be necessary for some textures. Some textures may need alpha values
//                      of 1.0 and 0.0 to be exactly represented, but some BC7 block modes distribute error between the colour and alpha
//                      channels (because they have a shared least significant bit in the encoding). This could result in the alpha values
//                      being pulled away from zero or one by the global minimization of the error. If this flag is specified then the encoder
//                      will restrict its behaviour so that for blocks which contain an alpha of zero or one then these values should be
//                      precisely represented
//
//      modeMask      - This is an advanced option. (Valid only for BC7 in this release)
//                      BC7 can encode blocks using any of 8 different block modes in order to obtain the highest quality (for reference of how each
//                      of these block modes work consult the BC7 specification)
//                      Under some circumstances it is possible that it might be desired to manipulate the encoder to only produce certain modes
//                      Using this setting it is possible to instruct the encoder to only use certain block modes.
//                      This input is a bitmask of permitted modes for the encoder to use - for normal operation it should be set to 0xFF (all modes valid)
//                      The bitmask is arranged such that a setting of 0x1 only allows the encoder to use block mode 0.
//                      0x80 would only permit the use of block mode 7
//                      Restricting the available modes will generally reduce quality, but will also increase encoding speed
//
//      encoder       - Address of a pointer to an encoder.
//                      This function will allocate a BC7BlockEncoder or BC6HBlockEncoder object using new
//
//      isSigned      - For BC6H this flag sets the bit layout, false = UF16 (unsigned float) and true = SF16 (signed float)
//
// Note: For BC6H quality and modeMask are reserved for future release
//
BC_ERROR CMP_API CMP_CreateBC6HEncoder(CMP_BC6H_BLOCK_PARAMETERS user_settings, BC6HBlockEncoder** encoder);
BC_ERROR CMP_API CMP_CreateBC7Encoder(double quality, CMP_BOOL restrictColour, CMP_BOOL restrictAlpha, CMP_DWORD modeMask, double performance,
                                      BC7BlockEncoder** encoder);

// CMP_EncodeBC7Block()  - Enqueue a single BC7  block to the library for encoding
// CMP_EncodeBC6HBlock() - Enqueue a single BC6H block to the library for encoding
//
// For BC7:
// Input is expected to be a single 16 element block containing 4 components in the range 0.->255.
// Pixel data in the block should be arranged in row-major order
// For three-component input images the 4th component (BC7_COMP_ALPHA) should be set to 255 for
// all pixels to ensure optimal encoding
//
// For BC6H:
// Input is expected to be a single 16 element block containing 4 components in Half-Float format (16bit).
// Pixel data in the block should be arranged in row-major order.
// the 4th component should be set to 0, since Alpha is not supported in BC6H
//
BC_ERROR CMP_API CMP_EncodeBC7Block(BC7BlockEncoder* encoder, double in[BC_BLOCK_PIXELS][BC_COMPONENT_COUNT], CMP_BYTE* out);
BC_ERROR CMP_API CMP_EncodeBC6HBlock(BC6HBlockEncoder* encoder, CMP_FLOAT in[BC_BLOCK_PIXELS][BC_COMPONENT_COUNT], CMP_BYTE* out);

//
// CMP_DecodeBC6HBlock() - Decode a BC6H block to an uncompressed output
// CMP_DecodeBC7Block()  - Decode a BC7 block to an uncompressed output
//
// This function takes a pointer to an encoded BC block as input, decodes it and writes out the result
//
//
BC_ERROR CMP_API CMP_DecodeBC6HBlock(CMP_BYTE* in, CMP_FLOAT out[BC_BLOCK_PIXELS][BC_COMPONENT_COUNT]);
BC_ERROR CMP_API CMP_DecodeBC7Block(CMP_BYTE* in, double out[BC_BLOCK_PIXELS][BC_COMPONENT_COUNT]);

//
// CMP_DestroyBC6HEncoder() - Deletes a previously allocated encoder object
// CMP_DestroyBC7Encoder()  - Deletes a previously allocated encoder object
//
//
BC_ERROR CMP_API CMP_DestroyBC6HEncoder(BC6HBlockEncoder* encoder);
BC_ERROR CMP_API CMP_DestroyBC7Encoder(BC7BlockEncoder* encoder);

//=================================================================================

// CMP_Feedback_Proc
// Feedback function for conversion.
// \param[in] fProgress The percentage progress of the texture compression.
// \param[in] mipProgress The current MIP level been processed, value of fProgress = mipProgress
// \return non-NULL(true) value to abort conversion
typedef bool(CMP_API* CMP_Feedback_Proc)(CMP_FLOAT fProgress, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2);


// Calculates the required buffer size for the specified texture
// \param[in] pTexture A pointer to the texture.
// \return    The size of the buffer required to hold the texture data.
CMP_DWORD CMP_API CMP_CalculateBufferSize(const CMP_Texture* pTexture);

// Converts the source texture to the destination texture
// This can be compression, decompression or converting between two uncompressed formats.
// \param[in] pSourceTexture A pointer to the source texture.
// \param[in] pDestTexture A pointer to the destination texture.
// \param[in] pOptions A pointer to the compression options - can be NULL.
// \param[in] pFeedbackProc A pointer to the feedback function - can be NULL.
// \return    CMP_OK if successful, otherwise the error code.

CMP_ERROR CMP_API CMP_ConvertTexture(CMP_Texture* pSourceTexture, 
                                     CMP_Texture* pDestTexture, 
                                     const CMP_CompressOptions* pOptions,
                                     CMP_Feedback_Proc pFeedbackProc);


#ifdef __cplusplus
};
#endif

//==================================================
// API Definitions for Compressonator v3.2 and higher
//===================================================

typedef struct {
    CMP_FLOAT   mipProgress; // The percentage progress of the current MIP level texture compression
    CMP_INT     mipLevel;    // returns the current MIP level been processed 0..max available for the image
    CMP_INT     cubeFace;    // returns the current Cube Face been processed 1..6
} CMP_MIPPROGRESSPARAM;

// The structure describing block encoder level settings.
typedef struct {
    unsigned int  width;   // Width of the encoded texture.
    unsigned int  height;  // Height of the encoded texture.
    unsigned int  pitch;   // Distance to start of next line..
    float         quality; // Set the quality used for encoders 0.05 is the lowest and 1.0 for highest.
    unsigned int  format;  // Format of the encoder to use: this is a enum set see compressonator.h CMP_FORMAT
} CMP_EncoderSetting;


typedef enum _CMP_ANALYSIS_MODES
{
    CMP_ANALYSIS_MSEPSNR = 0x00000000  // Enable Measurement of MSE and PSNR for 2 mipset image samples
} CMP_ANALYSIS_MODES;

typedef struct
{
    // User settings
    unsigned long analysisMode;   // Bit mapped setting to enable various forms of image anlaysis
    unsigned int  channelBitMap;  // Bit setting for active channels to do analysis on and reserved features
                                  // msb(....ABGR)lsb

    // For HDR Image processing
    float fInputDefog;     // default = 0.0f
    float fInputExposure;  // default = 0.0f
    float fInputKneeLow;   // default = 0.0f
    float fInputKneeHigh;  // default = 5.0f
    float fInputGamma;     // default = 2.2f

    // Data return after anlysis
    double mse;    // Mean Square Error for all active channels in a given CMP_FORMAT
    double mseR;   // Mean Square for Red Channel
    double mseG;   // Mean Square for Green
    double mseB;   // Mean Square for Blue
    double mseA;   // Mean Square for Alpha
    double psnr;   // Peak Signal Ratio for all active channels in a given CMP_FORMAT
    double psnrR;  // Peak Signal Ratio for Red Chennel
    double psnrG;  // Peak Signal Ratio for Green
    double psnrB;  // Peak Signal Ratio for Blue
    double psnrA;  // Peak Signal Ratio for Alpha

} CMP_AnalysisData;


#ifdef __cplusplus
extern "C"
{
#endif

// MIP MAP Interfaces
CMP_INT     CMP_API CMP_CalcMaxMipLevel(CMP_INT nHeight, CMP_INT nWidth, CMP_BOOL bForGPU);
CMP_INT     CMP_API CMP_CalcMinMipSize(CMP_INT nHeight, CMP_INT nWidth, CMP_INT MipsLevel);
CMP_INT     CMP_API CMP_GenerateMIPLevelsEx(CMP_MipSet* pMipSet, CMP_CFilterParams* pCFilterParams);
CMP_INT     CMP_API CMP_GenerateMIPLevels(CMP_MipSet *pMipSet, CMP_INT nMinSize);
CMP_ERROR   CMP_API CMP_CreateCompressMipSet(CMP_MipSet* pMipSetCMP, CMP_MipSet* pMipSetSRC);
CMP_ERROR   CMP_API CMP_CreateMipSet(CMP_MipSet* pMipSet, CMP_INT nWidth, CMP_INT nHeight, CMP_INT nDepth, ChannelFormat channelFormat, TextureType textureType);

// MIP Map Quality
CMP_UINT    CMP_API CMP_getFormat_nChannels(CMP_FORMAT format);
CMP_ERROR   CMP_API CMP_MipSetAnlaysis(CMP_MipSet* src1, CMP_MipSet* src2, CMP_INT nMipLevel, CMP_INT nFaceOrSlice, CMP_AnalysisData* pAnalysisData);

// CMP_MIPFeedback_Proc
// Feedback function for conversion.
// \param[in] fProgress The percentage progress of the texture compression.
// \param[in] mipProgress The current MIP level been processed, value of fProgress = mipProgress
// \return non-NULL(true) value to abort conversion
typedef bool(CMP_API* CMP_MIPFeedback_Proc)(CMP_MIPPROGRESSPARAM mipProgress);

// Converts the source texture to the destination texture using MipSets with MIP MAP Levels
CMP_ERROR CMP_API CMP_ConvertMipTexture(CMP_MipSet* p_MipSetIn, CMP_MipSet* p_MipSetOut, const CMP_CompressOptions* pOptions, CMP_Feedback_Proc pFeedbackProc);


//--------------------------------------------
// CMP_Compute Lib: Texture Encoder Interfaces
//--------------------------------------------
CMP_ERROR   CMP_API CMP_LoadTexture(const char *sourceFile, CMP_MipSet *pMipSet);
CMP_ERROR   CMP_API CMP_SaveTexture(const char *destFile,   CMP_MipSet *pMipSet);
CMP_ERROR   CMP_API CMP_ProcessTexture(CMP_MipSet* srcMipSet, CMP_MipSet* dstMipSet, KernelOptions kernelOptions,  CMP_Feedback_Proc pFeedbackProc);
CMP_ERROR   CMP_API CMP_CompressTexture(KernelOptions *options,CMP_MipSet srcMipSet,CMP_MipSet dstMipSet,CMP_Feedback_Proc pFeedback);
CMP_VOID    CMP_API CMP_Format2FourCC(CMP_FORMAT format,   CMP_MipSet *pMipSet);
CMP_FORMAT  CMP_API CMP_ParseFormat(char* pFormat);
CMP_INT     CMP_API CMP_NumberOfProcessors();
CMP_VOID    CMP_API CMP_FreeMipSet(CMP_MipSet *MipSetIn);
CMP_VOID    CMP_API CMP_GetMipLevel(CMP_MipLevel **data, const CMP_MipSet* pMipSet, CMP_INT nMipLevel, CMP_INT nFaceOrSlice);
CMP_ERROR   CMP_API CMP_GetPerformanceStats(KernelPerformanceStats* pPerfStats);
CMP_ERROR   CMP_API CMP_GetDeviceInfo(KernelDeviceInfo* pDeviceInfo);
CMP_BOOL    CMP_API CMP_IsCompressedFormat(CMP_FORMAT format);
CMP_BOOL    CMP_API CMP_IsFloatFormat(CMP_FORMAT InFormat);

//--------------------------------------------
// CMP_Compute Lib: Host level interface
//--------------------------------------------
CMP_ERROR CMP_API CMP_CreateComputeLibrary(CMP_MipSet *srcTexture, KernelOptions  *kernelOptions, void *Reserved);
CMP_ERROR CMP_API CMP_DestroyComputeLibrary(CMP_BOOL forceClose);
CMP_ERROR CMP_API CMP_SetComputeOptions(ComputeOptions *options);

//---------------------------------------------------------
// Generic API to access the core using CMP_EncoderSetting
//----------------------------------------------------------
CMP_ERROR CMP_API CMP_CreateBlockEncoder(void** blockEncoder, CMP_EncoderSetting encodeSettings);
CMP_ERROR CMP_API CMP_CompressBlock(void** blockEncoder, void* srcBlock, unsigned int sourceStride, void* dstBlock, unsigned int dstStride);
CMP_ERROR CMP_API CMP_CompressBlockXY(void**       blockEncoder,
                                      unsigned int blockx,
                                      unsigned int blocky,
                                      void*        imgSrc,
                                      unsigned int sourceStride,
                                      void*        cmpDst,
                                      unsigned int dstStride);
void CMP_API CMP_DestroyBlockEncoder(void** blockEncoder);

//-----------------------------------
// CMP_Framework Lib: Host interface
//-----------------------------------
void CMP_InitFramework();


#ifdef __cplusplus
};
#endif

#endif  // ASPM_GPU

typedef bool (CMP_API * Codec_Feedback_Proc)(float fProgress, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2);
#endif  // COMPRESSONATOR_H
