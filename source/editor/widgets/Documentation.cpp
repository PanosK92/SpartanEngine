/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================
#include "pch.h"
#include "Documentation.h"
#include "../Editor.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Style.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
//==============================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
using namespace doc;
//============================

namespace
{
    namespace fs = std::filesystem;

    const char* online_docs = "https://github.com/PanosK92/SpartanEngine/tree/master/docs";

    float scaled(const float value)
    {
        return ImGui::EditorUi::scaled(value);
    }

    ImU32 to_u32(const ImVec4& color)
    {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    string trim(const string& text)
    {
        const size_t begin = text.find_first_not_of(" \t");
        if (begin == string::npos)
        {
            return "";
        }

        const size_t end = text.find_last_not_of(" \t");
        return text.substr(begin, end - begin + 1);
    }

    string to_lower(string text)
    {
        transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        return text;
    }

    bool starts_with(const string& text, const char* prefix)
    {
        return text.rfind(prefix, 0) == 0;
    }

    string normalize_path(const fs::path& path)
    {
        error_code error;
        fs::path absolute = fs::absolute(path, error);
        return (error ? path : absolute).lexically_normal().generic_string();
    }

    bool is_markdown(const string& path)
    {
        return to_lower(fs::path(path).extension().string()) == ".md";
    }

    bool is_external(const string& url)
    {
        return starts_with(url, "http://") || starts_with(url, "https://") || starts_with(url, "mailto:");
    }

    string read_file(const string& path)
    {
        ifstream file(path, ios::binary);
        stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    string url_decode(const string& text)
    {
        string result;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] == '%' && i + 2 < text.size() && isxdigit(static_cast<unsigned char>(text[i + 1])) && isxdigit(static_cast<unsigned char>(text[i + 2])))
            {
                result += static_cast<char>(stoi(text.substr(i + 1, 2), nullptr, 16));
                i += 2;
            }
            else
            {
                result += text[i];
            }
        }

