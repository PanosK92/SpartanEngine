#include "PhasmaMCP/Codebase/BM25Index.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <future>
#include <unordered_map>

namespace pmcp
{
    // -------------------------------------------------------------------------
    // Tokenization
    // -------------------------------------------------------------------------

    std::vector<std::string> BM25Index::SplitCamelCase(const std::string &word)
    {
        // "getRenderPass" -> ["get", "render", "pass"]
        // "SSAOPass"      -> ["ssao", "pass"]
        // "m_renderPass"  -> ["m", "render", "pass"]
        std::vector<std::string> parts;
        std::string current;

        for (size_t i = 0; i < word.size(); ++i)
        {
            char c = word[i];
            bool isUpper = std::isupper(static_cast<unsigned char>(c));
            bool nextIsLower = (i + 1 < word.size()) &&
                               std::islower(static_cast<unsigned char>(word[i + 1]));

            // Start a new part when:
            // - uppercase followed by lowercase (camelCase boundary)
            // - transition from lowercase to uppercase
            if (isUpper && !current.empty())
            {
                bool prevIsUpper = std::isupper(static_cast<unsigned char>(current.back()));
                if (!prevIsUpper || nextIsLower)
                {
                    parts.push_back(current);
                    current.clear();
                }
            }

            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (!current.empty())
            parts.push_back(current);

        return parts;
    }

    std::vector<std::string> BM25Index::Tokenize(const std::string &text)
    {
        std::vector<std::string> tokens;
        std::string word;

        auto flushWord = [&]()
        {
            if (word.empty())
                return;

            // Split camelCase and add each sub-token
            auto parts = SplitCamelCase(word);
            for (auto &p : parts)
            {
                if (p.size() >= 2) // skip single-char tokens
                    tokens.push_back(std::move(p));
            }

            // Also add the full word (lowercase) for exact matching
            std::string lower;
            lower.reserve(word.size());
            for (char c : word)
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.size() >= 2)
                tokens.push_back(std::move(lower));

            word.clear();
        };

        for (char c : text)
        {
            if (std::isalnum(static_cast<unsigned char>(c)))
            {
                word += c;
            }
            else if (c == '_')
            {
                // snake_case boundary: flush current word, start new one
                flushWord();
            }
            else
            {
                flushWord();
            }
        }
        flushWord();

        return tokens;
    }

    // -------------------------------------------------------------------------
    // Index management
    // -------------------------------------------------------------------------

    void BM25Index::Add(const std::string &id, const std::string &content)
    {
        auto tokens = Tokenize(content);

        Document doc;
        doc.id = id;
        doc.content = content;
        doc.totalTerms = static_cast<int>(tokens.size());

        for (const auto &tok : tokens)
            doc.termFreqs[tok]++;

        std::unique_lock lock(m_mutex);

        // Replace if same id exists
        for (auto &d : m_docs)
        {
            if (d.id == id)
            {
                // Remove old doc freqs
                for (const auto &[term, _] : d.termFreqs)
                {
                    auto it = m_docFreq.find(term);
                    if (it != m_docFreq.end() && --it->second <= 0)
                        m_docFreq.erase(it);
                }
                d = std::move(doc);
                for (const auto &[term, _] : d.termFreqs)
                    m_docFreq[term]++;
                RebuildStats();
                return;
            }
        }

        // Add doc frequency counts
        for (const auto &[term, _] : doc.termFreqs)
            m_docFreq[term]++;

        m_docs.push_back(std::move(doc));
        RebuildStats();
    }

    void BM25Index::Rebuild(std::vector<std::pair<std::string, std::string>> documents)
    {
        std::vector<Document> docs;
        docs.reserve(documents.size());

        std::unordered_map<std::string, int> docFreq;
        double totalDocLen = 0.0;

        for (auto &[id, content] : documents)
        {
            auto tokens = Tokenize(content);

            Document doc;
            doc.id = std::move(id);
            doc.content = std::move(content);
            doc.totalTerms = static_cast<int>(tokens.size());

            for (const auto &tok : tokens)
                doc.termFreqs[tok]++;

            for (const auto &[term, _] : doc.termFreqs)
                docFreq[term]++;

            totalDocLen += static_cast<double>(doc.totalTerms);
            docs.push_back(std::move(doc));
        }

        std::unique_lock lock(m_mutex);
        m_docs = std::move(docs);
        m_docFreq = std::move(docFreq);
        m_avgDocLen = m_docs.empty() ? 0.0 : (totalDocLen / static_cast<double>(m_docs.size()));
    }

    void BM25Index::Remove(const std::string &id)
    {
        std::unique_lock lock(m_mutex);
        for (auto it = m_docs.begin(); it != m_docs.end(); ++it)
        {
            if (it->id == id)
            {
                for (const auto &[term, _] : it->termFreqs)
                {
                    auto dit = m_docFreq.find(term);
                    if (dit != m_docFreq.end() && --dit->second <= 0)
                        m_docFreq.erase(dit);
                }
                m_docs.erase(it);
                RebuildStats();
                return;
            }
        }
    }

