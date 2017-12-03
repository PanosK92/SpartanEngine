/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ========================
#include "DirectusFileExplorer.h"
#include "DirectusIconProvider.h"
#include "DirectusFileDialog.h"
#include "DirectusInspector.h"
#include "DirectusHierarchy.h"
#include <QStandardItem>
#include <QApplication>
#include <QMouseEvent>
#include <QMimeData>
#include <QMenu>
#include <QDrag>
#include "Logging/Log.h"
#include "FileSystem/FileSystem.h"
#include "Core/GameObject.h"
#include "Graphics/Material.h"
#include "Resource/ResourceManager.h"
//===================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

DirectusFileExplorer::DirectusFileExplorer(QWidget* parent) : QListView(parent)
{

}

void DirectusFileExplorer::Initialize(QWidget* mainWindow, DirectusViewport* directusViewport, DirectusHierarchy* hierarchy, DirectusInspector* inspector)
{
    m_directusViewport = directusViewport;
    m_hierarchy = hierarchy;
    m_inspector = inspector;

    this->setAcceptDrops(true);
    this->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_fileModel = new QFileSystemModel(this);

    // Set a filter that displays only folders
    m_fileModel->setFilter(QDir::Files | QDir::AllDirs | QDir::NoDotAndDotDot);

    // Set icon provider
    m_directusIconProvider = new DirectusIconProvider();
    m_directusIconProvider->SetContext(m_inspector->GetContext());
    m_fileModel->setIconProvider(m_directusIconProvider);

    // Create file dialog
    m_fileDialog = new DirectusFileDialog();
    m_fileDialog->Initialize(mainWindow, hierarchy, directusViewport);

    // Set the model to the tree view
    this->setModel(m_fileModel);

    // Context menu
    connect(this, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(ShowContextMenu(QPoint)));

    // Double click
    connect(this, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(DoubleClick(QModelIndex)));

    string projectDirectory = directusViewport->GetEngineContext()->GetSubsystem<ResourceManager>()->GetProjectDirectory();
    SetRootDirectory(projectDirectory);
}

void DirectusFileExplorer::SetRootPath(QString path)
{
    this->setRootIndex(m_fileModel->setRootPath(path));
}

QFileSystemModel* DirectusFileExplorer::GetFileSystemModel()
{
    return m_fileModel;
}

//= DRAG N DROP RELATED ============================================================================
void DirectusFileExplorer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        // save the position in order to be
        // try and determine later if that's a drag
        m_dragStartPosition = event->pos();

        // Make QListView deselect any items when
        // you click anywhere else but on an item.
        QModelIndex item = indexAt(event->pos());
        QListView::mousePressEvent(event);
        if ((item.row() == -1 && item.column() == -1))
        {
            clearSelection();
            const QModelIndex index;
            selectionModel()->setCurrentIndex(index, QItemSelectionModel::Select);
        }
    }

    if(event->button() == Qt::RightButton)
    {
        clearSelection();
        selectionModel()->setCurrentIndex(indexAt(event->pos()), QItemSelectionModel::Select);
        emit customContextMenuRequested(event->pos());
    }
}

