#include "PhasmaMCP/Codebase/CodebaseIndexer.h"
#include "PhasmaMCP/Utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace pmcp
{
    namespace fs = std::filesystem;

    CodebaseIndexer::CodebaseIndexer(BM25Index *bm25,
                                     IndexProgressCallback progressCb,
                                     std::function<void(const std::string &)> logCb)
        : m_bm25(bm25), m_progressCb(std::move(progressCb)), m_logCb(std::move(logCb))
    {
    }

    void CodebaseIndexer::Cancel()
    {
        m_cancel.store(true);
    }

    bool CodebaseIndexer::IsCancelled() const
    {
        return m_cancel.load();
    }

    static std::string toLower(std::string s)
    {
        for (auto &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    bool CodebaseIndexer::IsBinaryFile(const std::string &filePath, const std::set<std::string> &binaryExts) const
    {
        auto ext = toLower(fs::path(filePath).extension().string());
        return binaryExts.count(ext) > 0;
    }

    std::vector<std::string> CodebaseIndexer::ScanFiles(const IndexerConfig &config) const
    {
        std::vector<std::string> files;

        std::set<std::string> extSet;
        for (const auto &ext : config.extensions)
            extSet.insert(toLower(ext));

        std::set<std::string> skipDirs;
        for (auto d : config.skip_directories)
        {
            for (auto &ch : d)
                if (ch == '\\')
                    ch = '/';
            while (!d.empty() && d.back() == '/')
                d.pop_back();
            skipDirs.insert(toLower(d));
        }

        std::set<std::string> skipFiles;
        for (const auto &f : config.skip_files)
            skipFiles.insert(toLower(f));

        std::vector<std::regex> skipRegex;
        for (const auto &pat : config.skip_regex)
        {
            try
            {
                skipRegex.emplace_back(pat, std::regex::ECMAScript | std::regex::icase);
            }
            catch (...)
            {
            }
        }

        for (const auto &dir : config.directories)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                continue;

            auto it = fs::recursive_directory_iterator(dir, ec);
            for (auto end = fs::recursive_directory_iterator(); it != end; ++it)
            {
                const auto &entry = *it;

                if (entry.is_directory() && !skipDirs.empty())
                {
                    auto u8dir = entry.path().u8string();
                    std::string dirPath(u8dir.begin(), u8dir.end());
                    for (auto &ch : dirPath)
                        if (ch == '\\')
                            ch = '/';
                    while (!dirPath.empty() && dirPath.back() == '/')
                        dirPath.pop_back();
                    if (skipDirs.count(toLower(dirPath)))
                    {
                        it.disable_recursion_pending();
                        continue;
                    }
                }

                if (!entry.is_regular_file())
                    continue;

                const auto &p = entry.path();

                if (!skipFiles.empty())
                {
                    auto u8name = p.filename().u8string();
                    std::string fileName(u8name.begin(), u8name.end());
                    if (skipFiles.count(toLower(fileName)))
                        continue;
                }

                auto u8path = p.u8string();
                std::string filePath(u8path.begin(), u8path.end());
                auto u8ext = p.extension().u8string();
                std::string ext = toLower(std::string(u8ext.begin(), u8ext.end()));

                if (!extSet.empty() && !extSet.count(ext))
                    continue;

                if (!skipRegex.empty())
                {
                    bool matched = false;
                    for (const auto &re : skipRegex)
                        if (std::regex_search(filePath, re))
                        {
                            matched = true;
                            break;
                        }
                    if (matched)
                        continue;
                }

                files.push_back(std::move(filePath));
            }
        }

        for (const auto &f : config.include_files)
        {
            std::error_code ec;
            if (fs::is_regular_file(f, ec))
            {
                auto u8 = fs::path(f).u8string();
                files.emplace_back(u8.begin(), u8.end());
            }
        }

        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());
        return files;
    }

    static std::string ComputeCommonRoot(const std::vector<std::string> &directories)
    {
        if (directories.empty())
            return {};
        fs::path root = fs::path(directories[0]).parent_path();
        std::string commonRoot = root.string();
        for (auto &ch : commonRoot)
            if (ch == '\\')
                ch = '/';
        if (!commonRoot.empty() && commonRoot.back() != '/')
            commonRoot += '/';
        return commonRoot;
    }

    static std::string MakeRelative(const std::string &filePath, const std::string &commonRoot)
    {
        std::string rel = filePath;
        for (auto &ch : rel)
            if (ch == '\\')
                ch = '/';
        if (!commonRoot.empty() && rel.find(commonRoot) == 0)
            rel = rel.substr(commonRoot.size());
        return rel;
    }

    int CodebaseIndexer::Index(const IndexerConfig &config)
    {
        m_cancel.store(false);

        if (m_logCb)
            m_logCb("Scanning files...");
        auto files = ScanFiles(config);

        if (files.empty())
        {
            if (m_logCb)
                m_logCb("No files found to index.");
            return 0;
        }

        const std::string commonRoot = ComputeCommonRoot(config.directories);

        std::set<std::string> binaryExts;
        for (const auto &ext : config.skip_extensions)
            binaryExts.insert(toLower(ext));

        if (m_logCb)
            m_logCb("Found " + std::to_string(files.size()) + " files to index.");

        const int totalFiles = static_cast<int>(files.size());
        int totalChunks = 0;
        int filesProcessed = 0;

        for (const auto &filePath : files)
        {
            if (m_cancel.load())
                break;

            const std::string rel = MakeRelative(filePath, commonRoot);

            if (m_bm25)
                m_bm25->RemoveByFile(rel);

            auto extPos = filePath.rfind('.');
            const std::string ext = extPos != std::string::npos ? toLower(filePath.substr(extPos)) : "";
            const bool isBinary = binaryExts.count(ext) > 0;

            if (!isBinary)
            {
                std::ifstream f(fs::path(std::u8string(filePath.begin(), filePath.end())));
                if (!f.is_open())
                {
                    ++filesProcessed;
                    continue;
                }

                std::string source((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                if (source.empty())
                {
                    ++filesProcessed;
                    continue;
                }

                std::vector<std::string> lines;
                std::istringstream iss(source);
                std::string line;
                while (std::getline(iss, line))
                    lines.push_back(line);

                int lineIdx = 0;
                const int totalLines = static_cast<int>(lines.size());
                while (lineIdx < totalLines && !m_cancel.load())
                {
                    std::string prefix = "// File: " + rel + " (lines " + std::to_string(lineIdx + 1) + "-";
                    std::string body;
                    int chunkStart = lineIdx;
                    int charsUsed = static_cast<int>(prefix.size()) + 10;

                    while (lineIdx < totalLines)
                    {
                        int lineLen = static_cast<int>(lines[lineIdx].size()) + 1;
                        if (charsUsed + lineLen > config.max_chunk_chars && lineIdx > chunkStart)
                            break;
                        body += lines[lineIdx] + "\n";
                        charsUsed += lineLen;
                        ++lineIdx;
                    }

                    if (lineIdx < totalLines && lineIdx > chunkStart + 1)
                    {
                        for (int look = lineIdx - 1; look > lineIdx - 10 && look > chunkStart; --look)
                        {
                            if (lines[look].find_first_not_of(" \t\r") == std::string::npos)
                            {
                                body.clear();
                                for (int i = chunkStart; i <= look; ++i)
                                    body += lines[i] + "\n";
                                lineIdx = look + 1;
                                break;
                            }
                        }
                    }

                    int endLine = lineIdx;
                    std::string content = SanitizeUTF8(prefix + std::to_string(endLine) + ")\n" + body);
                    std::string id = "codebase_" + rel + ":" + std::to_string(chunkStart + 1);
                    if (m_bm25)
                        m_bm25->Add(id, content);
                    ++totalChunks;

                    if (lineIdx < totalLines && config.chunk_overlap_lines > 0)
                        lineIdx = std::max(chunkStart + 1, lineIdx - config.chunk_overlap_lines);
                }
            }

            ++filesProcessed;
            if (m_progressCb)
                m_progressCb(filesProcessed, totalFiles, rel);
        }

        if (m_logCb)
            m_logCb("Indexing " + std::string(m_cancel.load() ? "cancelled" : "complete") +
                    ": " + std::to_string(totalChunks) + " chunks from " +
                    std::to_string(filesProcessed) + " files.");

        return totalChunks;
    }
} // namespace pmcp
