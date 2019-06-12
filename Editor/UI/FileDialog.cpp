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

//= INCLUDES ==============================
#include "FileDialog.h"
#include "ImGui_Extension.h"
#include "../ImGui/Source/imgui.h"
#include "../ImGui/Source/imgui_internal.h"
#include "../ImGui/Source/imgui_stdlib.h"
//=========================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

namespace _FileDialog
{
	static float g_item_size_min = 50.0f;
	static float g_item_size_max = 200.0f;
	static bool g_is_hovering_item;
	static string g_hovered_item_path;
	static bool g_is_hovering_window;
	static ImGuiEx::DragDropPayload g_drag_drop_payload;
	static unsigned int g_context_menu_id;
}

#define OPERATION_NAME	(m_operation == FileDialog_Op_Open)	? "Open"		: (m_operation == FileDialog_Op_Load)	? "Load"		: (m_operation == FileDialog_Op_Save) ? "Save" : "View"
#define FILTER_NAME		(m_filter == FileDialog_Filter_All)	? "All (*.*)"	: (m_filter == FileDialog_Filter_Model)	? "Model(*.*)"	: "World (*.world)"

FileDialog::FileDialog(Context* context, const bool standalone_window, const FileDialog_Type type, const FileDialog_Operation operation, const FileDialog_Filter filter)
{
	m_context							= context;
	m_type								= type;
	m_operation							= operation;
	m_filter							= filter;
	m_type								= type;
	m_title								= OPERATION_NAME;
	m_is_window							= standalone_window;
	m_current_directory					= FileSystem::GetWorkingDirectory();
	m_item_size							= 100.0f;
	m_is_dirty							= true;
	m_selection_made					= false;
	m_callback_on_item_clicked			= nullptr;
	m_callback_on_item_double_clicked	= nullptr;
}

void FileDialog::SetOperation(const FileDialog_Operation operation)
{
	m_operation = operation;
	m_title		= OPERATION_NAME;
}

bool FileDialog::Show(bool* is_visible, string* directory /*= nullptr*/, string* file_path /*= nullptr*/)
{
	if (!(*is_visible))
	{
		m_is_dirty = true; // set as dirty as things can change till next time
		return false;
	}

	m_selection_made					= false;
	_FileDialog::g_is_hovering_item		= false;
	_FileDialog::g_is_hovering_window	= false;
	
	ShowTop(is_visible);	// Top menu	
	ShowMiddle();			// Contents of the current directory
	ShowBottom(is_visible); // Bottom menu

	if (m_is_window)
	{
		ImGui::End();
	}

	if (m_is_dirty)
	{
		DialogUpdateFromDirectory(m_current_directory);
		m_is_dirty = false;
	}

	if (m_selection_made)
	{
		if (directory)
		{
			(*directory) = m_current_directory;
		}

		if (file_path)
		{
			(*file_path) = m_current_directory + "/" + string(m_input_box);
		}
	}

	EmptyAreaContextMenu();

	return m_selection_made;
}

void FileDialog::ShowTop(bool* is_visible)
{
	if (m_is_window)
	{
		ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(350, 250), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::Begin(m_title.c_str(), is_visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking);
		ImGui::SetWindowFocus();
	}

	if (ImGui::Button("<"))
	{
		DialogSetCurrentPath(FileSystem::GetParentDirectory(m_current_directory));
		m_is_dirty     = true;
	}
	ImGui::SameLine();
	ImGui::Text(m_current_directory.c_str());
	ImGui::SameLine(ImGui::GetWindowContentRegionWidth() * 0.8f);
	ImGui::PushItemWidth(ImGui::GetWindowContentRegionWidth() * 0.207f);
	ImGui::SliderFloat("##FileDialogSlider", &m_item_size, _FileDialog::g_item_size_min, _FileDialog::g_item_size_max);
	ImGui::PopItemWidth();

	ImGui::Separator();
}