        return result;
    }

    // github anchors: lowercase, punctuation dropped, spaces become hyphens
    string slugify(const string& text)
    {
        string slug;
        for (const unsigned char c : text)
        {
            if (isalnum(c) || c >= 128 || c == '_')
            {
                slug += static_cast<char>(tolower(c));
            }
            else if (c == ' ' || c == '-')
            {
                slug += '-';
            }
        }

        return slug;
    }

    string spans_text(const spans& content)
    {
        string text;
        for (const span& s : content)
        {
            text += s.text;
        }

        return text;
    }

    // only tags that show up in the repository's markdown are stripped, so a literal <world> stays text
    bool match_html_tag(const string& text, const size_t at, size_t& end)
    {
        static const char* tags[] = { "img", "p", "a", "br", "div", "picture", "source", "sub", "sup", "details", "summary", "b", "i", "em", "strong", "h1", "h2", "h3", "h4", "h5", "h6", "table", "tr", "td", "th", "thead", "tbody", "span", "center", "kbd", "video", "hr" };

        size_t name_begin = at + 1;
        if (name_begin < text.size() && text[name_begin] == '/')
        {
            name_begin++;
        }

        size_t name_end = name_begin;
        while (name_end < text.size() && isalnum(static_cast<unsigned char>(text[name_end])))
        {
            name_end++;
        }

        if (name_end == name_begin)
        {
            return false;
        }

        const string name = to_lower(text.substr(name_begin, name_end - name_begin));
        if (find_if(std::begin(tags), std::end(tags), [&](const char* tag) { return name == tag; }) == std::end(tags))
        {
            return false;
        }

        end = text.find('>', name_end);
        return end != string::npos;
    }

    size_t find_closing(const string& text, const size_t open_at, const char open, const char close)
    {
        int depth = 0;
        for (size_t i = open_at; i < text.size(); i++)
        {
            if (text[i] == '\\')
            {
                i++;
            }
            else if (text[i] == open)
            {
                depth++;
            }
            else if (text[i] == close && --depth == 0)
            {
                return i;
            }
        }

        return string::npos;
    }

    void parse_inline(const string& text, spans& out, const uint8_t style, const string& link)
    {
        static const pair<const char*, const char*> entities[] = { { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&nbsp;", " " }, { "&quot;", "\"" }, { "&#39;", "'" }, { "&mdash;", "-" }, { "&ndash;", "-" } };

        string current;
        auto flush = [&]()
        {
            if (!current.empty())
            {
                out.push_back({ current, link, style });
                current.clear();
            }
        };

        const size_t n = text.size();
        for (size_t i = 0; i < n; i++)
        {
            const char c = text[i];

            if (c == '\\' && i + 1 < n && ispunct(static_cast<unsigned char>(text[i + 1])))
            {
                current += text[++i];
                continue;
            }

            if (c == '`')
            {
                size_t run = 0;
                while (i + run < n && text[i + run] == '`')
                {
                    run++;
                }

                const string fence(run, '`');
                const size_t close = text.find(fence, i + run);
                if (close != string::npos)
                {
                    flush();
                    string code = text.substr(i + run, close - i - run);
                    if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ')
                    {
                        code = code.substr(1, code.size() - 2);
                    }
                    out.push_back({ code, link, static_cast<uint8_t>(style | span_code) });
                    i = close + run - 1;
                    continue;
                }

                current += fence;
                i += run - 1;
                continue;
            }

            if (c == '*' && i + 1 < n && text[i + 1] == '*')
            {
                const size_t close = text.find("**", i + 2);
                if (close != string::npos && close > i + 2)
                {
                    flush();
                    parse_inline(text.substr(i + 2, close - i - 2), out, style | span_bold, link);
                    i = close + 1;
                    continue;
                }
            }
            else if (c == '*' && i + 1 < n && text[i + 1] != ' ')
            {
                const size_t close = text.find('*', i + 1);
                if (close != string::npos)
                {
                    flush();
                    parse_inline(text.substr(i + 1, close - i - 1), out, style | span_italic, link);
                    i = close;
                    continue;
                }
            }

            if (c == '[' || (c == '!' && i + 1 < n && text[i + 1] == '['))
            {
                const bool image       = c == '!';
                const size_t open      = image ? i + 1 : i;
                const size_t label_end = find_closing(text, open, '[', ']');
                if (label_end != string::npos && label_end + 1 < n && text[label_end + 1] == '(')
                {
                    const size_t url_end = find_closing(text, label_end + 1, '(', ')');
                    if (url_end != string::npos)
                    {
                        flush();
                        string url = trim(text.substr(label_end + 2, url_end - label_end - 2));
                        const size_t title = url.find(' ');
                        if (title != string::npos)
                        {
                            url = url.substr(0, title);
                        }
                        if (url.size() >= 2 && url.front() == '<' && url.back() == '>')
                        {
                            url = url.substr(1, url.size() - 2);
                        }

                        string label = text.substr(open + 1, label_end - open - 1);
                        if (image && label.empty())
                        {
                            label = "image";
                        }
                        parse_inline(label, out, style | (image ? span_italic : 0), url);
                        i = url_end;
                        continue;
                    }
                }
            }

            if (c == '<')
            {
                const size_t close = text.find('>', i);
                if (close != string::npos)
                {
                    const string inner = text.substr(i + 1, close - i - 1);
                    if (is_external(inner))
                    {
                        flush();
                        out.push_back({ inner, inner, style });
                        i = close;
                        continue;
                    }
                }

                size_t tag_end = 0;
                if (match_html_tag(text, i, tag_end))
                {
                    i = tag_end;
                    continue;
                }
            }

            if (c == 'h' && link.empty() && (i == 0 || text[i - 1] == ' ' || text[i - 1] == '(') && (text.compare(i, 8, "https://") == 0 || text.compare(i, 7, "http://") == 0))
            {
                size_t end = text.find_first_of(" )", i);
                end        = end == string::npos ? n : end;
                while (end > i && (text[end - 1] == '.' || text[end - 1] == ','))
                {
                    end--;
                }

                flush();
                const string url = text.substr(i, end - i);
                out.push_back({ url, url, style });
                i = end - 1;
                continue;
            }

            if (c == '&')
            {
                bool replaced = false;
                for (const auto& [entity, replacement] : entities)
                {
                    const size_t length = strlen(entity);
                    if (text.compare(i, length, entity) == 0)
                    {
                        current += replacement;
                        i       += length - 1;
                        replaced = true;
                        break;
                    }
                }

                if (replaced)
                {
                    continue;
                }
            }

            current += c;
        }

        flush();
    }

    vector<string> split_table_cells(const string& line)
    {
        string row = line;
        if (!row.empty() && row.front() == '|')
        {
            row.erase(0, 1);
        }
        if (row.size() >= 2 && row.back() == '|' && row[row.size() - 2] != '\\')
        {
            row.pop_back();
        }

        vector<string> cells;
        string cell;
        bool in_code = false;
        for (size_t i = 0; i < row.size(); i++)
        {
            const char c = row[i];
            if (c == '\\' && i + 1 < row.size() && row[i + 1] == '|')
            {
                cell += in_code ? "|" : "\\|";
                i++;
                continue;
            }

            if (c == '`')
            {
                in_code = !in_code;
            }

            if (c == '|' && !in_code)
            {
                cells.push_back(trim(cell));
                cell.clear();
                continue;
            }

            cell += c;
        }
        cells.push_back(trim(cell));

        return cells;
    }

    bool is_table_separator(const vector<string>& cells)
    {
        for (const string& cell : cells)
        {
            if (cell.empty() || cell.find_first_not_of("-: ") != string::npos)
            {
                return false;
            }
        }

        return true;
    }

    bool is_rule(const string& line)
    {
        if (line.size() < 3 || (line[0] != '-' && line[0] != '*' && line[0] != '_'))
        {
            return false;
        }

        return line.find_first_not_of(string(1, line[0]) + " ") == string::npos;
    }

    shared_ptr<page> parse_page(const string& path, const string& source)
    {
        shared_ptr<page> result = make_shared<page>();
        result->path            = path;
        vector<block>& blocks   = result->blocks;
        unordered_map<string, int> slug_counts;

        string pending;
        block_type pending_type = block_type::paragraph;
        int pending_level       = 0;
        string pending_marker;
        auto flush = [&]()
        {
            if (!pending.empty())
            {
                block b;
                b.type   = pending_type;
                b.level  = pending_level;
                b.marker = pending_marker;
                parse_inline(pending, b.content, 0, "");
                blocks.push_back(move(b));
            }
            pending.clear();
            pending_type  = block_type::paragraph;
            pending_level = 0;
            pending_marker.clear();
        };

        vector<string> table_lines;
        auto flush_table = [&]()
        {
            if (table_lines.empty())
            {
                return;
            }

            block b;
            b.type = block_type::table;
            for (const string& line : table_lines)
            {
                const vector<string> cells = split_table_cells(line);
                if (is_table_separator(cells))
                {
                    continue;
                }

                vector<spans> row;
                for (const string& cell : cells)
                {
                    spans content;
                    parse_inline(cell, content, 0, "");
                    row.push_back(move(content));
                }
                b.rows.push_back(move(row));
            }
            table_lines.clear();
            blocks.push_back(move(b));
        };

        istringstream stream(source);
        string line;
        bool in_code       = false;
        size_t code_indent = 0;
        block code;
        while (getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            size_t tab = 0;
            while ((tab = line.find('\t', tab)) != string::npos)
            {
                line.replace(tab, 1, "    ");
            }

            const string t      = trim(line);
            const size_t indent = min(line.find_first_not_of(' '), line.size());

            if (in_code)
            {
                if (starts_with(t, "```"))
                {
                    blocks.push_back(move(code));
                    code    = block();
                    in_code = false;
                }
                else
                {
                    code.lines.push_back(line.substr(min(code_indent, indent)));
                }
                continue;
            }

            if (!table_lines.empty() && !starts_with(t, "|"))
            {
                flush_table();
            }

            if (starts_with(t, "```"))
            {
                flush();
                in_code     = true;
                code        = block();
                code.type   = block_type::code;
                code.marker = trim(t.substr(3));
                code_indent = indent;
                continue;
            }

            if (t.empty())
            {
                flush();
                continue;
            }

            if (t[0] == '#')
            {
                const size_t hashes = t.find_first_not_of('#');
                if (hashes != string::npos && hashes <= 6 && t[hashes] == ' ')
                {
                    flush();
                    string title = trim(t.substr(hashes));
                    while (!title.empty() && title.back() == '#')
                    {
                        title.pop_back();
                    }

                    block b;
                    b.type  = block_type::heading;
                    b.level = static_cast<int>(hashes);
                    parse_inline(trim(title), b.content, 0, "");
                    b.anchor  = slugify(to_lower(spans_text(b.content)));
                    int& seen = slug_counts[b.anchor];
                    if (seen++ > 0)
                    {
                        b.anchor += "-" + to_string(seen - 1);
                    }
                    blocks.push_back(move(b));
                    continue;
                }
            }

            if (is_rule(t))
            {
                flush();
                block b;
                b.type = block_type::rule;
                blocks.push_back(move(b));
                continue;
            }

            if (t[0] == '|')
            {
                flush();
                table_lines.push_back(t);
                continue;
            }

            if (t[0] == '>')
            {
                const string quote = trim(t.substr(1));
                if (pending_type != block_type::quote)
                {
                    flush();
                    pending_type = block_type::quote;
                }
                pending += pending.empty() ? quote : " " + quote;
                continue;
            }

            // list items, bullets or numbers, nested by indentation
            {
                string item;
                string marker;
                if ((t[0] == '-' || t[0] == '*' || t[0] == '+') && t.size() > 1 && t[1] == ' ')
                {
                    item = t.substr(2);
                }
                else
                {
                    const size_t digits = t.find_first_not_of("0123456789");
                    if (digits != string::npos && digits > 0 && digits + 1 < t.size() && (t[digits] == '.' || t[digits] == ')') && t[digits + 1] == ' ')
                    {
                        marker = t.substr(0, digits) + ".";
                        item   = t.substr(digits + 2);
                    }
                }

                if (!item.empty() || !marker.empty())
                {
                    flush();
                    pending_type   = block_type::list_item;
                    pending_level  = min(static_cast<int>(indent / 2), 4);
                    pending_marker = marker;
                    pending        = trim(item);
                    continue;
                }
            }

            // lines that are only html, such as a centered logo, carry no text
            if (t[0] == '<')
            {
                spans content;
                parse_inline(t, content, 0, "");
                if (trim(spans_text(content)).empty())
                {
                    continue;
                }
            }

            if (pending_type == block_type::quote)
            {
                flush();
            }
            pending += pending.empty() ? t : " " + t;
        }

        flush();
        flush_table();
        if (in_code)
        {
            blocks.push_back(move(code));
        }

        for (block& b : blocks)
        {
            if (b.type == block_type::code)
            {
                for (const string& code_line : b.lines)
                {
                    b.text += code_line + " ";
                }
            }
            else if (b.type == block_type::table)
            {
                for (const vector<spans>& row : b.rows)
                {
                    for (const spans& cell : row)
                    {
                        b.text += spans_text(cell) + " | ";
                    }
                }
            }
            else
            {
                b.text = spans_text(b.content);
            }
            b.text_lower = to_lower(b.text);

            if (result->title.empty() && b.type == block_type::heading && b.level == 1)
            {
                result->title = trim(b.text);
            }
        }

        if (result->title.empty())
        {
            result->title = fs::path(path).filename().string();
        }

        return result;
    }

    struct draw_context
    {
        const vector<string>* tokens = nullptr;
        string clicked;
        float max_width = FLT_MAX;
    };

    ImFont* font_regular()
    {
        return ImGui::GetFont();
    }

    ImFont* font_strong()
    {
        return Editor::font_bold ? Editor::font_bold : ImGui::GetFont();
    }

    ImFont* font_code()
    {
        return Editor::font_mono ? Editor::font_mono : ImGui::GetFont();
    }

    void draw_highlights(ImDrawList* draw_list, ImFont* font, const float size, const char* begin, const char* end, const ImVec2& position, const vector<string>& tokens)
    {
        const string lower = to_lower(string(begin, end));
        const ImU32 color  = to_u32(ImGui::Style::with_alpha(ImGui::Style::color_warning, 0.38f));
        for (const string& token : tokens)
        {
            size_t at = lower.find(token);
            while (at != string::npos)
            {
                const float x0 = font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, begin + at).x;
                const float x1 = font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, begin + at + token.size()).x;
                draw_list->AddRectFilled(ImVec2(position.x + x0, position.y), ImVec2(position.x + x1, position.y + size), color, scaled(2.0f));
                at = lower.find(token, at + token.size());
            }
        }
    }

    // word wrapped rich text, wraps between words and keeps a word's styled pieces together
    void draw_spans(const spans& content, const float scale, const bool strong, const ImVec4& color, draw_context& ctx)
    {
        struct piece
        {
            const span* source;
            size_t begin;
            size_t end;
            float width;
            ImFont* font;
            float size;
        };

        struct word
        {
            size_t first     = 0;
            size_t count     = 0;
            float width      = 0.0f;
            bool space_after = false;
        };

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const float base      = ImGui::GetFontSize() * scale;
        ImFont* font_text     = strong ? font_strong() : font_regular();
        const float code_size = base * 0.9f;
        const float code_pad  = scaled(3.0f);
        const float line_h    = base * (scale > 1.0f ? 1.3f : 1.5f);
        const float space_w   = font_text->CalcTextSizeA(base, FLT_MAX, 0.0f, " ").x;
        const float wrap      = max(scaled(40.0f), min(ImGui::GetContentRegionAvail().x, ctx.max_width));

        vector<piece> pieces;
        vector<word> words;
        word current;
        auto end_word = [&](const bool space)
        {
            if (current.count > 0)
            {
                current.space_after = space;
                words.push_back(current);
            }
            else if (space && !words.empty())
            {
                words.back().space_after = true;
            }
            current       = word();
            current.first = pieces.size();
        };

        for (const span& s : content)
        {
            const bool code  = s.style & span_code;
            ImFont* font     = code ? font_code() : ((s.style & span_bold) ? font_strong() : font_text);
            const float size = code ? code_size : base;
            size_t p         = 0;
            while (p < s.text.size())
            {
                if (s.text[p] == ' ')
                {
                    end_word(true);
                    p++;
                    continue;
                }

                size_t q = s.text.find(' ', p);
                q        = q == string::npos ? s.text.size() : q;
                const float width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.text.data() + p, s.text.data() + q).x + (code ? code_pad * 2.0f : 0.0f);
                pieces.push_back({ &s, p, q, width, font, size });
                current.count++;
                current.width += width;
                p = q;
            }
        }
        end_word(false);

        const ImU32 color_text      = to_u32(color);
        const ImU32 color_link      = to_u32(ImGui::Style::color_accent_1);
        const ImU32 color_link_over = to_u32(ImGui::Style::color_accent_hi);
        const ImU32 color_code_text = to_u32(ImGui::Style::lerp(color, ImGui::Style::color_warning, 0.35f));
        const ImU32 color_code_bg   = to_u32(ImGui::Style::color_surface_active);
        const ImU32 color_emphasis  = to_u32(ImGui::Style::color_info);
        const bool window_hovered   = ImGui::IsWindowHovered();

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float x     = 0.0f;
        float y     = 0.0f;
        float max_x = 0.0f;
        for (const word& w : words)
        {
            if (x > 0.0f && x + w.width > wrap)
            {
                x  = 0.0f;
                y += line_h;
            }

            for (size_t k = w.first; k < w.first + w.count; k++)
            {
                const piece& pc     = pieces[k];
                const span& s       = *pc.source;
                const bool code     = s.style & span_code;
                const ImVec2 cell_min(origin.x + x, origin.y + y);
                const ImVec2 cell_max(origin.x + x + pc.width, origin.y + y + line_h);
                const ImVec2 text_pos(cell_min.x + (code ? code_pad : 0.0f), cell_min.y + (line_h - pc.size) * 0.5f);
                const char* text_begin = s.text.data() + pc.begin;
                const char* text_end   = s.text.data() + pc.end;

                if (code)
                {
                    draw_list->AddRectFilled(ImVec2(cell_min.x, text_pos.y - scaled(1.5f)), ImVec2(cell_max.x, text_pos.y + pc.size + scaled(1.5f)), color_code_bg, scaled(3.0f));
                }

                if (ctx.tokens)
                {
                    draw_highlights(draw_list, pc.font, pc.size, text_begin, text_end, text_pos, *ctx.tokens);
                }

                ImU32 piece_color = code ? color_code_text : ((s.style & span_italic) ? color_emphasis : color_text);
                bool hovered      = false;
                if (!s.link.empty())
                {
                    hovered     = window_hovered && ImGui::IsMouseHoveringRect(cell_min, cell_max);
                    piece_color = hovered ? color_link_over : color_link;
                    if (hovered)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        ImGui::SetTooltip("%s", s.link.c_str());
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            ctx.clicked = s.link;
                        }
                    }
                }

                draw_list->AddText(pc.font, pc.size, text_pos, piece_color, text_begin, text_end);
                if (hovered)
                {
                    const float underline_y = text_pos.y + pc.size;
                    draw_list->AddLine(ImVec2(text_pos.x, underline_y), ImVec2(cell_max.x - (code ? code_pad : 0.0f), underline_y), piece_color, scaled(1.0f));
                }

                x += pc.width;
            }

            max_x = max(max_x, x);
            if (w.space_after)
            {
                x += space_w;
            }
        }

        const float height = words.empty() ? line_h : y + line_h;
        ImGui::Dummy(ImVec2(max(1.0f, min(max_x, wrap)), height));
    }

    void draw_code(const block& b, draw_context& ctx)
    {
        ImFont* font     = font_code();
        const float size = ImGui::GetFontSize() * 0.92f;
        const float lh   = size * 1.45f;
        const float pad  = scaled(10.0f);

        float text_w = 0.0f;
        for (const string& line : b.lines)
        {
            text_w = max(text_w, font->CalcTextSizeA(size, FLT_MAX, 0.0f, line.c_str()).x);
        }

        const float width     = min(ImGui::GetContentRegionAvail().x, ctx.max_width);
        const float content_h = b.lines.size() * lh + pad * 2.0f;
        const bool overflow   = text_w + pad * 2.0f > width;
        const float height    = content_h + (overflow ? ImGui::GetStyle().ScrollbarSize : 0.0f);

        ImGui::PushID(&b);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::Style::color_canvas_deep);
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::Style::color_border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, scaled(4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginChild("##code", ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const ImVec2 origin   = ImGui::GetCursorScreenPos();
            const ImU32 color     = to_u32(ImGui::Style::color_text);
            for (size_t i = 0; i < b.lines.size(); i++)
            {
                const string& line = b.lines[i];
                const ImVec2 position(origin.x + pad, origin.y + pad + i * lh + (lh - size) * 0.5f);
                if (ctx.tokens)
                {
                    draw_highlights(draw_list, font, size, line.data(), line.data() + line.size(), position, *ctx.tokens);
                }
                draw_list->AddText(font, size, position, color, line.data(), line.data() + line.size());
            }
            ImGui::Dummy(ImVec2(text_w + pad * 2.0f, content_h));

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            {
                const float button_w = ImGui::CalcTextSize("Copy").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - button_w - scaled(6.0f), ImGui::GetWindowPos().y + scaled(6.0f)));
                if (ImGui::SmallButton("Copy"))
                {
                    string joined;
                    for (const string& line : b.lines)
                    {
                        joined += line + "\n";
                    }
                    ImGui::SetClipboardText(joined.c_str());
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    void draw_table(const block& b, draw_context& ctx)
    {
        size_t columns = 0;
        for (const vector<spans>& row : b.rows)
        {
            columns = max(columns, row.size());
        }

        if (columns == 0)
        {
            return;
        }

        const float width   = min(ImGui::GetContentRegionAvail().x, ctx.max_width);
        const int flags     = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
        ImGui::PushID(&b);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(scaled(8.0f), scaled(4.0f)));
        if (ImGui::BeginTable("##table", static_cast<int>(columns), flags, ImVec2(width, 0.0f)))
        {
            // columns share the width by how much text they carry, so a short key column stays narrow
            for (size_t c = 0; c < columns; c++)
            {
                float length = 0.0f;
                for (const vector<spans>& row : b.rows)
                {
                    length += c < row.size() ? static_cast<float>(spans_text(row[c]).size()) : 0.0f;
                }
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, clamp(length / b.rows.size(), 6.0f, 70.0f));
            }

            for (size_t r = 0; r < b.rows.size(); r++)
            {
                const bool header = r == 0;
                ImGui::TableNextRow(header ? ImGuiTableRowFlags_Headers : ImGuiTableRowFlags_None);
                if (header)
                {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, to_u32(ImGui::Style::color_surface_hover));
                }

                for (size_t c = 0; c < b.rows[r].size() && c < columns; c++)
                {
                    ImGui::TableSetColumnIndex(static_cast<int>(c));
                    draw_spans(b.rows[r][c], 1.0f, header, ImGui::Style::color_text, ctx);
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
    }
}

Documentation::Documentation(Editor* editor) : Widget(editor)
{
    m_title        = "Documentation";
    m_visible      = false;
    m_flags       |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    m_size_initial = Vector2(1200, 800);
    m_size_min     = Vector2(640, 400);
}

void Documentation::OnTick()
{
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F1, false))
    {
        m_visible = !m_visible;
    }
}

