// © 2024 NVIDIA Corporation

// Goal: data streaming

#pragma once

#define NRI_STREAMER_H 1

NriNamespaceBegin

NriForwardStruct(Streamer);

NriStruct(DataSize) {
    const void* data;
    uint64_t size;
};

NriStruct(BufferOffset) {
    NriPtr(Buffer) buffer;
    uint64_t offset;
};

NriStruct(StreamerDesc) {
    // Statically allocated ring-buffer for dynamic constants
    NriOptional Nri(MemoryLocation) constantBufferMemoryLocation; // UPLOAD or DEVICE_UPLOAD
    NriOptional uint64_t constantBufferSize;            // should be large enough to avoid overwriting data for enqueued frames

    // Dynamically (re)allocated ring-buffer for copying and rendering
    Nri(MemoryLocation) dynamicBufferMemoryLocation;    // UPLOAD or DEVICE_UPLOAD
    Nri(BufferDesc) dynamicBufferDesc;                  // "size" is ignored
    uint32_t queuedFrameNum;                            // number of frames "in-flight" (usually 1-3), adds 1 under the hood for the current "not-yet-committed" frame
};

NriStruct(StreamBufferDataDesc) {
    // Data to upload
    const NriPtr(DataSize) dataChunks;                  // will be concatenated in dynamic buffer memory
    uint32_t dataChunkNum;
    uint32_t placementAlignment;                        // desired alignment for "BufferOffset::offset"

    // Destination
    NriOptional NriPtr(Buffer) dstBuffer;
    NriOptional uint64_t dstOffset;
};

NriStruct(StreamTextureDataDesc) {
    // Data to upload
    const void* data;
    uint32_t dataRowPitch;
    uint32_t dataSlicePitch;

    // Destination
    NriOptional NriPtr(Texture) dstTexture;
    NriOptional Nri(TextureRegionDesc) dstRegion;
};

// Threadsafe: yes by default (see NRI_STREAMER_THREAD_SAFE CMake option)
NriStruct(StreamerInterface) {
    Nri(Result)         (NRI_CALL *CreateStreamer)              (NriRef(Device) device, const NriRef(StreamerDesc) streamerDesc, NriOut NriRef(Streamer*) streamer);
    void                (NRI_CALL *DestroyStreamer)             (NriPtr(Streamer) streamer);

    // Statically allocated (never changes)
    NriPtr(Buffer)      (NRI_CALL *GetStreamerConstantBuffer)   (NriRef(Streamer) streamer);

    // (HOST) Stream data to a dynamic buffer. Return "buffer & offset" for direct usage in the current frame
    Nri(BufferOffset)   (NRI_CALL *StreamBufferData)            (NriRef(Streamer) streamer, const NriRef(StreamBufferDataDesc) streamBufferDataDesc);
    Nri(BufferOffset)   (NRI_CALL *StreamTextureData)           (NriRef(Streamer) streamer, const NriRef(StreamTextureDataDesc) streamTextureDataDesc);

    // (HOST) Stream data to a constant buffer. Return "offset" in "GetStreamerConstantBuffer" for direct usage in the current frame
    uint32_t            (NRI_CALL *StreamConstantData)          (NriRef(Streamer) streamer, const void* data, uint32_t dataSize);

    // Command buffer
    // {
        // (DEVICE) Copy data to destinations (if any), which must be in "COPY_DESTINATION" state
        void            (NRI_CALL *CmdCopyStreamedData)         (NriRef(CommandBuffer) commandBuffer, NriRef(Streamer) streamer);
    // }

    // (HOST) Must be called once at the very end of the frame
    void                (NRI_CALL *EndStreamerFrame)            (NriRef(Streamer) streamer);
};

NriNamespaceEnd