void FileDialog::ShowMiddle()
{
	const auto push_style = []()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f); // Remove border
	};

	const auto pop_style = []() { ImGui::PopStyleVar(); };

	// CONTENT WINDOW START
	const auto window			= ImGui::GetCurrentWindowRead();
	const auto content_width	= ImGui::GetContentRegionAvail().x;
	const auto content_height	= ImGui::GetContentRegionAvail().y - (m_type != FileDialog_Type_Browser ? 30.0f : 0.0f);

	push_style();
	if (ImGui::BeginChild("##ContentRegion", ImVec2(content_width, content_height), true))
	{
		_FileDialog::g_is_hovering_window = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ? true : _FileDialog::g_is_hovering_window;

        // Go through all the items
        float pen_x     = 0.0f;
        bool new_line   = true;
		for (int i = 0; i < m_items.size(); i++)
		{
            // Start new line ?
            if (new_line)
            {
                ImGui::BeginGroup();
                new_line = false;
            }

            ImGui::BeginGroup();
            {
                // Get item to be displayed
                auto& item = m_items[i];

                // THUMBNAIL
                {
                    ImGui::PushID(i);
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.25f));

                    if (ImGuiEx::ImageButton(item.GetTexture(), ImVec2(m_item_size, m_item_size - 23.0f)))
                    {
                        // Determine type of click
                        item.Clicked();
                        const auto is_single_click = item.GetTimeSinceLastClickMs() > 500;

                        if (is_single_click)
                        {
                            // Updated input box
                            m_input_box = item.GetLabel();
                            // Callback
                            if (m_callback_on_item_clicked) m_callback_on_item_clicked(item.GetPath());
                        }
                        else // Double Click
                        {
                            m_is_dirty = DialogSetCurrentPath(item.GetPath());
                            m_selection_made = !item.IsDirectory();

                            // Callback
                            if (m_callback_on_item_double_clicked) m_callback_on_item_double_clicked(m_current_directory);
                        }
                    }

                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                }

                // Item functionality
                {
                    // Manually detect some useful states
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
                    {
                        _FileDialog::g_is_hovering_item     = true;
                        _FileDialog::g_hovered_item_path    = item.GetPath();
                    }

                    ItemClick(&item);
                    ItemContextMenu(&item);
                    ItemDrag(&item);
                }

                // LABEL
                {
                    auto& g = *GImGui;
                    auto& style = ImGui::GetStyle();

                    const char* label_text  = item.GetLabel().c_str();
                    const ImVec2 label_size = ImGui::CalcTextSize(label_text, nullptr, true);                 
                    float label_width       = m_item_size + style.FramePadding.x;
                    float label_height      = g.FontSize + style.FramePadding.y;
                    ImRect label_rect       = ImRect
                    (
                        ImGui::GetCursorScreenPos().x,
                        ImGui::GetCursorScreenPos().y - label_height,
                        ImGui::GetCursorScreenPos().x + label_width + 5.0f, // why a 5.0f here? What style var am I missing? Future self, please fix.
                        ImGui::GetCursorScreenPos().y - 5.0f                // why a 5.0f here? What style var am I missing? Future self, please fix.
                    );

                    // Draw text background
                    ImGui::GetWindowDrawList()->AddRectFilled(label_rect.Min, label_rect.Max, IM_COL32(0.2f * 255, 0.2f * 255, 0.2f * 255, 0.75f * 255)); 

                    // Draw text
                    ImGui::SetWindowFontScale(m_item_size / 100.0f);
                    ImGui::SetCursorScreenPos(ImVec2(label_rect.Min.x, label_rect.Min.y));
       
                    if (label_size.x <= m_item_size)
                        ImGui::TextUnformatted(label_text);
                    else
                    {
                        ImGui::RenderTextClipped(label_rect.Min, label_rect.Max, label_text, nullptr, &label_size, ImVec2(0, 0), &label_rect);
                    }
                }

                ImGui::EndGroup();
            }

            // Decide whether we should switch to the next column or switch row
            pen_x += m_item_size + ImGui::GetStyle().ItemSpacing.x;
            if (pen_x >= content_width - m_item_size)
            {
                ImGui::EndGroup();
                pen_x = 0;
                new_line = true;
            }
            else
            {
                ImGui::SameLine();
            }
		}

        if (!new_line)
            ImGui::EndGroup();
	}
	ImGui::EndChild();
	pop_style();
}

void FileDialog::ShowBottom(bool* is_visible)
{
	// Bottom-right buttons
	if (m_type == FileDialog_Type_Browser)
		return;

	ImGui::SetCursorPosY(ImGui::GetWindowSize().y - 35); // move to the bottom of the window
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f); // move to the bottom of the window

	ImGui::PushItemWidth(ImGui::GetWindowSize().x - 235);
	ImGui::InputText("##InputBox", &m_input_box);
	ImGui::PopItemWidth();

	ImGui::SameLine();
	ImGui::Text(FILTER_NAME);

	ImGui::SameLine();
	if (ImGui::Button(OPERATION_NAME))
	{
		m_selection_made = true;
	}

	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
	{
		m_selection_made = false;
		(*is_visible) = false;
	}
}

