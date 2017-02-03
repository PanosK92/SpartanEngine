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

//= INCLUDES ====================
#include "DirectusFileExplorer.h"
#include <QStandardItem>
#include <QMouseEvent>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include "Logging/Log.h"
#include <QMenu>
#include "FileSystem/FileSystem.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

DirectusFileExplorer::DirectusFileExplorer(QWidget *parent) : QListView(parent)
{

}

void DirectusFileExplorer::Initialize(
        QWidget* mainWindow,
        DirectusCore* directusCore,
        DirectusHierarchy* hierarchy,
        DirectusInspector* inspector
        )
{
    QString root = "Standard Assets";
    setAcceptDrops(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setFilter(QDir::Files | QDir::AllDirs | QDir::NoDotAndDotDot); // Set a filter that displays only folders
    m_fileModel->setRootPath(root);  // Set the root path

    m_directusCore = directusCore;
    m_hierarchy = hierarchy;
    m_inspector = inspector;

    // Set icon provider
    m_directusIconProvider = new DirectusIconProvider();
    m_directusIconProvider->Initialize();
    m_fileModel->setIconProvider(m_directusIconProvider);

    // Create file dialog
    m_fileDialog = new DirectusFileDialog();
    m_fileDialog->Initialize(mainWindow, hierarchy, directusCore);

    // Set the model to the tree view
    this->setModel(m_fileModel);

    // I must set the path manually as the tree view
    // (at least visually) refuses to change anything.
    QModelIndex index = m_fileModel->index(root);
    this->setRootIndex(index);

    // Context menu
    connect(this, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(ShowContextMenu(QPoint)));

    // Rename
    connect(this, SIGNAL(objectNameChanged(QString)), this, SLOT(RenameItem(QString)));

    // Double click
    connect(this, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(DoubleClick(QModelIndex)));
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
    filePath = QString::fromStdString(FileSystem::GetRelativePathFromAbsolutePath(filePath.toStdString()));
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
    QString filePath = m_fileModel->fileInfo(selectedItems[0]).filePath();

    // Determine what type of file that was, and display it in the inspector (if possible).
    if (FileSystem::IsSupportedMaterialFile(filePath.toStdString()))
        m_inspector->InspectMaterialFile(filePath.toStdString());
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

    //= DROP CASE: GAMEOBJECT ===========================================================================
    std::string gameObjectID = mimeData->text().toStdString();
    GameObject* gameObject = m_directusCore->GetEngineSocket()->GetGameObjectByID(gameObjectID);
    if (gameObject)
    {
        // Save the dropped GameObject as a prefab
        gameObject->SaveAsPrefab(GetRootPath().toStdString() + "/" + gameObject->GetName());
        event->acceptProposedAction();
        return;
    }
    //===================================================================================================

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
            vector<string> modelFilePaths = FileSystem::GetSupportedModelFilesInDirectory(filePath);
            if (modelFilePaths.size() != 0)
                m_fileDialog->LoadModelDirectly(modelFilePaths.front());
        }
        //= DROP CASE: FILE ==================================
        else if(fileInfo.isFile()) // The user dropped a file
        {
            // Model ?
            if (FileSystem::IsSupportedModelFile(filePath))
                m_fileDialog->LoadModelDirectly(filePath);

            // Audio ?
            if (FileSystem::IsSupportedAudioFile(filePath))
            {
                std::string fileName = FileSystem::GetFileNameFromPath(filePath);
                std::string destinationPath = GetRootPath().toStdString() + "/" + fileName;
                FileSystem::CopyFileFromTo(filePath, destinationPath);
                return;
            }

            // Image ?
            if (FileSystem::IsSupportedImageFile(filePath))
            {
                std::string fileName = FileSystem::GetFileNameFromPath(filePath);
                std::string destinationPath = GetRootPath().toStdString() + "/" + fileName;
                FileSystem::CopyFileFromTo(filePath, destinationPath);
            }
        }
        //===================================================

    }
    //===================================================================================================

    event->acceptProposedAction();
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

    QAction actionOpenSceneAdditive("Open Scene Additive", this);
    actionOpenSceneAdditive.setEnabled(false);

    QAction actionImportNewAsset("Import New Asset...", this);
    actionImportNewAsset.setEnabled(false);

    //= SIGNAL - SLOT connections ==================================================================
    connect(&actionCreateFolder,        SIGNAL(triggered()), this,  SLOT(CreateDirectory_()));
    connect(&actionCreateMaterial,      SIGNAL(triggered()), this,  SLOT(CreateMaterial()));
    connect(&actionShowInExplorer,      SIGNAL(triggered()), this,  SLOT(ShowRootPathInExplorer()));
    connect(&actionDelete,              SIGNAL(triggered()), this,  SLOT(DeleteSelectedFile()));
    //==============================================================================================

    contextMenu.addMenu(&actionCreate);
    contextMenu.addAction(&actionShowInExplorer);
    contextMenu.addAction(&actionOpen);
    contextMenu.addAction(&actionDelete);
    contextMenu.addSeparator();
    contextMenu.addAction(&actionOpenSceneAdditive);
    contextMenu.addSeparator();
    contextMenu.addAction(&actionImportNewAsset);

    contextMenu.exec(QCursor::pos());
}

void DirectusFileExplorer::RenameItem(QString name)
{
    LOG_INFO("Triggered");
}

void DirectusFileExplorer::DoubleClick(QModelIndex modelIndex)
{
    if (m_fileModel->fileInfo(modelIndex).isDir())
    {
        QString path = m_fileModel->fileInfo(modelIndex).absoluteFilePath();
        this->SetRootPath(path);
    }
}

void DirectusFileExplorer::CreateDirectory_()
{
    FileSystem::CreateDirectory_(GetRootPath().toStdString() + "/NewFolder");
}

void DirectusFileExplorer::CreateMaterial()
{
    std::string materialName = "NewMaterial";
    auto material = std::make_shared<Material>(m_directusCore->GetEngineSocket()->GetContext());
    material->SetName(materialName);
    material->Save(GetRootPath().toStdString() + "/" + materialName, true);
}

void DirectusFileExplorer::ShowRootPathInExplorer()
{
    FileSystem::OpenDirectoryInExplorer(GetRootPath().toStdString());
}

void DirectusFileExplorer::DeleteSelectedFile()
{
    if(!FileSystem::DeleteFile_(GetSelectionPath().toStdString()))
        FileSystem::DeleteDirectory(GetSelectionPath().toStdString());
}
//===================================================================================================
