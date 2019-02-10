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
#include "DragDrop.h"
#include "../ImGui/Source/imgui.h"
#include "../ImGui/Source/imgui_internal.h"
#include "../ImGui/Source/imgui_stdlib.h"
//=========================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

namespace FileDialog_Options
{
	static float g_itemSizeMin = 50.0f;
	static float g_itemSizeMax = 200.0f;
	static bool g_isHoveringItem;
	static string g_hoveredItemPath;
	static bool g_isHoveringWindow;
	static DragDropPayload g_dragDropPayload;
	static unsigned int g_contextMenuID;
}

#define OPERATION_NAME	(m_operation == FileDialog_Op_Open)	? "Open"		: (m_operation == FileDialog_Op_Load)	? "Load"		: (m_operation == FileDialog_Op_Save) ? "Save" : "View"
#define FILTER_NAME		(m_filter == FileDialog_Filter_All)	? "All (*.*)"	: (m_filter == FileDialog_Filter_Model)	? "Model(*.*)"	: "Scene (*.scene)"

FileDialog::FileDialog(Context* context, bool standaloneWindow, FileDialog_Type type, FileDialog_Operation operation, FileDialog_Filter filter)
{
	m_context						= context;
	m_type							= type;
	m_operation						= operation;
	m_filter						= filter;
	m_type							= type;
	m_title							= OPERATION_NAME;
	m_isWindow						= standaloneWindow;
	m_currentDirectory				= FileSystem::GetWorkingDirectory();
	m_itemSize						= 100.0f;
	m_isDirty						= true;
	m_selectionMade					= false;
	m_callback_OnItemClicked		= nullptr;
	m_callback_OnItemDoubleClicked	= nullptr;
}

void FileDialog::SetOperation(FileDialog_Operation operation)
{
	m_operation = operation;
	m_title		= OPERATION_NAME;
}

bool FileDialog::Show(bool* isVisible, string* directory /*= nullptr*/, string* filePath /*= nullptr*/)
{
	if (!(*isVisible))
	{
		m_wasVisible	= false;
		m_isDirty		= true; // set as dirty as things can change till next time
		return false;
	}

	m_selectionMade							= false;
	m_wasVisible							= true;
	FileDialog_Options::g_isHoveringItem	= false;
	FileDialog_Options::g_isHoveringWindow	= false;
	FileDialog_Options::g_hoveredItemPath;
	
	Show_Top(isVisible);	// Top menu	
	Show_Middle();			// Contents of the current directory
	Show_Bottom(isVisible); // Bottom menu

	if (m_isWindow)
	{
		ImGui::End();
	}

	if (m_isDirty)
	{
		Dialog_UpdateFromDirectory(m_currentDirectory);
		m_isDirty = false;
	}

	if (m_selectionMade)
	{
		if (directory)
		{
			(*directory) = m_currentDirectory;
		}

		if (filePath)
		{
			(*filePath) = m_currentDirectory + "/" + string(m_inputBox);
		}
	}

	EmptyArea_ContextMenu();

	return m_selectionMade;
}

void FileDialog::Show_Top(bool* isVisible)
{
	if (m_isWindow)
	{
		ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(350, 250), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::Begin(m_title.c_str(), isVisible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking);
		ImGui::SetWindowFocus();
	}

	if (ImGui::Button("<"))
	{
		Dialog_SetCurrentPath(FileSystem::GetParentDirectory(m_currentDirectory));
		m_isDirty     = true;
	}
	ImGui::SameLine();
	ImGui::Text(m_currentDirectory.c_str());
	ImGui::SameLine(ImGui::GetWindowContentRegionWidth() * 0.8f);
	ImGui::PushItemWidth(ImGui::GetWindowContentRegionWidth() * 0.207f);
	ImGui::SliderFloat("##FileDialogSlider", &m_itemSize, FileDialog_Options::g_itemSizeMin, FileDialog_Options::g_itemSizeMax);
	ImGui::PopItemWidth();

	ImGui::Separator();
}