void Documentation::Open(const string& path, const string& anchor)
{
    if (!m_library_loaded)
    {
        load_library();
    }

    const fs::path target(path);
    navigate(target.is_absolute() ? path : m_root + path, anchor, true);
    m_visible = true;
}

void Documentation::load_library()
{
    m_library_loaded = true;
    m_library.clear();
    m_pages.clear();
    m_root.clear();

    // the engine runs from binaries/, the docs ship with the repository one level up
    for (const char* candidate : { "../", "./", "../../" })
    {
        if (fs::exists(fs::path(candidate) / "docs" / "README.md"))
        {
            m_root = normalize_path(candidate);
            if (m_root.empty() || m_root.back() != '/')
            {
                m_root += '/';
            }
            break;
        }
    }

    if (m_root.empty())
    {
        return;
    }

    auto add = [this](const string& path)
    {
        shared_ptr<page> p = get_page(path);
        if (p && find(m_library.begin(), m_library.end(), p) == m_library.end())
        {
            m_library.push_back(p);
        }
        return p;
    };

    // the index decides the reading order, every page it links to follows it
    const string docs = m_root + "docs/";
    if (shared_ptr<page> index = add(docs + "README.md"))
    {
        auto add_links = [&](const spans& content)
        {
            for (const span& s : content)
            {
                if (s.link.empty() || is_external(s.link))
                {
                    continue;
                }

                const string target = s.link.substr(0, s.link.find('#'));
                if (is_markdown(target))
                {
                    add(normalize_path(fs::path(docs) / url_decode(target)));
                }
            }
        };

        for (const block& b : index->blocks)
        {
            add_links(b.content);
            for (const vector<spans>& row : b.rows)
            {
                for (const spans& cell : row)
                {
                    add_links(cell);
                }
            }
        }
    }

    vector<string> remaining;
    error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(docs, error))
    {
        if (entry.is_regular_file() && is_markdown(entry.path().string()))
        {
            remaining.push_back(normalize_path(entry.path()));
        }
    }
    sort(remaining.begin(), remaining.end());
    for (const string& path : remaining)
    {
        add(path);
    }

    if (fs::exists(m_root + "readme.md"))
    {
        add(normalize_path(m_root + "readme.md"));
    }
}

