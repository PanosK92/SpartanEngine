// © 2021 NVIDIA Corporation

// Goal: mesh shaders
// https://www.khronos.org/blog/mesh-shading-for-vulkan
// https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html

#pragma once

#define NRI_MESH_SHADER_H 1

NriNamespaceBegin

NriStruct(DrawMeshTasksDesc) {
    uint32_t x, y, z;
};

// Threadsafe: no
NriStruct(MeshShaderInterface) {
    // Command buffer
    // {
        // Draw
        void    (NRI_CALL *CmdDrawMeshTasks)            (NriRef(CommandBuffer) commandBuffer, const NriRef(DrawMeshTasksDesc) drawMeshTasksDesc);
        void    (NRI_CALL *CmdDrawMeshTasksIndirect)    (NriRef(CommandBuffer) commandBuffer, const NriRef(Buffer) buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, NriOptional  const NriPtr(Buffer) countBuffer, uint64_t countBufferOffset); // buffer contains "DrawMeshTasksDesc" commands
    // }
};

NriNamespaceEnd