void DirectusFileExplorer::mouseMoveEvent(QMouseEvent* event)
{
    // Determine whether a drag should begin, and
    // construct a drag object to handle the operation.
    if (!(event->buttons() & Qt::LeftButton))
        return;

    if ((event->pos() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance())
        return;

    QModelIndexList selectedItems = this->selectionModel()->selectedIndexes();
    if (selectedItems.empty())
        return;

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;

    QString filePath = m_fileModel->fileInfo(selectedItems[0]).absoluteFilePath();
    filePath = QString::fromStdString(FileSystem::GetRelativeFilePath(filePath.toStdString()));
    mimeData->setText(filePath);
    drag->setMimeData(mimeData);

    drag->exec();
}

void DirectusFileExplorer::mouseReleaseEvent(QMouseEvent* event)
{
    // Clear the hierarchy and the inspector
    m_hierarchy->clearSelection();
    m_inspector->Clear();

    // See if anything was actually clicked,
    QModelIndexList selectedItems = this->selectionModel()->selectedIndexes();
    if (selectedItems.empty())
        return;

    // If something was indeed clicked, get it's path.
    string filePath = m_fileModel->fileInfo(selectedItems[0]).filePath().toStdString();

    // Display file in the inspector
    if (FileSystem::IsEngineMaterialFile(filePath))
    {
        m_inspector->InspectMaterialFile(filePath);
    }

    // Load scene
    if (FileSystem::IsEngineSceneFile(filePath))
    {
        m_inspector->GetContext()->GetSubsystem<Scene>()->LoadFromFile(filePath);
    }
}

void DirectusFileExplorer::dragEnterEvent(QDragEnterEvent* event)
{
    event->accept();
}

void DirectusFileExplorer::dragMoveEvent(QDragMoveEvent* event)
{
    event->accept();
}

void DirectusFileExplorer::dropEvent(QDropEvent* event)
{ 
    const QMimeData* mimeData = event->mimeData();

    if (!mimeData)
        return;

     //= DROP CASE: FILE/DIRECTORY ======================================================================
    QList<QUrl> droppedUrls = mimeData->urls();
    for(const auto& droppedURL : droppedUrls)
    {
        QString localPath = droppedURL.toLocalFile();
        QFileInfo fileInfo(localPath);
        string filePath = fileInfo.filePath().toStdString();

        //= DROP CASE: DIRECTORY ==========================
        if (fileInfo.isDir()) // The user dropped a folder
        {
            // Pass the directory as a model filepath and the engine
            // will figure out if there is a model in there
            m_fileDialog->OpenModeImmediatly(filePath);

            event->acceptProposedAction();
            return;
        }

        // DROP CASE: FILE ==================================
        if(fileInfo.isFile()) // The user dropped a file
        {
            // Model ?
            if (FileSystem::IsSupportedModelFile(filePath))
            {
                m_fileDialog->OpenModeImmediatly(filePath);

                event->acceptProposedAction();
                return;
            }

            // Audio ?
            if (FileSystem::IsSupportedAudioFile(filePath))
            {
                string fileName = FileSystem::GetFileNameFromFilePath(filePath);
                string destinationPath = GetRootPath().toStdString() + "/" + fileName;
                FileSystem::CopyFileFromTo(filePath, destinationPath);

                event->acceptProposedAction();
                return;
            }

            // Image ?
            if (FileSystem::IsSupportedImageFile(filePath))
            {
                string fileName = FileSystem::GetFileNameFromFilePath(filePath);
                string destinationPath = GetRootPath().toStdString() + "/" + fileName;
                FileSystem::CopyFileFromTo(filePath, destinationPath);

                event->acceptProposedAction();
                return;
            }
        }

        //= DROP CASE: GAMEOBJECT ===========================================================================
        unsigned int gameObjectID = stoi(mimeData->text().toStdString());
        auto gameObject = m_directusViewport->GetEngineContext()->GetSubsystem<Scene>()->GetGameObjectByID(gameObjectID);
        if (!gameObject.expired())
        {
            // Save the dropped GameObject as a prefab
            gameObject._Get()->SaveAsPrefab(GetRootPath().toStdString() + "/" + gameObject._Get()->GetName());

            event->acceptProposedAction();
            return;
        }
    }
}

void DirectusFileExplorer::SetRootDirectory(const string& directory)
{
    QString root = QString::fromStdString(directory);

    // Set the root path
    m_fileModel->setRootPath(root);

    // Set the model to the tree view
    this->setModel(m_fileModel);

    // I must set the path manually as the tree view
    // (at least visually) refuses to change anything.
    QModelIndex index = m_fileModel->index(root);
    this->setRootIndex(index);
}

QString DirectusFileExplorer::GetRootPath()
{
    return m_fileModel->filePath(rootIndex());
}

QString DirectusFileExplorer::GetSelectionPath()
{
    auto indices = this->selectedIndexes();
    return indices.size() != 0 ? m_fileModel->filePath(indices[0]) : "";
}
//===================================================================================================

// CONTEXT MENU =====================================================================================
void DirectusFileExplorer::ShowContextMenu(QPoint pos)
{
    QMenu contextMenu(tr("Context menu"), this);

    QMenu actionCreate("Create", this);
    actionCreate.setEnabled(true);

    //= actionCreate MENU =========================
    QAction actionCreateFolder("Folder", this);
    actionCreateFolder.setEnabled(true);

    QAction actionCreateMaterial("Material", this);
    actionCreateMaterial.setEnabled(true);

    actionCreate.addAction(&actionCreateFolder);
    actionCreate.addSeparator();
    actionCreate.addAction(&actionCreateMaterial);
    //=============================================

    QAction actionShowInExplorer("Show in Explorer", this);
    actionShowInExplorer.setEnabled(true);

    QAction actionOpen("Open", this);
    actionOpen.setEnabled(false);

    QAction actionDelete("Delete", this);
    actionDelete.setEnabled(true);

    QAction actionRename("Rename", this);
    actionRename.setEnabled(true);

    //= SIGNAL - SLOT connections ==================================================================
    connect(&actionCreateFolder,        SIGNAL(triggered()), this,  SLOT(CreateDirectory_()));
    connect(&actionCreateMaterial,      SIGNAL(triggered()), this,  SLOT(CreateMaterial()));
    connect(&actionShowInExplorer,      SIGNAL(triggered()), this,  SLOT(ShowRootPathInExplorer()));
    connect(&actionDelete,              SIGNAL(triggered()), this,  SLOT(DeleteSelectedFile()));
    connect(&actionRename,              SIGNAL(triggered()), this,  SLOT(RenameSelectedItem()));
    //==============================================================================================

    contextMenu.addMenu(&actionCreate);
    contextMenu.addAction(&actionShowInExplorer);
    contextMenu.addAction(&actionOpen);
    contextMenu.addAction(&actionDelete);
    contextMenu.addAction(&actionRename);

    contextMenu.exec(QCursor::pos());
}

void DirectusFileExplorer::RenameSelectedItem()
{
    QModelIndexList selectionList = this->selectedIndexes();
    QModelIndex selectedItem = selectionList.first();

    // TODO: Implement actual renaming on the item
}

void DirectusFileExplorer::DoubleClick(QModelIndex modelIndex)
{
    std::string selectedItemPath = m_fileModel->fileInfo(modelIndex).absoluteFilePath().toStdString();
    // If the user double click on a folder, open this directory
    if (m_fileModel->fileInfo(modelIndex).isDir())
    {
        this->SetRootPath(QString::fromStdString(selectedItemPath));
        return;
    }

    // If the user double clicked on a engine model file, load it
    if (FileSystem::IsEngineModelFile(selectedItemPath))
    {
        m_fileDialog->OpenModeImmediatly(selectedItemPath);
    }
}

void DirectusFileExplorer::CreateDirectory_()
{
    FileSystem::CreateDirectory_(GetRootPath().toStdString() + "/NewFolder");
}

void DirectusFileExplorer::CreateMaterial()
{
    string materialName = "NewMaterial";
    auto material = make_shared<Material>(m_directusViewport->GetEngineContext());
    material->SetResourceName(materialName);
    material->SaveToFile(GetRootPath().toStdString() + "/" + materialName);
}

void DirectusFileExplorer::ShowRootPathInExplorer()
{
    QString path = GetRootPath();
    QStringList args;
    args << "/select," << QDir::toNativeSeparators(path);
    QProcess *process = new QProcess(this);
    process->start("explorer.exe", args);
}

void DirectusFileExplorer::DeleteSelectedFile()
{
    if(!FileSystem::DeleteFile_(GetSelectionPath().toStdString()))
    {
        FileSystem::DeleteDirectory(GetSelectionPath().toStdString());
    }
}
//===================================================================================================
