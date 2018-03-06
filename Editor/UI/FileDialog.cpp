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

//= INCLUDES =======================
#include "FileDialog.h"
#include "../ImGui/imgui.h"
#include "FileSystem/FileSystem.h"
#include "Logging/Log.h"
#include "EditorHelper.h"
#include "DragDrop.h"
#include "../ImGui/imgui_internal.h"
//==================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

namespace FileDialogStatics
{
	static float g_itemSizeMin = 100.0f;
	static float g_itemSizeMax = 200.0f;
	static bool g_isHoveringItem;
	static const char* g_hoveredItemPath;
	static bool g_isMouseHoveringWindow;
	static DragDropPayload g_dragDropPayload;
}

#define GET_WINDOW_NAME (type == FileDialog_Open)		? "Open"		: (type == FileDialog_Load)			? "Load"		: (type == FileDialog_Save) ? "Save" : "View"
#define GET_FILTER_NAME	(m_filter == FileDialog_All)	? "All (*.*)"	: (m_filter == FileDialog_Model)	? "Model(*.*)"	: "Scene (*.scene)"
#define GET_BUTTON_NAME (m_style == FileDialog_Open)	? "Open"		: (m_style == FileDialog_Load)		? "Load"		: "Save"

FileDialog::FileDialog(Context* context, bool standaloneWindow, FileDialog_Filter filter, FileDialog_Mode type)
{
	m_context         = context;
	m_filter          = filter;
	m_style           = type;
	m_title           = GET_WINDOW_NAME;
	m_isWindow        = standaloneWindow;
	m_currentPath     = FileSystem::GetWorkingDirectory();
	m_currentFullPath = m_currentPath;
	m_itemSize        = (type != FileDialog_Basic) ? FileDialogStatics::g_itemSizeMin * 2.0f : FileDialogStatics::g_itemSizeMin;
	m_stopwatch       = make_unique<Stopwatch>();
	m_isDirty         = true;
	m_selectionMade   = false;
}

void FileDialog::SetFilter(FileDialog_Filter filter)
{
	m_filter = filter;
}

void FileDialog::SetStyle(FileDialog_Mode type)
{
	m_style = type;
	m_title = GET_WINDOW_NAME;
}

bool FileDialog::Show(bool* isVisible, string* path)
{
	if (!(*isVisible))
		return false;

	m_selectionMade								= false;
	FileDialogStatics::g_isHoveringItem			= false;
	FileDialogStatics::g_hoveredItemPath		= "";
	FileDialogStatics::g_isMouseHoveringWindow = false;

	// Top menu
	Dialog_Top(isVisible);
	// Contents of the current directory
	Dialog_Middle();
	// Bottom menu
	Dialog_Bottom(isVisible);

	if (m_isWindow)
	{
		ImGui::End();
	}

	if (m_isDirty)
	{
		NavigateToDirectory(m_currentPath);
		m_isDirty = false;
	}

	if (m_selectionMade)
	{
		(*path) = m_currentPath + "/" + string(m_fileNameText);
	}

	return m_selectionMade;
}

void FileDialog::Dialog_Top(bool* isVisible)
{
	if (m_isWindow)
	{
		ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
		ImGui::Begin(m_title.c_str(), isVisible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_ResizeFromAnySide | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);
		ImGui::SetWindowFocus();
	}

	if (ImGui::Button("<"))
	{
		m_currentPath = FileSystem::GetParentDirectory(m_currentPath);
		m_isDirty     = true;
	}
	ImGui::SameLine();
	ImGui::Text(m_currentPath.c_str());
	ImGui::SameLine(ImGui::GetWindowContentRegionWidth() * 0.8f);
	ImGui::PushItemWidth(ImGui::GetWindowContentRegionWidth() * 0.207f);
	ImGui::SliderFloat("##FileDialogSlider", &m_itemSize, FileDialogStatics::g_itemSizeMin, FileDialogStatics::g_itemSizeMax);
	ImGui::PopItemWidth();
}