void FileDialog::Show_Middle()
{
	auto PushStyle = []()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);				// Remove border
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 5.0f));		// Remove item spacing (because it clips the thumbnails)
	};

	auto PopStyle = []() { ImGui::PopStyleVar(2); };

	// CONTENT WINDOW START
	ImGuiWindow* window = ImGui::GetCurrentWindowRead();
	float contentWidth  = window->ContentsRegionRect.Max.x - window->ContentsRegionRect.Min.x;
	float contentHeight = window->ContentsRegionRect.Max.y - window->ContentsRegionRect.Min.y - (m_type != FileDialog_Type_Browser ? 58.0f : 28.0f);

	PushStyle();
	ImGui::BeginChild("##ContentRegion", ImVec2(contentWidth, contentHeight), true);
	{
		FileDialog_Options::g_isHoveringWindow = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ? true : FileDialog_Options::g_isHoveringWindow;

		// A bunch of columns, where is item represents a thumbnail
		int index   = 0;
		int columns = (int)(ImGui::GetWindowContentRegionWidth() / m_itemSize);
		columns     = columns < 1 ? 1 : columns;
		ImGui::Columns(columns, nullptr, false);
		for (auto& item : m_items)
		{
			ImGui::BeginGroup();
			{
				// THUMBNAIL
				{
					ImGui::PushID(index);
					ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

					if (ImGui::ImageButton(item.GetShaderResource(), ImVec2(m_itemSize, m_itemSize - 23.0f)))
					{
						// Determine type of click
						item.Clicked();
						bool isSingleClick = item.GetTimeSinceLastClickMs() > 500;

						if (isSingleClick)
						{
							// Updated input box
							m_inputBox = item.GetLabel();
							// Callback
							if (m_callback_OnItemClicked) m_callback_OnItemClicked(item.GetPath());
						}
						else // Double Click
						{
							m_isDirty		= Dialog_SetCurrentPath(item.GetPath());
							m_selectionMade = !item.IsDirectory();

							// Callback
							if (m_callback_OnItemDoubleClicked) m_callback_OnItemDoubleClicked(m_currentDirectory);				
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
						FileDialog_Options::g_isHoveringItem = true;
						FileDialog_Options::g_hoveredItemPath = item.GetPath();
					}

					Item_Click(&item);
					Item_ContextMenu(&item);
					Item_Drag(&item);
				}

				// LABEL
				{
					ImGui::Text(item.GetLabel().c_str());
				}			
			}
			ImGui::EndGroup();

			// COLUMN END
			ImGui::NextColumn();
			index++;
		}
		ImGui::Columns(1);

	} // CONTENT WINDOW END
	ImGui::EndChild();
	PopStyle();
}

void FileDialog::Show_Bottom(bool* isVisible)
{
	// Bottom-right buttons
	if (m_type == FileDialog_Type_Browser)
		return;

	ImGui::SetCursorPosY(ImGui::GetWindowSize().y - 35); // move to the bottom of the window
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f); // move to the bottom of the window

	ImGui::PushItemWidth(ImGui::GetWindowSize().x - 235);
	ImGui::InputText("##InputBox", &m_inputBox);
	ImGui::PopItemWidth();

	ImGui::SameLine();
	ImGui::Text(FILTER_NAME);

	ImGui::SameLine();
	if (ImGui::Button(OPERATION_NAME))
	{
		m_selectionMade = true;
	}

	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
	{
		m_selectionMade = false;
		(*isVisible) = false;
	}
}

void FileDialog::Item_Drag(FileDialog_Item* item)
{
	if (!item || m_type != FileDialog_Type_Browser)
		return;

	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		auto SetPayload = [this](DragPayloadType type, const std::string& path)
		{
			FileDialog_Options::g_dragDropPayload.type = type;
			FileDialog_Options::g_dragDropPayload.data = path.c_str();
			DragDrop::Get().DragPayload(FileDialog_Options::g_dragDropPayload);
		};

		if (FileSystem::IsSupportedModelFile(item->GetPath()))	{ SetPayload(DragPayload_Model,		item->GetPath()); }
		if (FileSystem::IsSupportedImageFile(item->GetPath()))	{ SetPayload(DragPayload_Texture,	item->GetPath()); }
		if (FileSystem::IsSupportedAudioFile(item->GetPath()))	{ SetPayload(DragPayload_Audio,		item->GetPath()); }
		if (FileSystem::IsEngineScriptFile(item->GetPath()))	{ SetPayload(DragPayload_Script,	item->GetPath()); }

		THUMBNAIL_IMAGE_BY_SHADER_RESOURCE(item->GetShaderResource(), 50);
		ImGui::EndDragDropSource();
	}
}