shared_ptr<page> Documentation::get_page(const string& path)
{
    const string full = normalize_path(path);
    const string key  = to_lower(full);
    auto it = m_pages.find(key);
    if (it != m_pages.end())
    {
        return it->second;
    }

    if (!fs::is_regular_file(full))
    {
        return nullptr;
    }

    shared_ptr<page> result = parse_page(full, read_file(full));
    m_pages[key]            = result;
    return result;
}

void Documentation::navigate(const string& path, const string& anchor, const bool push_history, const int block)
{
    shared_ptr<page> target = get_page(path);
    if (!target)
    {
        SP_LOG_WARNING("Documentation page not found: %s", path.c_str());
        return;
    }

    if (push_history && m_page)
    {
        m_back.push_back({ m_page->path, m_scroll });
        m_forward.clear();
    }

    if (target != m_page)
    {
        m_scroll_restore = 0.0f;
    }

    m_page            = target;
    m_scroll_to_block = block;
    if (!anchor.empty())
    {
        const string wanted = to_lower(url_decode(anchor));
        for (size_t i = 0; i < m_page->blocks.size(); i++)
        {
            if (m_page->blocks[i].type == block_type::heading && m_page->blocks[i].anchor == wanted)
            {
                m_scroll_to_block = static_cast<int>(i);
                break;
            }
        }
    }

    if (m_scroll_to_block >= 0)
    {
        m_scroll_restore = -1.0f;
        m_flash_block    = m_scroll_to_block;
        m_flash_time     = 1.5f;
    }
}

