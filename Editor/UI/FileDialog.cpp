/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../ImGui/Source/imgui.h"
#include "../ImGui/Source/imgui_internal.h"
#include "FileSystem/FileSystem.h"
#include "Logging/Log.h"
#include "EditorHelper.h"
#include "DragDrop.h"
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
	static const char* g_hoveredItemPath;
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
	m_currentPath					= FileSystem::GetWorkingDirectory();
	m_itemSize						= 100.0f;
	m_isDirty						= true;
	m_selectionMade					= false;
	m_callback_OnPathClicked		= nullptr;
	m_callback_OnPathDoubleClicked	= nullptr;
}

void FileDialog::SetOperation(FileDialog_Operation operation)
{
	m_operation = operation;
	m_title		= OPERATION_NAME;
}

bool FileDialog::Show(bool* isVisible, string* itemPath)
{
	if (!(*isVisible))
	{
		m_wasVisible = false;
		return false;
	}

	// Force an update as files may have changed since last time
	if (!m_wasVisible)
	{
		m_isDirty = true;
	}

	m_selectionMade							= false;
	m_wasVisible							= true;
	FileDialog_Options::g_isHoveringItem	= false;
	FileDialog_Options::g_hoveredItemPath	= "";
	FileDialog_Options::g_isHoveringWindow	= false;

	
	Show_Top(isVisible);	// Top menu	
	Show_Middle();			// Contents of the current directory
	Show_Bottom(isVisible); // Bottom menu

	if (m_isWindow)
	{
		ImGui::End();
	}

	if (m_isDirty)
	{
		Dialog_UpdateFromDirectory();
		m_isDirty = false;
	}

	if (itemPath && m_selectionMade)
	{
		if (m_type == FileDialog_Type_Browser)
		{
			(*itemPath) = m_currentPath;
		}
		else
		{
			if (m_operation == FileDialog_Op_Save)
			{
				(*itemPath) = m_currentPath + "/" + string(m_inputBox) + EXTENSION_SCENE;
			}
			else if (m_operation == FileDialog_Op_Open || m_operation == FileDialog_Op_Load)
			{
				(*itemPath) = m_currentPath;
			}
		}
	}

	Dialog_ContextMenu();
	Dialog_Click();

	return m_selectionMade;
}

