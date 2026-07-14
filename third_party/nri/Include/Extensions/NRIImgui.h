// © 2025 NVIDIA Corporation

// Goal: ImGui rendering

#pragma once

#define NRI_IMGUI_H 1

/*
Requirements:
- ImGui 1.92+ with "ImGuiBackendFlags_RendererHasTextures" flag ("IMGUI_DISABLE_OBSOLETE_FUNCTIONS" is recommended)
- unmodified "ImDrawVert" (20 bytes) and "ImDrawIdx" (2 bytes)
- "ImTextureID_Invalid" = 0

Expected usage:
- the goal of this extension is to support latest ImGui only
- designed only for rendering
- "drawList->AddCallback" functionality is not supported! But there is a special callback, allowing to override "hdrScale":
     drawList->AddCallback(NRI_IMGUI_OVERRIDE_HDR_SCALE(1000.0f)); // to override "DrawImguiDesc::hdrScale"
     drawList->AddCallback(NRI_IMGUI_OVERRIDE_HDR_SCALE(0.0f));    // to revert back to "DrawImguiDesc::hdrScale"
- "ImGui::Image*" functions are supported. "ImTextureID" must be a "SHADER_RESOURCE" descriptor:
     ImGui::Image((ImTextureID)descriptor, ...)
*/

NonNriForwardStruct(ImDrawList);
NonNriForwardStruct(ImTextureData);

NriNamespaceBegin

NriForwardStruct(Imgui);
NriForwardStruct(Streamer);

NriStruct(ImguiDesc) {
    NriOptional uint32_t descriptorPoolSize;    // upper bound of textures used by Imgui for drawing: {number of queued frames} * {number of "CmdDrawImgui" calls} * (1 + {"drawList->AddImage*" calls})
};

NriStruct(CopyImguiDataDesc) {
    const ImDrawList* const* drawLists;         // ImDrawData::CmdLists.Data
    uint32_t drawListNum;                       // ImDrawData::CmdLists.Size
    ImTextureData* const* textures;             // ImDrawData::Textures->Data (same as "ImGui::GetPlatformIO().Textures.Data")
    uint32_t textureNum;                        // ImDrawData::Textures->Size (same as "ImGui::GetPlatformIO().Textures.Size")
};

NriStruct(DrawImguiDesc) {
    const ImDrawList* const* drawLists;         // ImDrawData::CmdLists.Data (same as for "CopyImguiDataDesc")
    uint32_t drawListNum;                       // ImDrawData::CmdLists.Size (same as for "CopyImguiDataDesc")
    Nri(Dim2_t) displaySize;                    // ImDrawData::DisplaySize
    float hdrScale;                             // SDR intensity in HDR mode (1 by default)
    Nri(Format) attachmentFormat;               // destination attachment (render target) format
    bool linearColor;                           // apply de-gamma to vertex colors (needed for sRGB attachments and HDR)
};

// Threadsafe: yes
NriStruct(ImguiInterface) {
    Nri(Result) (NRI_CALL *CreateImgui)         (NriRef(Device) device, const NriRef(ImguiDesc) imguiDesc, NriOut NriRef(Imgui*) imgui);
    void        (NRI_CALL *DestroyImgui)        (NriPtr(Imgui) imgui);

    // Command buffer
    // {
        // Copy
        void    (NRI_CALL *CmdCopyImguiData)    (NriRef(CommandBuffer) commandBuffer, NriRef(Streamer) streamer, NriRef(Imgui) imgui, const NriRef(CopyImguiDataDesc) streamImguiDesc);

        // Draw (changes descriptor pool, pipeline layout and pipeline, barriers are externally controlled)
        void    (NRI_CALL *CmdDrawImgui)        (NriRef(CommandBuffer) commandBuffer, NriRef(Imgui) imgui, const NriRef(DrawImguiDesc) drawImguiDesc);
    // }
};

NriNamespaceEnd

#define NRI_IMGUI_OVERRIDE_HDR_SCALE(hdrScale) (ImDrawCallback)1, _NriCastFloatToVoidPtr(hdrScale)

inline void* _NriCastFloatToVoidPtr(float f) {
    // A strange cast is there to get a fast path in Imgui
    return *(void**)&f;
}
