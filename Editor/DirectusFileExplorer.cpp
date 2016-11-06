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
    QString root = "Assets";
    setAcceptDrops(true);

    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setFilter(QDir::Files | QDir::AllDirs | QDir::NoDotAndDotDot); // Set a filter that displays only folders
    m_fileModel->setRootPath(root);  // Set the root path

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
    QModelIndex index = m_fileModel->index("Assets");
    this->setRootIndex(index);

    // Context menu
    connect(this, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(ShowContextMenu(QPoint)));
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
    // In case this mouse press is the future start point of a drag n drop event,
    // we have to keep the starting position in order to calculate the delta
    // and determine if there has been any dragging, in mouseMoveEvent(QMouseEvent* event)

    if (event->button() == Qt::LeftButton)
        m_dragStartPosition = event->pos();

    if(event->button() == Qt::RightButton)
        emit customContextMenuRequested(event->pos());

    QListView::mousePressEvent(event);
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
    mimeData->setText(filePath);
    drag->setMimeData(mimeData);

    drag->exec();
}

void DirectusFileExplorer::mouseReleaseEvent(QMouseEvent* event)
{
    // Clear the inspector.
    m_hierarchy->clearSelection();
    m_inspector->Clear();

    // See if anything was actually clicked,
    QModelIndexList selectedItems = this->selectionModel()->selectedIndexes();
    if (selectedItems.empty())
        return;

    // If something was indeed clicked, get it's path.
    QString filePath = m_fileModel->fileInfo(selectedItems[0]).filePath();

    // If so, determine what type of file that was, and display it in the inspector (if possible).
    if (FileSystem::IsMaterialFile(filePath.toStdString()))
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
    QList<QUrl> droppedUrls = event->mimeData()->urls();
    int droppedUrlCnt = droppedUrls.size();
    for(int i = 0; i < droppedUrlCnt; i++)
    {
        QString localPath = droppedUrls[i].toLocalFile();
        QFileInfo fileInfo(localPath);

        string filePath = fileInfo.filePath().toStdString();
        if(fileInfo.isFile()) // The user dropped a file
        {
            if (FileSystem::IsSupportedModel(filePath))
                m_fileDialog->LoadModelDirectly(filePath);
        }
        else if (fileInfo.isDir()) // The user dropped a folder
        {
            vector<string> modelFilePaths = FileSystem::GetSupportedModelsInDirectory(filePath);
            if (modelFilePaths.size() != 0)
                m_fileDialog->LoadModelDirectly(modelFilePaths.front());
        }
    }

    event->acceptProposedAction();
}
//===================================================================================================

// CONTEXT MENU =====================================================================================
void DirectusFileExplorer::ShowContextMenu(QPoint pos)
{
    QMenu contextMenu(tr("Context menu"), this);

    QMenu actionCreate("Create", this);
    actionCreate.setEnabled(false);

    QAction menuShowInExplorer("Show in Explorer", this);
    menuShowInExplorer.setEnabled(false);

    QAction actionOpen("Open", this);
    actionOpen.setEnabled(false);

    QAction actionDelete("Delete", this);
    actionDelete.setEnabled(false);

    QAction actionOpenSceneAdditive("Open Scene Additive", this);
    actionOpenSceneAdditive.setEnabled(false);

    QAction actionImportNewAsset("Import New Asset...", this);
    actionImportNewAsset.setEnabled(false);

    contextMenu.addMenu(&actionCreate);
    contextMenu.addAction(&menuShowInExplorer);
    contextMenu.addAction(&actionOpen);
    contextMenu.addAction(&actionDelete);
    contextMenu.addSeparator();
    contextMenu.addAction(&actionOpenSceneAdditive);
    contextMenu.addSeparator();
    contextMenu.addAction(&actionImportNewAsset);

    contextMenu.exec(QCursor::pos());
}

void DirectusFileExplorer::DoubleClick(QModelIndex modelIndex)
{
    if (m_fileModel->fileInfo(modelIndex).isDir())
    {
        QString path = m_fileModel->fileInfo(modelIndex).absoluteFilePath();
        this->SetRootPath(path);
    }
}
//===================================================================================================