void FileDialog::Dialog_Middle()
{
	// CONTENT WINDOW START
	ImGuiWindow* window = ImGui::GetCurrentWindowRead();
	float contentWidth  = window->ContentsRegionRect.Max.x - window->ContentsRegionRect.Min.x;
	float contentHeight = window->ContentsRegionRect.Max.y - window->ContentsRegionRect.Min.y - (m_style != FileDialog_Basic ? 55.0f : 25.0f);
	ImGui::BeginChild("##ContentRegion", ImVec2(contentWidth, contentHeight), true);

	FileDialogStatics::g_isMouseHoveringWindow = ImGui::IsMouseHoveringWindow() ? true : FileDialogStatics::g_isMouseHoveringWindow;

	// LIST OF WINDOWS, REPRESENTING DIRECTORY CONTENTS
	int index   = 0;
	int columns = (int)(ImGui::GetWindowContentRegionWidth() / m_itemSize);
	columns     = columns < 1 ? 1 : columns;
	ImGui::Columns(columns, nullptr, false);
	for (const auto& entry : m_directoryEntries)
	{
		// ITEM WINDOW START
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
		ImGui::BeginChild(entry.first.c_str(), ImVec2(m_itemSize + 25, m_itemSize + 15), true, ImGuiWindowFlags_NoScrollbar);

		// ICON
		ImGui::PushID(index);
		if (ImGui::ImageButton(SHADER_RESOURCE(entry.second), ImVec2(m_itemSize, m_itemSize - 23.0f)))
		{
			if (m_currentFullPath != entry.first) // Single click
			{
				m_currentFullPath = entry.first;
				EditorHelper::SetCharArray(&m_fileNameText[0], FileSystem::GetFileNameFromFilePath(entry.first));
				m_stopwatch->Start();
			}
			else if (m_stopwatch->GetElapsedTimeMs() <= 500) // Double click
			{
				bool isDirectory = FileSystem::IsDirectory(entry.first);
				if (isDirectory)
				{
					m_currentPath = entry.first;
					m_isDirty     = true;
				}
				m_selectionMade = !isDirectory;
			}
		}
		ImGui::PopID();

		// Manually detect some useful states
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
		{
			FileDialogStatics::g_isHoveringItem     = true;
			FileDialogStatics::g_hoveredItemPath = entry.first.c_str();
		}
		FileDialogStatics::g_isMouseHoveringWindow = ImGui::IsMouseHoveringWindow() ? true : FileDialogStatics::g_isMouseHoveringWindow;
		HandleClicking(FileDialogStatics::g_hoveredItemPath);
		ContextMenu();
		
		// Drag
		HandleDrag(entry);

		// ITEM WINDOW END		
		ImGui::EndChild();
		ImGui::PopStyleVar();

		// LABEL
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() - m_itemSize - 20); // move to the left of the thumbnail
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + m_itemSize - 5);  // move to the bottom of the thumbnail
		ImGui::PushItemWidth(m_itemSize + 8.5f);

		EditorHelper::SetCharArray(&m_itemLabel[0], FileSystem::GetFileNameFromFilePath(entry.first));
		ImGui::Text(m_itemLabel);

		ImGui::PopItemWidth();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m_itemSize + 20); // move to the right of the thumbnail
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - m_itemSize + 5);  // move to the top of the thumbnail

		// COLUMN END
		ImGui::NextColumn();
		index++;
	}
	ImGui::Columns(1);

	// CONTENT WINDOW END
	ImGui::EndChild();
}

void FileDialog::Dialog_Bottom(bool* isVisible)
{
	// Bottom-right buttons
	if (m_style != FileDialog_Basic)
	{
		ImGui::SetCursorPosY(ImGui::GetWindowSize().y - 35); // move to the bottom of the window
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f); // move to the bottom of the window

		ImGui::PushItemWidth(ImGui::GetWindowSize().x - 235);
		ImGui::InputText("##FileName", m_fileNameText, BUFFER_TEXT_DEFAULT);
		ImGui::PopItemWidth();

		ImGui::SameLine();
		ImGui::Text(GET_FILTER_NAME);

		ImGui::SameLine();
		if (ImGui::Button(GET_BUTTON_NAME))
		{
			m_selectionMade = true;
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_selectionMade = false;
			(*isVisible)    = false;
		}
	}
}

bool FileDialog::NavigateToDirectory(const string& pathClicked)
{
	if (!FileSystem::IsDirectory(pathClicked))
	{
		LOG_WARNING("FileDialog: Can't navigate to directory, provided directory is invalid.");
		return false;
	}

	m_directoryEntries.clear();

	// Get directories
	vector<string> childDirectories = FileSystem::GetDirectoriesInDirectory(pathClicked);
	for (const auto& childDir : childDirectories)
	{
		AddThumbnail(childDir, Thumbnail_Folder);
	}

	// Get files (based on filter)
	vector<string> childFiles;
	if (m_filter == FileDialog_All)
	{
		childFiles = FileSystem::GetFilesInDirectory(pathClicked);
		for (const auto& childFile : childFiles)
		{
			AddThumbnail(childFile);
		}
	}
	else if (m_filter == FileDialog_Scene)
	{
		childFiles = FileSystem::GetSupportedSceneFilesInDirectory(pathClicked);
		for (const auto& childFile : childFiles)
		{
			AddThumbnail(childFile, Thumbnail_File_Scene);
		}
	}
	else if (m_filter == FileDialog_Model)
	{
		childFiles = FileSystem::GetSupportedModelFilesInDirectory(pathClicked);
		for (const auto& childFile : childFiles)
		{
			AddThumbnail(childFile, Thumbnail_File_Model);
		}
	}

	EditorHelper::SetCharArray(&m_fileNameText[0], "");

	return true;
}

