/*
Copyright(c) 2016-2019 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES ===============
#include <memory>
#include "IconProvider.h"
#include "ImGui_Extension.h"
#include "Core/FileSystem.h"
//==========================

enum FileDialog_Type
{
	FileDialog_Type_Browser,
	FileDialog_Type_FileSelection
};

enum FileDialog_Operation
{
	FileDialog_Op_Open,
	FileDialog_Op_Load,
	FileDialog_Op_Save
};

enum FileDialog_Filter
{
	FileDialog_Filter_All,
	FileDialog_Filter_Scene,
	FileDialog_Filter_Model
};

class FileDialogDirectory
{
public:
    bool Set(const std::string& directory)
    {
        if (!Spartan::FileSystem::IsDirectory(directory))
            return false;

        backward = current;
        current  = directory;

        tree_path.clear();
        tree_label.clear();
        std::string parent_dir = Spartan::FileSystem::GetParentDirectory(current);
        if (!parent_dir.empty())
        {
            while (!parent_dir.empty())
            {
                tree_path.emplace_back(parent_dir);

                std::string label = parent_dir.substr(0, parent_dir.length() - 1);  // remove last slash
                label = Spartan::FileSystem::GetStringAfterExpression(label, "/");  // Get string after new last slash
                if (!label.empty())
                {
                    tree_label.emplace_back(label);
                }
                else
                {
                    tree_label.emplace_back(parent_dir);
                }

                parent_dir = Spartan::FileSystem::GetParentDirectory(parent_dir);
            }
        }
        else
        {
           tree_path.emplace_back(current);
           tree_label.emplace_back(Spartan::FileSystem::GetStringBeforeExpression(current, "/"));
        }

        return true;
    }

    std::string current;
    std::string backward;
    std::string forward;
    std::vector<std::string> tree_path;
    std::vector<std::string> tree_label;
};

class FileDialogItem
{
public:
	FileDialogItem(const std::string& path, const Thumbnail& thumbnail)
	{
		m_path			= path;
		m_thumbnail		= thumbnail;
		m_id			= Spartan::Spartan_Object::GenerateId();
		m_isDirectory	= Spartan::FileSystem::IsDirectory(path);
		m_label			= Spartan::FileSystem::GetFileNameFromFilePath(path);
	}

	const auto& GetPath()           const { return m_path; }
	const auto& GetLabel()          const { return m_label; }
	auto GetId()                    const { return m_id; }
	auto GetTexture()               const { return IconProvider::Get().GetTextureByThumbnail(m_thumbnail); }
	auto IsDirectory()              const { return m_isDirectory; }
	auto GetTimeSinceLastClickMs()  const { return static_cast<float>(m_time_since_last_click.count()); }

	void Clicked()	
	{
		const auto now			= std::chrono::high_resolution_clock::now();
		m_time_since_last_click	= now - m_last_click_time;
		m_last_click_time		= now;
	}
	
private:
	Thumbnail m_thumbnail;
	unsigned int m_id;
	std::string m_path;
	std::string m_label;
	bool m_isDirectory;
	std::chrono::duration<double, std::milli> m_time_since_last_click;
	std::chrono::time_point<std::chrono::high_resolution_clock> m_last_click_time;
};

class FileDialog
{
public:
	FileDialog(Spartan::Context* context, bool standalone_window, FileDialog_Type type, FileDialog_Operation operation, FileDialog_Filter filter);

	// Type & Filter
	auto GetType()      const { return m_type; }
    auto GetFilter()    const { return m_filter; }

	// Operation
    auto GetOperation() const { return m_operation; }
	void SetOperation(FileDialog_Operation operation);

	// Shows the dialog and returns true if a a selection was made
	bool Show(bool* is_visible, std::string* directory = nullptr, std::string* file_path = nullptr);

	void SetCallbackOnItemClicked(const std::function<void(const std::string&)>& callback)			{ m_callback_on_item_clicked = callback; }
	void SetCallbackOnItemDoubleClicked(const std::function<void(const std::string&)>& callback)	{ m_callback_on_item_double_clicked = callback; }

private:
	void ShowTop(bool* is_visible);
	void ShowMiddle();
	void ShowBottom(bool* is_visible);

	// Item functionality handling
	void ItemDrag(FileDialogItem* item) const;
	void ItemClick(FileDialogItem* item) const;
	void ItemContextMenu(FileDialogItem* item);

	// Misc
    bool DialogUpdateFromDirectory(const std::string& path);
	void EmptyAreaContextMenu();

    // Options
    const bool m_drop_shadow    = true;
    const float m_item_size_min = 50.0f;
    const float m_item_size_max = 200.0f;

    // Flags
	bool m_is_window;
	bool m_selection_made;
	bool m_is_dirty;
    bool m_is_hovering_item;    
    bool m_is_hovering_window;
    std::string m_title;
    FileDialogDirectory m_directory;
    std::string m_input_box;
    std::string m_hovered_item_path;
    uint32_t m_displayed_item_count;

    // Misc
    mutable unsigned int m_context_menu_id;
    mutable ImGuiEx::DragDropPayload m_drag_drop_payload;
    FileDialog_Type m_type;
    FileDialog_Operation m_operation;
    FileDialog_Filter m_filter;
    std::vector<FileDialogItem> m_items;
    Spartan::Math::Vector2 m_item_size;
    ImGuiTextFilter m_search_filter;
	Spartan::Context* m_context;

	// Callbacks
	std::function<void(const std::string&)> m_callback_on_item_clicked;
	std::function<void(const std::string&)> m_callback_on_item_double_clicked;
};
