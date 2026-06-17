#pragma once

#include "PhasmaMCP/Codebase/BM25Index.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <set>

namespace pmcp
{
    struct IndexerConfig
    {
        std::vector<std::string> directories;
        std::vector<std::string> include_files;    // Individual files to index (bypass skip rules)
        std::vector<std::string> skip_directories; // Directory names to skip during scanning
        std::vector<std::string> skip_files;       // File names to skip
        std::vector<std::string> extensions;       // Empty = index all text files
        std::vector<std::string> skip_extensions = {
            ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".exr", ".ktx", ".ktx2", ".dds",
            ".obj", ".fbx", ".gltf", ".glb", ".stl", ".ply", ".dae",
            ".ttf", ".otf", ".woff", ".woff2",
            ".wav", ".mp3", ".ogg", ".flac",
            ".zip", ".tar", ".gz", ".7z", ".rar",
            ".exe", ".dll", ".so", ".dylib", ".lib", ".a", ".o", ".pdb", ".bin",
            ".spv", ".dxo", ".cso"};
        std::vector<std::string> skip_regex; // Regex patterns to skip paths
        int max_chunk_chars = 4000;
        int chunk_overlap_lines = 3;
    };

    // Progress callback: (filesProcessed, totalFiles, currentFile)
    using IndexProgressCallback = std::function<void(int, int, const std::string &)>;

    class CodebaseIndexer
    {
    public:
        CodebaseIndexer(BM25Index *bm25,
                        IndexProgressCallback progressCb = nullptr,
                        std::function<void(const std::string &)> logCb = nullptr);

        // Run indexing synchronously. Call from a background thread.
        // Returns number of chunks indexed.
        int Index(const IndexerConfig &config);

        // Request cancellation (thread-safe).
        void Cancel();
        bool IsCancelled() const;

    private:
        struct Chunk
        {
            std::string content; // Text with file path + line range prefix
            std::string file;    // Source file path (relative)
            int startLine = 0;
            int endLine = 0;
        };

        std::vector<std::string> ScanFiles(const IndexerConfig &config) const;
        bool IsBinaryFile(const std::string &filePath, const std::set<std::string> &binaryExts) const;

        BM25Index *m_bm25;
        IndexProgressCallback m_progressCb;
        std::function<void(const std::string &)> m_logCb;
        std::atomic<bool> m_cancel{false};
    };
} // namespace pmcp