    void BM25Index::RemoveByFile(const std::string &file)
    {
        std::unique_lock lock(m_mutex);
        bool changed = false;

        m_docs.erase(
            std::remove_if(m_docs.begin(), m_docs.end(),
                           [&](const Document &doc)
                           {
                               // Match documents whose content starts with the file path marker
                               if (doc.content.find("// File: " + file) == 0)
                               {
                                   for (const auto &[term, _] : doc.termFreqs)
                                   {
                                       auto it = m_docFreq.find(term);
                                       if (it != m_docFreq.end() && --it->second <= 0)
                                           m_docFreq.erase(it);
                                   }
                                   changed = true;
                                   return true;
                               }
                               return false;
                           }),
            m_docs.end());

        if (changed)
            RebuildStats();
    }

    void BM25Index::Clear()
    {
        std::unique_lock lock(m_mutex);
        m_docs.clear();
        m_docFreq.clear();
        m_avgDocLen = 0.0;
    }

    size_t BM25Index::Size() const
    {
        std::shared_lock lock(m_mutex);
        return m_docs.size();
    }

    void BM25Index::RebuildStats()
    {
        // Must be called under unique_lock
        if (m_docs.empty())
        {
            m_avgDocLen = 0.0;
            return;
        }

        double total = 0.0;
        for (const auto &doc : m_docs)
            total += doc.totalTerms;
        m_avgDocLen = total / m_docs.size();
    }

    // -------------------------------------------------------------------------
    // Search
    // -------------------------------------------------------------------------

    std::vector<BM25Index::Result> BM25Index::Search(const std::string &query, int top_k) const
    {
        auto queryTokens = Tokenize(query);
        if (queryTokens.empty())
            return {};

        // Deduplicate query tokens
        std::sort(queryTokens.begin(), queryTokens.end());
        queryTokens.erase(std::unique(queryTokens.begin(), queryTokens.end()), queryTokens.end());

        std::shared_lock lock(m_mutex);

        if (m_docs.empty())
            return {};

        const int N = static_cast<int>(m_docs.size());
        const double avgdl = m_avgDocLen > 0.0 ? m_avgDocLen : 1.0;

        std::vector<Result> results;
        results.reserve(m_docs.size());

        for (const auto &doc : m_docs)
        {
            double score = 0.0;
            const double dl = static_cast<double>(doc.totalTerms);

            for (const auto &term : queryTokens)
            {
                auto tfIt = doc.termFreqs.find(term);
                if (tfIt == doc.termFreqs.end())
                    continue;

                double tf = static_cast<double>(tfIt->second);

                // IDF: log((N - df + 0.5) / (df + 0.5) + 1)
                int df = 0;
                auto dfIt = m_docFreq.find(term);
                if (dfIt != m_docFreq.end())
                    df = dfIt->second;

                double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);

                // BM25 term score
                double tfNorm = (tf * (k1 + 1.0)) / (tf + k1 * (1.0 - b + b * (dl / avgdl)));
                score += idf * tfNorm;
            }

            if (score > 0.0)
                results.push_back({doc.id, doc.content, static_cast<float>(score)});
        }

        // Sort by score descending
        std::sort(results.begin(), results.end(),
                  [](const Result &a, const Result &b)
                  { return a.score > b.score; });

        if (static_cast<int>(results.size()) > top_k)
            results.resize(top_k);

        return results;
    }

    std::vector<BM25Index::Result> BM25Index::SearchMulti(const std::vector<std::string> &queries, int top_k) const
    {
        if (queries.empty())
            return {};

        // Single query: skip thread overhead
        if (queries.size() == 1)
            return Search(queries[0], top_k);

        // Launch one async search per query. BM25::Search takes a shared_lock so
        // multiple concurrent readers are safe.
        std::vector<std::future<std::vector<Result>>> futures;
        futures.reserve(queries.size());
        for (const auto &q : queries)
            futures.push_back(std::async(std::launch::async, [this, q_copy = q, top_k]()
                                         { return Search(q_copy, top_k); }));

        // Merge: for each document id keep the highest score across all queries.
        std::unordered_map<std::string, Result> merged;
        for (auto &f : futures)
        {
            for (auto &r : f.get())
            {
                auto [it, inserted] = merged.emplace(r.id, r);
                if (!inserted && r.score > it->second.score)
                    it->second.score = r.score;
            }
        }

        std::vector<Result> out;
        out.reserve(merged.size());
        for (auto &[id, r] : merged)
            out.push_back(std::move(r));

        int keep = std::min(static_cast<int>(out.size()), top_k);
        std::partial_sort(out.begin(), out.begin() + keep, out.end(),
                          [](const Result &a, const Result &b)
                          { return a.score > b.score; });
        out.resize(keep);

        return out;
    }
} // namespace pmcp