void Documentation::go_back()
{
    if (m_back.empty())
    {
        return;
    }

    const location previous = m_back.back();
    m_back.pop_back();
    if (m_page)
    {
        m_forward.push_back({ m_page->path, m_scroll });
    }
    navigate(previous.path, "", false);
    m_scroll_restore = previous.scroll;
}

void Documentation::go_forward()
{
    if (m_forward.empty())
    {
        return;
    }

    const location next = m_forward.back();
    m_forward.pop_back();
    if (m_page)
    {
        m_back.push_back({ m_page->path, m_scroll });
    }
    navigate(next.path, "", false);
    m_scroll_restore = next.scroll;
}

void Documentation::follow_link(const string& url)
{
    if (is_external(url))
    {
        FileSystem::OpenUrl(url);
        return;
    }

    const size_t hash   = url.find('#');
    string target       = url_decode(url.substr(0, hash));
    const string anchor = hash == string::npos ? "" : url.substr(hash + 1);
    if (target.empty())
    {
        if (m_page)
        {
            navigate(m_page->path, anchor, true);
        }
        return;
    }

    fs::path base = m_page ? fs::path(m_page->path).parent_path() : fs::path(m_root);
    if (target[0] == '/')
    {
        base   = fs::path(m_root);
        target = target.substr(1);
    }

    string full = normalize_path(base / target);
    if (fs::is_directory(full) && fs::exists(fs::path(full) / "README.md"))
    {
        full = normalize_path(fs::path(full) / "README.md");
    }

    if (is_markdown(full) && fs::is_regular_file(full))
    {
        navigate(full, anchor, true);
    }
    else if (fs::exists(full))
    {
        FileSystem::OpenUrl(full);
    }
    else
    {
        SP_LOG_WARNING("Documentation link target not found: %s", full.c_str());
    }
}

