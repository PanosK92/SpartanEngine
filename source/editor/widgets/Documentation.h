/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include "Widget.h"
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
//================================

namespace doc
{
    enum class block_type : uint8_t
    {
        heading,
        paragraph,
        list_item,
        code,
        table,
        quote,
        rule
    };

    enum span_style : uint8_t
    {
        span_bold   = 1 << 0,
        span_italic = 1 << 1,
        span_code   = 1 << 2
    };

    struct span
    {
        std::string text;
        std::string link;
        uint8_t style = 0;
    };

    using spans = std::vector<span>;

    struct block
    {
        block_type type = block_type::paragraph;
        int level       = 0; // heading level or list nesting
        std::string marker;  // ordered list number
        std::string anchor;  // heading slug, as github makes them
        spans content;
        std::vector<std::string> lines;      // code block
        std::vector<std::vector<spans>> rows; // table, the first row is the header
        std::string text;                     // plain text, for search
        std::string text_lower;
        float height       = -1.0f; // measured last frame, lets off-screen blocks be skipped
        float height_width = 0.0f;
    };

    struct page
    {
        std::string path; // absolute, forward slashes
        std::string title;
        std::vector<block> blocks;
    };

    struct search_hit
    {
        std::shared_ptr<page> target;
        int block = 0;
        std::string section;
        std::string snippet;
    };
}

class Documentation : public Widget
{
public:
    Documentation(Editor* editor);

    void OnTick() override;
    void OnTickVisible() override;

    // opens a markdown file, absolute or relative to the repository root, at an optional heading anchor
    void Open(const std::string& path, const std::string& anchor = "");

private:
    struct location
    {
        std::string path;
        float scroll = 0.0f;
    };

    void load_library();
    void start_download();
    std::shared_ptr<doc::page> get_page(const std::string& path);
    void navigate(const std::string& path, const std::string& anchor, bool push_history, int block = -1);
    void go_back();
    void go_forward();
    void follow_link(const std::string& url);
    void run_search();
    void draw_toolbar();
    void draw_sidebar();
    void draw_page();

    std::string m_root; // repository root, absolute with a trailing slash, or the local copy of the online docs
    bool m_library_loaded = false;
    bool m_remote         = false; // no repository next to binaries/, the pages come from github
    bool m_download_ran   = false;
    std::shared_ptr<std::atomic<int>> m_download_state = std::make_shared<std::atomic<int>>(0);
    std::vector<std::shared_ptr<doc::page>> m_library;
    std::unordered_map<std::string, std::shared_ptr<doc::page>> m_pages;
    std::shared_ptr<doc::page> m_page;

    std::vector<location> m_back;
    std::vector<location> m_forward;
    float m_scroll         = 0.0f;
    float m_scroll_restore = -1.0f;
    int m_scroll_to_block  = -1;
    int m_flash_block      = -1;
    float m_flash_time     = 0.0f;

    char m_search[256] = {};
    std::string m_search_last;
    std::vector<std::string> m_search_tokens;
    std::vector<doc::search_hit> m_hits;
    bool m_focus_search = false;
};