void FileDialog::Item_Click(FileDialog_Item* item)
{
	if (!item || !FileDialog_Options::g_isHoveringWindow)
		return;

	// Context menu on right click
	if (ImGui::IsItemClicked(1))
	{
		FileDialog_Options::g_contextMenuID = item->GetID();
		ImGui::OpenPopup("##FileDialogContextMenu");
	}
}

void FileDialog::Item_ContextMenu(FileDialog_Item* item)
{
	if (FileDialog_Options::g_contextMenuID != item->GetID())
		return;

	if (!ImGui::BeginPopup("##FileDialogContextMenu"))
		return;

	if (ImGui::MenuItem("Delete"))
	{
		if (item->IsDirectory())
		{
			FileSystem::DeleteDirectory(item->GetPath());
			m_isDirty = true;
		}
		else
		{
			FileSystem::DeleteFile_(item->GetPath());
			m_isDirty = true;
		}
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Open in file explorer"))
	{
		FileSystem::OpenDirectoryWindow(item->GetPath());
	}

	ImGui::EndPopup();
}

bool FileDialog::Dialog_SetCurrentPath(const std::string& path)
{
	if (!FileSystem::IsDirectory(path))
		return false;

	m_currentDirectory = path;
	return true;
}

bool FileDialog::Dialog_UpdateFromDirectory(const std::string& path)
{
	if (!FileSystem::IsDirectory(path))
	{
		LOG_ERROR_INVALID_PARAMETER();
		return false;
	}

	m_items.clear();
	m_items.shrink_to_fit();

	// Get directories
	vector<string> childDirectories = FileSystem::GetDirectoriesInDirectory(path);
	for (const auto& childDir : childDirectories)
	{
		m_items.emplace_back(childDir, IconProvider::Get().Thumbnail_Load(childDir, Thumbnail_Folder, (int)m_itemSize));
	}

	// Get files (based on filter)
	vector<string> childFiles;
	if (m_filter == FileDialog_Filter_All)
	{
		childFiles = FileSystem::GetFilesInDirectory(path);
		for (const auto& childFile : childFiles)
		{
			m_items.emplace_back(childFile, IconProvider::Get().Thumbnail_Load(childFile, Thumbnail_Custom, (int)m_itemSize));
		}
	}
	else if (m_filter == FileDialog_Filter_Scene)
	{
		childFiles = FileSystem::GetSupportedSceneFilesInDirectory(path);
		for (const auto& childFile : childFiles)
		{
			m_items.emplace_back(childFile, IconProvider::Get().Thumbnail_Load(childFile, Thumbnail_File_Scene, (int)m_itemSize));
		}
	}
	else if (m_filter == FileDialog_Filter_Model)
	{
		childFiles = FileSystem::GetSupportedModelFilesInDirectory(path);
		for (const auto& childFile : childFiles)
		{
			m_items.emplace_back(childFile, IconProvider::Get().Thumbnail_Load(childFile, Thumbnail_File_Model, (int)m_itemSize));
		}
	}

	return true;
}

void FileDialog::EmptyArea_ContextMenu()
{
	if (ImGui::IsMouseClicked(1) && FileDialog_Options::g_isHoveringWindow && !FileDialog_Options::g_isHoveringItem)
	{
		ImGui::OpenPopup("##Content_ContextMenu");
	}

	if (!ImGui::BeginPopup("##Content_ContextMenu"))
		return;

	if (ImGui::MenuItem("Create folder"))
	{
		FileSystem::CreateDirectory_(m_currentDirectory + "New folder");
		m_isDirty = true;
	}

	if (ImGui::MenuItem("Open directory in explorer"))
	{
		FileSystem::OpenDirectoryWindow(m_currentDirectory);
	}

	ImGui::EndPopup();
}