void Documentation::run_search()
{
    m_search_last = m_search;
    m_hits.clear();
    m_search_tokens.clear();

    istringstream stream(to_lower(m_search_last));
    string token;
    while (stream >> token)
    {
        m_search_tokens.push_back(token);
    }

    if (m_search_tokens.empty())
    {
        return;
    }

    for (const shared_ptr<page>& p : m_library)
    {
        string section;
        for (size_t i = 0; i < p->blocks.size(); i++)
        {
            const block& b = p->blocks[i];
            if (b.type == block_type::heading)
            {
                section = b.text;
            }

            bool match = true;
            for (const string& t : m_search_tokens)
            {
                if (b.text_lower.find(t) == string::npos)
                {
                    match = false;
                    break;
                }
            }

            if (!match)
            {
                continue;
            }

            const size_t at = b.text_lower.find(m_search_tokens[0]);
            size_t start    = at > 50 ? at - 50 : 0;
            if (start > 0)
            {
                const size_t space = b.text.find(' ', start);
                start              = space != string::npos && space < at ? space + 1 : start;
            }
            const size_t end = min(b.text.size(), start + 150);
            string snippet   = (start > 0 ? "..." : "") + b.text.substr(start, end - start) + (end < b.text.size() ? "..." : "");
            replace(snippet.begin(), snippet.end(), '\n', ' ');

            m_hits.push_back({ p, static_cast<int>(i), b.type == block_type::heading ? "" : section, snippet });
        }
    }

    stable_sort(m_hits.begin(), m_hits.end(), [](const search_hit& a, const search_hit& b)
    {
        const bool a_heading = a.target->blocks[a.block].type == block_type::heading;
        const bool b_heading = b.target->blocks[b.block].type == block_type::heading;
        return a_heading && !b_heading;
    });
}

