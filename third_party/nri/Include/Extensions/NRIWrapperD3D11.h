// © 2021 NVIDIA Corporation

// Goal: wrapping native D3D11 objects into NRI objects

#pragma once

#define NRI_WRAPPER_D3D11_H 1

#include "NRIDeviceCreation.h"

typedef int32_t DXGIFormat;

NonNriForwardStruct(AGSContext);
NonNriForwardStruct(ID3D11Device);
NonNriForwardStruct(ID3D11Resource);
NonNriForwardStruct(ID3D11DeviceContext);

NriNamespaceBegin

NriStruct(DeviceCreationD3D11Desc) {
    ID3D11Device* d3d11Device;
    NriOptional AGSContext* agsContext;
    NriOptional Nri(CallbackInterface) callbackInterface;
    NriOptional Nri(AllocationCallbacks) allocationCallbacks;
    NriOptional uint32_t d3dShaderExtRegister;  // vendor specific shader extensions (default is "NRI_SHADER_EXT_REGISTER", space is always "0")
    NriOptional uint32_t d3dZeroBufferSize;     // no "memset" functionality in D3D, "CmdZeroBuffer" implemented via a bunch of copies (4 Mb by default)

    // Switches (disabled by default)
    bool enableNRIValidation;                   // embedded validation layer, checks for NRI specifics
    bool enableD3D11CommandBufferEmulation;     // enable? but why? (auto-enabled if deferred contexts are not supported)

    // Switches (enabled by default)
    bool disableNVAPIInitialization;            // at least NVAPI requires calling "NvAPI_Initialize" in DLL/EXE where the device is created
};

NriStruct(CommandBufferD3D11Desc) {
    ID3D11DeviceContext* d3d11DeviceContext;
};

NriStruct(BufferD3D11Desc) {
    ID3D11Resource* d3d11Resource;
    NriOptional const NriPtr(BufferDesc) desc;  // not all information can be retrieved from the resource if not provided
};

NriStruct(TextureD3D11Desc) {
    ID3D11Resource* d3d11Resource;
    NriOptional DXGIFormat format;             // must be provided "as a compatible typed format" if the resource is typeless
};

// Threadsafe: yes
NriStruct(WrapperD3D11Interface) {
    Nri(Result) (NRI_CALL *CreateCommandBufferD3D11)    (NriRef(Device) device, const NriRef(CommandBufferD3D11Desc) commandBufferD3D11Desc, NriOut NriRef(CommandBuffer*) commandBuffer);
    Nri(Result) (NRI_CALL *CreateBufferD3D11)           (NriRef(Device) device, const NriRef(BufferD3D11Desc) bufferD3D11Desc, NriOut NriRef(Buffer*) buffer);
    Nri(Result) (NRI_CALL *CreateTextureD3D11)          (NriRef(Device) device, const NriRef(TextureD3D11Desc) textureD3D11Desc, NriOut NriRef(Texture*) texture);
};

NRI_API Nri(Result) NRI_CALL nriCreateDeviceFromD3D11Device(const NriRef(DeviceCreationD3D11Desc) deviceDesc, NriOut NriRef(Device*) device);

NriNamespaceEnd