void FileDialog::ItemDrag(FileDialogItem* item) const
{
	if (!item || m_type != FileDialog_Type_Browser)
		return;

	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		const auto set_payload = [](const ImGuiEx::DragPayloadType type, const std::string& path)
		{
			_FileDialog::g_drag_drop_payload.type = type;
			_FileDialog::g_drag_drop_payload.data = path.c_str();
			ImGuiEx::CreateDragPayload(_FileDialog::g_drag_drop_payload);
		};

		if (FileSystem::IsSupportedModelFile(item->GetPath()))	{ set_payload(ImGuiEx::DragPayload_Model,	item->GetPath()); }
		if (FileSystem::IsSupportedImageFile(item->GetPath()))	{ set_payload(ImGuiEx::DragPayload_Texture,	item->GetPath()); }
		if (FileSystem::IsSupportedAudioFile(item->GetPath()))	{ set_payload(ImGuiEx::DragPayload_Audio,	item->GetPath()); }
		if (FileSystem::IsEngineScriptFile(item->GetPath()))	{ set_payload(ImGuiEx::DragPayload_Script,	item->GetPath()); }

		ImGuiEx::Image(item->GetTexture(), 50);
		ImGui::EndDragDropSource();
	}
}

void FileDialog::ItemClick(FileDialogItem* item) const
{
	if (!item || !_FileDialog::g_is_hovering_window)
		return;

	// Context menu on right click
	if (ImGui::IsItemClicked(1))
	{
		_FileDialog::g_context_menu_id = item->GetId();
		ImGui::OpenPopup("##FileDialogContextMenu");
	}
}

void FileDialog::ItemContextMenu(FileDialogItem* item)
{
	if (_FileDialog::g_context_menu_id != item->GetId())
		return;

	if (!ImGui::BeginPopup("##FileDialogContextMenu"))
		return;

	if (ImGui::MenuItem("Delete"))
	{
		if (item->IsDirectory())
		{
			FileSystem::DeleteDirectory(item->GetPath());
			m_is_dirty = true;
		}
		else
		{
			FileSystem::DeleteFile_(item->GetPath());
			m_is_dirty = true;
		}
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Open in file explorer"))
	{
		FileSystem::OpenDirectoryWindow(item->GetPath());
	}

	ImGui::EndPopup();
}

bool FileDialog::DialogSetCurrentPath(const std::string& path)
{
	if (!FileSystem::IsDirectory(path))
		return false;

	m_current_directory = path;
	return true;
}

bool FileDialog::DialogUpdateFromDirectory(const std::string& path)
{
	if (!FileSystem::IsDirectory(path))
	{
		LOG_ERROR_INVALID_PARAMETER();
		return false;
	}

	m_items.clear();
	m_items.shrink_to_fit();

	// Get directories
	auto child_directories = FileSystem::GetDirectoriesInDirectory(path);
	for (const auto& child_dir : child_directories)
	{
		m_items.emplace_back(child_dir, IconProvider::Get().Thumbnail_Load(child_dir, Thumbnail_Folder, static_cast<int>(m_item_size)));
	}

	// Get files (based on filter)
	vector<string> child_files;
	if (m_filter == FileDialog_Filter_All)
	{
		child_files = FileSystem::GetFilesInDirectory(path);
		for (const auto& child_file : child_files)
		{
			m_items.emplace_back(child_file, IconProvider::Get().Thumbnail_Load(child_file, Thumbnail_Custom, static_cast<int>(m_item_size)));
		}
	}
	else if (m_filter == FileDialog_Filter_Scene)
	{
		child_files = FileSystem::GetSupportedSceneFilesInDirectory(path);
		for (const auto& child_file : child_files)
		{
			m_items.emplace_back(child_file, IconProvider::Get().Thumbnail_Load(child_file, Thumbnail_File_Scene, static_cast<int>(m_item_size)));
		}
	}
	else if (m_filter == FileDialog_Filter_Model)
	{
		child_files = FileSystem::GetSupportedModelFilesInDirectory(path);
		for (const auto& child_file : child_files)
		{
			m_items.emplace_back(child_file, IconProvider::Get().Thumbnail_Load(child_file, Thumbnail_File_Model, static_cast<int>(m_item_size)));
		}
	}

	return true;
}

void FileDialog::EmptyAreaContextMenu()
{
	if (ImGui::IsMouseClicked(1) && _FileDialog::g_is_hovering_window && !_FileDialog::g_is_hovering_item)
	{
		ImGui::OpenPopup("##Content_ContextMenu");
	}

	if (!ImGui::BeginPopup("##Content_ContextMenu"))
		return;

	if (ImGui::MenuItem("Create folder"))
	{
		FileSystem::CreateDirectory_(m_current_directory + "New folder");
		m_is_dirty = true;
	}

	if (ImGui::MenuItem("Open directory in explorer"))
	{
		FileSystem::OpenDirectoryWindow(m_current_directory);
	}

	ImGui::EndPopup();
}