void Documentation::draw_toolbar()
{
    ImGui::BeginDisabled(m_back.empty());
    if (ImGui::ArrowButton("##doc_back", ImGuiDir_Left))
    {
        go_back();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Back (Alt+Left or the mouse back button)");

    ImGui::SameLine();
    ImGui::BeginDisabled(m_forward.empty());
    if (ImGui::ArrowButton("##doc_forward", ImGuiDir_Right))
    {
        go_forward();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Forward (Alt+Right)");

    ImGui::SameLine();
    if (ImGui::Button("Home") && !m_library.empty())
    {
        navigate(m_library[0]->path, "", true);
    }

    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        const string current = m_page ? m_page->path : "";
        m_page = nullptr;
        load_library();
        if (!current.empty())
        {
            navigate(current, "", false);
        }
        m_scroll_restore = m_scroll;
        run_search();
    }
    ImGui::SetItemTooltip("Read the markdown files again, for edits made while the editor runs");

    ImGui::SameLine();
    if (ImGui::Button("Open online"))
    {
        FileSystem::OpenUrl(online_docs);
    }

    if (m_page)
    {
        ImGui::SameLine();
        const string relative = starts_with(m_page->path, m_root.c_str()) ? m_page->path.substr(m_root.size()) : m_page->path;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::Style::color_text_faint, "%s", relative.c_str());
    }
}