void FileDialog::Show_Top(bool* isVisible)
{
	if (m_isWindow)
	{
		ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(350, 250), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::Begin(m_title.c_str(), isVisible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);
		ImGui::SetWindowFocus();
	}

	if (ImGui::Button("<"))
	{
		Dialog_SetCurrentPath(FileSystem::GetParentDirectory(m_currentPath));
		m_isDirty     = true;
	}
	ImGui::SameLine();
	ImGui::Text(m_currentPath.c_str());
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
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 18.0f));	// Remove item spacing (because it clips the thumbnails)
	};

	auto PopStyle = []() { ImGui::PopStyleVar(2); };

	// CONTENT WINDOW START
	ImGuiWindow* window = ImGui::GetCurrentWindowRead();
	float contentWidth  = window->ContentsRegionRect.Max.x - window->ContentsRegionRect.Min.x;
	float contentHeight = window->ContentsRegionRect.Max.y - window->ContentsRegionRect.Min.y - (m_type != FileDialog_Type_Browser ? 58.0f : 28.0f);

	PushStyle();
	ImGui::BeginChild("##ContentRegion", ImVec2(contentWidth, contentHeight), true);
	{
		FileDialog_Options::g_isHoveringWindow = ImGui::IsMouseHoveringWindow() ? true : FileDialog_Options::g_isHoveringWindow;

		// A bunch of columns, where is item represents a thumbnail
		int index   = 0;
		int columns = (int)(ImGui::GetWindowContentRegionWidth() / m_itemSize);
		columns     = columns < 1 ? 1 : columns;
		ImGui::Columns(columns, nullptr, false);
		for (auto& item : m_items)
		{
			// IMAGE BUTTON
			{
				ImGui::PushID(index);
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0)); // Remove button's border
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); // Remove button's background

				if (ImGui::ImageButton(item.GetShaderResource(), ImVec2(m_itemSize, m_itemSize - 23.0f)))
				{
					item.Clicked();

					if (m_currentPathID != item.GetID()) // Single click
					{		
						m_currentPath	= item.GetPath();
						m_currentPathID = item.GetID();			
						EditorHelper::SetCharArray(&m_inputBox[0], item.GetLabel());

						// Callback
						if (m_callback_OnPathClicked) m_callback_OnPathClicked(item.GetPath());
					}
					else if (item.GetTimeSinceLastClickMs() <= 500) // Double click
					{
						if (item.IsDirectory())
						{
							Dialog_SetCurrentPath(item.GetPath());
							m_isDirty = true;
						}
						m_selectionMade = !item.IsDirectory();

						// Callback
						if (m_callback_OnPathDoubleClicked) m_callback_OnPathDoubleClicked(m_currentPath);
					}
				}
				ImGui::PopStyleColor(2);
				ImGui::PopID();						
			}

			// Manually detect some useful states
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
			{
				FileDialog_Options::g_isHoveringItem	= true;
				FileDialog_Options::g_hoveredItemPath	= item.GetPath();
			}

			// Item functionality
			{
				PopStyle();

				Item_Click(&item);
				Item_ContextMenu(&item);
				Item_Drag(&item);

				PushStyle();
			}

			// LABEL
			{
				ImGui::SameLine();
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() - m_itemSize);		// move to the left of the thumbnail
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + m_itemSize - 13);	// move to the bottom of the thumbnail
				ImGui::PushItemWidth(m_itemSize + 8.5f);

				ImGui::Text(item.GetLabel());

				ImGui::PopItemWidth();
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m_itemSize);		// move to the right of the thumbnail
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - m_itemSize + 13);	// move to the top of the thumbnail
			}

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
	ImGui::InputText("##InputBox", m_inputBox, BUFFER_TEXT_DEFAULT);
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

	if (DragDrop::Get().DragBegin())
	{
		auto SetPayload = [this](DragPayloadType type, const char* path)
		{
			FileDialog_Options::g_dragDropPayload.type = type;
			FileDialog_Options::g_dragDropPayload.data = path;
			DragDrop::Get().DragPayload(FileDialog_Options::g_dragDropPayload);
		};

		if (FileSystem::IsSupportedModelFile(item->GetPath()))	{ SetPayload(DragPayload_Model,		item->GetPath()); }
		if (FileSystem::IsSupportedImageFile(item->GetPath()))	{ SetPayload(DragPayload_Texture,	item->GetPath()); }
		if (FileSystem::IsSupportedAudioFile(item->GetPath()))	{ SetPayload(DragPayload_Audio,		item->GetPath()); }
		if (FileSystem::IsEngineScriptFile(item->GetPath()))	{ SetPayload(DragPayload_Script,	item->GetPath()); }

		THUMBNAIL_IMAGE_BY_SHADER_RESOURCE(item->GetShaderResource(), 50);
		DragDrop::Get().DragEnd();
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

	// Current path change on right click
	if (ImGui::IsMouseClicked(1) && FileDialog_Options::g_isHoveringItem)
	{
		Dialog_SetCurrentPath(item->GetPath());
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
			Dialog_UpdateFromDirectory();
		}
		else
		{
			FileSystem::DeleteFile_(item->GetPath());
			Dialog_UpdateFromDirectory();
		}
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Open in file explorer"))
	{
		FileSystem::OpenDirectoryWindow(item->GetPath());
	}

	ImGui::EndPopup();
}

bool FileDialog::Dialog_UpdateFromDirectory(const char* path /*= nullptr*/)
{
	if (!path)
	{
		path = m_currentPath.c_str();
	}

	if (!FileSystem::IsDirectory(path))
	{
		LOG_WARNING("FileDialog::UpdateFromDirectory: Invalid parameter.");
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

void FileDialog::Dialog_Click()
{
	if (!FileDialog_Options::g_isHoveringWindow || FileDialog_Options::g_isHoveringItem)
		return;

	if (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))
	{
		Dialog_SetCurrentPath(FileSystem::GetDirectoryFromFilePath(m_currentPath));
	}
}

void FileDialog::Dialog_ContextMenu()
{
	if (ImGui::IsMouseClicked(1) && FileDialog_Options::g_isHoveringWindow && !FileDialog_Options::g_isHoveringItem)
	{
		ImGui::OpenPopup("##Content_ContextMenu");
	}

	if (!ImGui::BeginPopup("##Content_ContextMenu"))
		return;

	if (ImGui::MenuItem("Create folder"))
	{
		FileSystem::CreateDirectory_(m_currentPath + "New folder");
		Dialog_UpdateFromDirectory();
	}

	if (ImGui::MenuItem("Open directory in explorer"))
	{
		FileSystem::OpenDirectoryWindow(m_currentPath);
	}

	ImGui::EndPopup();
}

void FileDialog::Dialog_SetCurrentPath(const std::string& path)
{
	m_currentPath = path;
}
