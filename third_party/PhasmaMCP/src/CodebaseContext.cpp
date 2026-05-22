#include "PhasmaMCP/Codebase/CodebaseContext.h"
#include "PhasmaMCP/Codebase/BM25Index.h"

namespace pmcp
{
    std::shared_ptr<BM25Index> CodebaseContext::GetCodebaseBM25Shared() const
    {
        return m_codebaseBM25;
    }

    CodebaseIndexingConfig &CodebaseContext::MutableIndexingConfig()
    {
        return m_indexingConfig;
    }

    const CodebaseIndexingConfig &CodebaseContext::GetIndexingConfig() const
    {
        return m_indexingConfig;
    }

    void CodebaseContext::EnsureStores()
    {
        if (!m_codebaseBM25)
            m_codebaseBM25 = std::make_shared<BM25Index>();
    }

    CodebaseIndexStatus CodebaseContext::GetStatus() const
    {
        CodebaseIndexStatus status;
        status.indexing = m_indexing.load();
        status.ready = m_codebaseBM25 && m_codebaseBM25->Size() > 0;
        return status;
    }

    void CodebaseContext::SetIndexing(bool indexing)
    {
        m_indexing.store(indexing);
    }
} // namespace pmcp