void FileDialog::AddThumbnail(const std::string& filePath, Thumbnail_Type type)
{
	m_directoryEntries[filePath] = ThumbnailProvider::Get().Thumbnail_Load(filePath, type, (int)m_itemSize);
}

void FileDialog::HandleDrag(const map<basic_string<char>, Thumbnail>::value_type& entry)
{
	if (m_style == FileDialog_Basic)
	{
		if (DragDrop::Get().DragBegin())
		{
			if (FileSystem::IsSupportedModelFile(entry.first))
			{
				FileDialogStatics::g_dragDropPayload.type = DragPayload_Model;
				FileDialogStatics::g_dragDropPayload.data = entry.first;
				DragDrop::Get().DragPayload(FileDialogStatics::g_dragDropPayload, entry.second.texture->GetShaderResource());
			}
			else if (FileSystem::IsSupportedImageFile(entry.first) || FileSystem::IsEngineTextureFile(entry.first))
			{
				FileDialogStatics::g_dragDropPayload.type = DragPayload_Texture;
				FileDialogStatics::g_dragDropPayload.data = entry.first;
				DragDrop::Get().DragPayload(FileDialogStatics::g_dragDropPayload, entry.second.texture->GetShaderResource());
			}
			else if (FileSystem::IsSupportedAudioFile(entry.first))
			{
				FileDialogStatics::g_dragDropPayload.type = DragPayload_Audio;
				FileDialogStatics::g_dragDropPayload.data = entry.first;
				DragDrop::Get().DragPayload(FileDialogStatics::g_dragDropPayload, entry.second.texture->GetShaderResource());
			}
			else if (FileSystem::IsEngineScriptFile(entry.first))
			{
				FileDialogStatics::g_dragDropPayload.type = DragPayload_Script;
				FileDialogStatics::g_dragDropPayload.data = entry.first;
				DragDrop::Get().DragPayload(FileDialogStatics::g_dragDropPayload, entry.second.texture->GetShaderResource());
			}
			DragDrop::Get().DragEnd();
		}
	}
}

void FileDialog::HandleClicking(const char* directoryEntry)
{
	// Since we are handling clicking manually, we must ensure we are inside the window
	if (!FileDialogStatics::g_isMouseHoveringWindow)
		return;

	// Right click
	if (ImGui::IsMouseClicked(1))
	{
		ImGui::OpenPopup("##FileDialogContextMenu");
	}

	// Right click on item
	if (ImGui::IsMouseClicked(1) && FileDialogStatics::g_isHoveringItem)
	{
		if (FileSystem::IsDirectory(directoryEntry))
		{
			m_currentFullPath = directoryEntry;
		}
		else
		{
			m_currentFullPath = FileSystem::GetDirectoryFromFilePath(m_currentPath) + FileSystem::GetFileNameFromFilePath(directoryEntry);
		}
	}

	// Right/Left click on empty space
	if (((ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) && !FileDialogStatics::g_isHoveringItem) && m_stopwatch->GetElapsedTimeMs() >= 500)
	{
		m_currentFullPath = m_currentPath;
	}
}

void FileDialog::ContextMenu()
{
	if (!ImGui::BeginPopup("##FileDialogContextMenu"))
		return;

	if (ImGui::MenuItem("Create folder"))
	{
		FileSystem::CreateDirectory_(m_currentPath + "New folder");
		NavigateToDirectory(m_currentPath);
	}
	
	if (FileDialogStatics::g_isHoveringItem)
	{
		if (ImGui::MenuItem("Delete"))
		{
			if (FileSystem::IsDirectory(m_currentFullPath))
			{
				FileSystem::DeleteDirectory(m_currentFullPath);
				NavigateToDirectory(m_currentPath);
			}
			else
			{
				FileSystem::DeleteFile_(m_currentFullPath);
				NavigateToDirectory(m_currentPath);
			}
		}
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Open in file explorer"))
	{
		FileSystem::OpenDirectoryWindow(m_currentFullPath);
	}

	ImGui::EndPopup();
}