void Documentation::draw_sidebar()
{
    ImGui::BeginChild("##doc_sidebar", ImVec2(scaled(280.0f), 0.0f), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);

    if (m_focus_search)
    {
        ImGui::SetKeyboardFocusHere();
        m_focus_search = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##doc_search", "Search (Ctrl+F)", m_search, sizeof(m_search));
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        m_search[0] = 0;
    }
    if (m_search_last != m_search)
    {
        run_search();
    }
    ImGui::Dummy(ImVec2(0.0f, scaled(4.0f)));

    ImGui::BeginChild("##doc_sidebar_list");
    if (!m_search_tokens.empty())
    {
        ImGui::TextColored(ImGui::Style::color_text_muted, m_hits.empty() ? "No results" : "%d results", static_cast<int>(m_hits.size()));

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const float line      = ImGui::GetTextLineHeight();
        const size_t shown    = min<size_t>(m_hits.size(), 300);
        for (size_t i = 0; i < shown; i++)
        {
            const search_hit& hit = m_hits[i];
            ImGui::PushID(static_cast<int>(i));
            const bool selected = hit.target == m_page && hit.block == m_flash_block;
            if (ImGui::Selectable("##hit", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, line * 2.0f + scaled(6.0f))))
            {
                navigate(hit.target->path, "", true, hit.block);
            }

            const ImVec2 rect_min = ImGui::GetItemRectMin();
            const ImVec2 rect_max = ImGui::GetItemRectMax();
            const string title    = hit.section.empty() ? hit.target->title : hit.target->title + "  >  " + hit.section;
            draw_list->PushClipRect(rect_min, rect_max, true);
            draw_list->AddText(font_strong(), ImGui::GetFontSize(), ImVec2(rect_min.x + scaled(4.0f), rect_min.y + scaled(2.0f)), to_u32(ImGui::Style::color_text), title.c_str());
            draw_list->AddText(font_regular(), ImGui::GetFontSize(), ImVec2(rect_min.x + scaled(4.0f), rect_min.y + scaled(4.0f) + line), to_u32(ImGui::Style::color_text_muted), hit.snippet.c_str());
            draw_list->PopClipRect();
            ImGui::PopID();
        }
    }
    else
    {
        for (size_t p = 0; p < m_library.size(); p++)
        {
            const shared_ptr<page>& entry = m_library[p];
            const bool current            = entry == m_page;
            ImGui::PushID(static_cast<int>(p));
            if (ImGui::Selectable(entry->title.c_str(), current))
            {
                navigate(entry->path, "", true);
            }

            // the open page lists its sections, a click scrolls to one
            if (current)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
                for (size_t i = 0; i < entry->blocks.size(); i++)
                {
                    const block& b = entry->blocks[i];
                    if (b.type != block_type::heading || b.level < 2 || b.level > 3)
                    {
                        continue;
                    }

                    const float indent = scaled(b.level == 2 ? 12.0f : 24.0f);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Indent(indent);
                    if (ImGui::Selectable(b.text.c_str(), m_flash_block == static_cast<int>(i)))
                    {
                        navigate(entry->path, "", false, static_cast<int>(i));
                    }
                    ImGui::Unindent(indent);
                    ImGui::PopID();
                }
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

void Documentation::draw_page()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(scaled(28.0f), scaled(20.0f)));
    ImGui::BeginChild("##doc_page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();

    if (!m_page)
    {
        ImGui::EndChild();
        return;
    }

    if (m_scroll_restore >= 0.0f)
    {
        ImGui::SetScrollY(m_scroll_restore);
        m_scroll_restore = -1.0f;
    }
    m_scroll = ImGui::GetScrollY();

    draw_context ctx;
    ctx.tokens    = m_search_tokens.empty() ? nullptr : &m_search_tokens;
    ctx.max_width = scaled(920.0f);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float base      = ImGui::GetFontSize();
    const float gap       = base * 0.6f;
    const ImVec4 color    = ImGui::Style::color_text;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

    vector<block>& blocks = m_page->blocks;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        block& b = blocks[i];

        if (b.type == block_type::heading && i > 0)
        {
            ImGui::Dummy(ImVec2(0.0f, base * (b.level <= 2 ? 1.1f : 0.5f)));
        }

        if (static_cast<int>(i) == m_scroll_to_block)
        {
            ImGui::SetScrollHereY(0.0f);
            m_scroll_to_block = -1;
        }

        const ImVec2 start  = ImGui::GetCursorScreenPos();
        const float width   = min(ImGui::GetContentRegionAvail().x, ctx.max_width);
        const bool measured = b.height >= 0.0f && b.height_width == width;
        if (measured && !ImGui::IsRectVisible(ImVec2(width, b.height)))
        {
            ImGui::Dummy(ImVec2(width, b.height));
        }
        else
        {
            switch (b.type)
            {
                case block_type::heading:
                {
                    static const float scales[] = { 1.9f, 1.45f, 1.2f, 1.05f, 1.0f, 1.0f };
                    draw_spans(b.content, scales[clamp(b.level, 1, 6) - 1], true, ImGui::Style::h_color_1, ctx);
                    if (b.level <= 2)
                    {
                        const float y = ImGui::GetCursorScreenPos().y + base * 0.25f;
                        draw_list->AddLine(ImVec2(start.x, y), ImVec2(start.x + width, y), to_u32(ImGui::Style::color_border), scaled(1.0f));
                        ImGui::Dummy(ImVec2(0.0f, base * 0.5f));
                    }
                    break;
                }
                case block_type::paragraph:
                {
                    draw_spans(b.content, 1.0f, false, color, ctx);
                    break;
                }
                case block_type::list_item:
                {
                    const float indent = base * (1.4f + 1.3f * b.level);
                    ImGui::Indent(indent);
                    const ImVec2 position = ImGui::GetCursorScreenPos();
                    const float line_h    = base * 1.5f;
                    const ImU32 marker    = to_u32(ImGui::Style::color_text_muted);
                    if (b.marker.empty())
                    {
                        const ImVec2 center(position.x - base * 0.7f, position.y + line_h * 0.5f);
                        if (b.level == 0)
                        {
                            draw_list->AddCircleFilled(center, base * 0.16f, marker);
                        }
                        else
                        {
                            draw_list->AddCircle(center, base * 0.16f, marker, 0, scaled(1.0f));
                        }
                    }
                    else
                    {
                        const float marker_w = ImGui::CalcTextSize(b.marker.c_str()).x;
                        draw_list->AddText(ImVec2(position.x - base * 0.4f - marker_w, position.y + (line_h - base) * 0.5f), marker, b.marker.c_str());
                    }
                    draw_spans(b.content, 1.0f, false, color, ctx);
                    ImGui::Unindent(indent);
                    break;
                }
                case block_type::quote:
                {
                    ImGui::Indent(base);
                    draw_spans(b.content, 1.0f, false, ImGui::Style::color_text_muted, ctx);
                    ImGui::Unindent(base);
                    draw_list->AddRectFilled(ImVec2(start.x + base * 0.2f, start.y), ImVec2(start.x + base * 0.2f + scaled(3.0f), ImGui::GetCursorScreenPos().y), to_u32(ImGui::Style::color_accent_line));
                    break;
                }
                case block_type::code:
                {
                    draw_code(b, ctx);
                    break;
                }
                case block_type::table:
                {
                    draw_table(b, ctx);
                    break;
                }
                case block_type::rule:
                {
                    ImGui::Dummy(ImVec2(0.0f, base * 0.5f));
                    const float y = ImGui::GetCursorScreenPos().y;
                    draw_list->AddLine(ImVec2(start.x, y), ImVec2(start.x + width, y), to_u32(ImGui::Style::color_border), scaled(1.0f));
                    ImGui::Dummy(ImVec2(0.0f, base * 0.5f));
                    break;
                }
            }
        }

        const float end_y = ImGui::GetCursorScreenPos().y;
        b.height          = end_y - start.y;
        b.height_width    = width;

        if (static_cast<int>(i) == m_flash_block && m_flash_time > 0.0f)
        {
            const float alpha = 0.14f * min(1.0f, m_flash_time);
            draw_list->AddRectFilled(ImVec2(start.x - scaled(8.0f), start.y - scaled(4.0f)), ImVec2(start.x + width + scaled(8.0f), end_y + scaled(4.0f)), to_u32(ImGui::Style::with_alpha(ImGui::Style::color_accent_1, alpha)), scaled(4.0f));
        }

        // consecutive list items sit close together, everything else gets a paragraph gap
        const bool tight = b.type == block_type::list_item && i + 1 < blocks.size() && blocks[i + 1].type == block_type::list_item;
        ImGui::Dummy(ImVec2(0.0f, tight ? base * 0.15f : gap));
    }

    ImGui::Dummy(ImVec2(0.0f, base * 4.0f));
    ImGui::PopStyleVar();
    ImGui::EndChild();

    if (!ctx.clicked.empty())
    {
        follow_link(ctx.clicked);
    }
}

void Documentation::OnTickVisible()
{
    if (!m_library_loaded)
    {
        load_library();
    }

    if (!m_page && !m_library.empty())
    {
        navigate(m_library[0]->path, "", false);
    }

    if (m_root.empty())
    {
        ImGui::TextWrapped("The documentation was not found. It ships with the repository in docs/, next to binaries/.");
        if (ImGui::Button("Open the documentation online"))
        {
            FileSystem::OpenUrl(online_docs);
        }
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            m_focus_search = true;
        }
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
        {
            go_back();
        }
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
        {
            go_forward();
        }
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
    {
        if (ImGui::IsMouseClicked(3))
        {
            go_back();
        }
        if (ImGui::IsMouseClicked(4))
        {
            go_forward();
        }
    }

    draw_toolbar();
    ImGui::Dummy(ImVec2(0.0f, scaled(2.0f)));
    draw_sidebar();
    ImGui::SameLine();
    draw_page();

    m_flash_time = max(0.0f, m_flash_time - io.DeltaTime);
}
