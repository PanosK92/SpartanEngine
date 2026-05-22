#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <shared_mutex>

namespace pmcp
{
    // Lightweight BM25 keyword search index for code.
    // Tokenizes on word boundaries, splits camelCase and snake_case,
    // and scores documents using the Okapi BM25 formula.
    // Thread-safe for concurrent search; exclusive lock for Add/Remove/Clear.
    class BM25Index
    {
    public:
        struct Result
        {
            std::string id;
            std::string content;
            float score = 0.0f;
        };

        // Add a document. id must be unique; content is tokenized and indexed.
        void Add(const std::string &id, const std::string &content);

        // Rebuild the whole index from a document list in one pass.
        // Takes ownership of the vector so callers can std::move to avoid copies.
        void Rebuild(std::vector<std::pair<std::string, std::string>> documents);

        // Remove a document by id.
        void Remove(const std::string &id);

        // Remove all documents whose id starts with the given file path.
        void RemoveByFile(const std::string &file);

        // Search for documents matching the query. Returns top_k results sorted by score.
        std::vector<Result> Search(const std::string &query, int top_k = 10) const;

        // Search with multiple queries in parallel. Each document's score is the maximum
        // across all queries, so results cover all query angles simultaneously.
        // Returns top_k unique results sorted by merged score.
        std::vector<Result> SearchMulti(const std::vector<std::string> &queries, int top_k = 10) const;

        size_t Size() const;
        void Clear();

        // BM25 tuning parameters
        float k1 = 1.5f; // Term frequency saturation
        float b = 0.75f; // Length normalization

    private:
        // A stored document
        struct Document
        {
            std::string id;
            std::string content;
            std::unordered_map<std::string, int> termFreqs; // term -> count
            int totalTerms = 0;
        };

        // Split text into lowercase tokens, expanding camelCase and snake_case
        static std::vector<std::string> Tokenize(const std::string &text);

        // Split a single word on camelCase boundaries (e.g. "getRenderPass" -> "get", "render", "pass")
        static std::vector<std::string> SplitCamelCase(const std::string &word);

        std::vector<Document> m_docs;
        std::unordered_map<std::string, int> m_docFreq; // term -> number of docs containing it
        double m_avgDocLen = 0.0;

        mutable std::shared_mutex m_mutex;

        void RebuildStats();
    };
} // namespace pmcp
