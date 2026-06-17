#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pmcp
{
    class BM25Index;
    class CodebaseIndexer;

    struct CodebaseIndexingConfig
    {
        std::vector<std::string> directories;
        std::vector<std::string> include_files;
        std::vector<std::string> skip_directories;
        std::vector<std::string> skip_files;
        std::vector<std::string> skip_extensions;
        std::vector<std::string> skip_regex;
    };

    struct CodebaseIndexStatus
    {
        bool indexing = false;
        bool ready = false;
    };

    class CodebaseContext
    {
    public:
        std::shared_ptr<BM25Index> GetCodebaseBM25Shared() const;

        CodebaseIndexingConfig &MutableIndexingConfig();
        const CodebaseIndexingConfig &GetIndexingConfig() const;

        void EnsureStores();

        CodebaseIndexStatus GetStatus() const;
        void SetIndexing(bool indexing);

    private:
        std::shared_ptr<BM25Index> m_codebaseBM25;
        CodebaseIndexingConfig m_indexingConfig;

        std::atomic<bool> m_indexing{false};
    };
} // namespace pmcp
