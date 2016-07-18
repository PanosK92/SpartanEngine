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

//= INCLUDES ======================
#include "DirectusHierarchy.h"
#include <vector>
#include <QFileDialog>
#include "Components/Transform.h"
#include "IO/Log.h"
#include "DirectusQVariantPacker.h"
#include "DirectusAssetLoader.h"
#include <QApplication>
#include <QDrag>
//=================================

//= NAMESPACES =====
using namespace std;
//==================

#define NO_PATH "-1"

DirectusHierarchy::DirectusHierarchy(QWidget *parent) : QTreeWidget(parent)
{
    m_socket= nullptr;
    m_sceneFileName = NO_PATH;
    setAcceptDrops(true);
}

void DirectusHierarchy::SetDirectusCore(DirectusCore* directusCore)
{
    m_directusCore = directusCore;
    m_socket = m_directusCore->GetEngineSocket();

    Populate();
}

void DirectusHierarchy::Initialize(DirectusInspector* inspector, QWidget* mainWindow)
{
    m_inspector = inspector;
    m_mainWindow = mainWindow;
}

void DirectusHierarchy::mousePressEvent(QMouseEvent *event)
{
    // In case this mouse press evolves into a drag and drop
    // we have to keep the starting position in order to determine
    // if it's indeed one, in mouseMoveEvent(QMouseEvent* event)
    if (event->button() == Qt::LeftButton)
              m_dragStartPosition = event->pos();

    // I implement this myself because the QTreeWidget doesn't
    // deselect any items when you click anywhere else but on an item.
    QModelIndex item = indexAt(event->pos());
    QTreeWidget::mousePressEvent(event);
    if ((item.row() == -1 && item.column() == -1))
    {
        clearSelection();
        const QModelIndex index;
        selectionModel()->setCurrentIndex(index, QItemSelectionModel::Select);
    }
}

void DirectusHierarchy::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    QTreeWidget::selectionChanged(selected, deselected);

    if (m_inspector)
        m_inspector->Inspect(GetSelectedGameObject());
}

//= DRAG N DROP RELATED ============================================================================
// Determine whether a drag should begin, and
// construct a drag object to handle the operation.
void DirectusHierarchy::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton))
            return;

    if ((event->pos() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance())
            return;

    // Make sure the guy actually clicked on something
    GameObject* draggedGameObject = GetSelectedGameObject();
    if (!draggedGameObject)
        return;

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;

    QString gameObjectID = QString::fromStdString(draggedGameObject->GetID());
    mimeData->setText(gameObjectID);
    drag->setMimeData(mimeData);

    drag->exec();
}

// The dragEnterEvent() function is typically used to
// inform Qt about the types of data that the widget accepts.
void DirectusHierarchy::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->source() != this || !event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusHierarchy::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->source() != this || !event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();

    // Change the color of the hovered item
    /*QTreeWidgetItem* item = this->itemAt(event->pos());
    if (item)
        item->setBackgroundColor(0, QColor(0, 0, 200, 150));*/
    // NOTE: I need to find an efficient way to clear the color
    // from any previously hovered items.
}

// The dropEvent() is used to unpack dropped data and
// handle however I want.
void DirectusHierarchy::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    if (event->source() == this)
    {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    else
        event->acceptProposedAction();

    // Get the ID of the GameObject being dragged
    const QMimeData *mime = event->mimeData();
    std::string gameObjectID = mime->text().toStdString();

    // Get the dragged and the hovered GameObject
    GameObject* dragged = m_socket->GetGameObjectByID(gameObjectID);
    GameObject* hovered = TreeItemToGameObject(this->itemAt(event->pos()));

    // It was dropped on a gameobject
    if (dragged && hovered)
    {
        if (dragged->GetID() != hovered->GetID())
            dragged->GetTransform()->SetParent(hovered->GetTransform());

    }
    // It was dropped on nothing
    else if (dragged && !hovered)
    {
        dragged->GetTransform()->SetParent(nullptr);
    }

    Populate();
    return;
}
//===================================================================================================
void DirectusHierarchy::AddRoot(QTreeWidgetItem* item)
{
    this->addTopLevelItem(item);
}

void DirectusHierarchy::AddChild(QTreeWidgetItem* parent, QTreeWidgetItem* child)
{
    parent->addChild(child);
}

// Adds a gameobject, including any children, to the tree.
// NOTE: You probably want to pass root gameobjects here.
void DirectusHierarchy::AddGameObject(GameObject* gameobject, QTreeWidgetItem* parent)
{
    if (!gameobject)
        return;

    // Convert GameObject to QTreeWidgetItem
    QTreeWidgetItem* item = GameObjectToTreeItem(gameobject);

    // Add it to the tree
    if (gameobject->GetTransform()->IsRoot()) // This is a root gameobject
    {
        AddRoot(item);
    }
    else // This is a child gameobject
    {
        if (parent)
        {
            QTreeWidgetItem* child = item;
            AddChild(parent, child);
        }
    }

    // Do the same (recursively) for any children
    vector<Transform*> children = gameobject->GetTransform()->GetChildren();
    for(int i = 0; i < children.size(); i++)
    {
        GameObject* child = children[i]->GetGameObject();
        if (!child->IsVisibleInHierarchy())
            continue;

        AddGameObject(child, item);
    }
}

// Converts a QTreeWidgetItem to a GameObject
GameObject* DirectusHierarchy::TreeItemToGameObject(QTreeWidgetItem* treeItem)
{
    if (!treeItem)
        return nullptr;

    QVariant data = treeItem->data(0, Qt::UserRole);
    GameObject* gameObject = VPtr<GameObject>::asPtr(data);

    return gameObject;
}

// Converts a GameObject to a QTreeWidgetItem
QTreeWidgetItem* DirectusHierarchy::GameObjectToTreeItem(GameObject* gameobject)
{
    if (!gameobject)
        return nullptr;

    // Get data from the GameObject
    QString name = QString::fromStdString(gameobject->GetName());
    bool isRoot = gameobject->GetTransform()->IsRoot();

    // Create a tree item
    QTreeWidgetItem* item = isRoot ? new QTreeWidgetItem(this) : new QTreeWidgetItem();
    item->setText(0, name);
    item->setData(0, Qt::UserRole, VPtr<GameObject>::asQVariant(gameobject));

    //= About Qt::UserRole ==============================================================
    // Constant     -> Qt::UserRole
    // Value        -> 0x0100
    // Description  -> The first role that can be used for application-specific purposes.
    //===================================================================================

    item->setFlags(item->flags() | Qt::ItemIsEditable);

    return item;
}

// Returns the currently selected item
QTreeWidgetItem *DirectusHierarchy::GetSelectedItem()
{
    QList<QTreeWidgetItem*> selectedItems = this->selectedItems();
    if (selectedItems.count() == 0)
        return nullptr;

    QTreeWidgetItem* item = selectedItems[0];

    return item;
}

// Returns the currently selected item as a GameObject pointer
GameObject *DirectusHierarchy::GetSelectedGameObject()
{
    QTreeWidgetItem* item = GetSelectedItem();

    if (!item)
        return nullptr;

    GameObject* gameobject = TreeItemToGameObject(item);

    return gameobject;
}

bool DirectusHierarchy::IsAnyGameObjectSelected()
{
    return GetSelectedGameObject() ? true : false;
}

//= SLOTS ===============================================
void DirectusHierarchy::Clear()
{
    // Clears the tree (purely visual)
    clear();

    // Clear the inspector
    //if (m_inspector)
       // m_inspector->Inspect(nullptr);

    //if (clearEngine)
        //m_socket->ClearScene();
}

void DirectusHierarchy::Populate()
{
    Clear();

    if (!m_socket)
        return;

    vector<GameObject*> gameObjects = m_socket->GetRootGameObjects();
    for (int i = 0; i < gameObjects.size(); i++)
            AddGameObject(gameObjects[i], nullptr);

     m_directusCore->Update();
}

void DirectusHierarchy::CreateEmptyGameObject()
{
    GameObject* gameobject = new GameObject();
    Transform* transform = gameobject->GetTransform();

    GameObject* selectedGameObject = GetSelectedGameObject();
    if (selectedGameObject)
        transform->SetParent(selectedGameObject->GetTransform());

    Populate();
}

void DirectusHierarchy::NewScene()
{
    m_sceneFileName = NO_PATH;
    m_socket->ClearScene();

    Populate();
}

void DirectusHierarchy::OpenScene()
{
    // Display and open file dialog, aqcuire a file name
    QString title = "Load Scene";
    QString filter = "All files (*.dss)";
    QString dir = "Assets";
    m_sceneFileName = QFileDialog::getOpenFileName(this, title, dir, filter);

    // Create an asset loader and enable a progress bar dialog
    DirectusAssetLoader* sceneLoader = new DirectusAssetLoader();  
    sceneLoader->PrepareForScene(m_sceneFileName.toStdString(), m_socket);
    sceneLoader->EnableProgressBar(m_mainWindow);

    // Move the scene loader to the newly created thread
    QThread* thread = new QThread();
    sceneLoader->moveToThread(thread);

    // Emit a signal and clear the hierarchy
    emit SceneLoadingStarted();
    //Clear();

    // Connect all the necessery signals to their slots
    connect(thread,         SIGNAL(started()),  sceneLoader,    SLOT(LoadScene()));
    connect(sceneLoader,    SIGNAL(Finished()), this,           SLOT(Populate()));
    connect(sceneLoader,    SIGNAL(Finished()), thread,         SLOT(quit()));
    connect(sceneLoader,    SIGNAL(Finished()), sceneLoader,    SLOT(deleteLater()));
    connect(thread,         SIGNAL(finished()), m_directusCore, SLOT(Update()));
    connect(thread,         SIGNAL(finished()), thread,         SLOT(deleteLater()));

    // Start the thread
    thread->start(QThread::HighestPriority);
}

void DirectusHierarchy::SaveScene()
{
    if (m_sceneFileName == NO_PATH)
    {
        SaveSceneAs();
        return;
    }

    QThread* thread = new QThread();
    DirectusAssetLoader* sceneLoader = new DirectusAssetLoader();

    sceneLoader->moveToThread(thread);
    sceneLoader->PrepareForScene(m_sceneFileName.toStdString(), m_socket);

    connect(thread,         SIGNAL(started()),  sceneLoader,    SLOT(SaveScene()));
    connect(sceneLoader,    SIGNAL(Finished()), thread,         SLOT(quit()));
    connect(sceneLoader,    SIGNAL(Finished()), sceneLoader,    SLOT(deleteLater()));
    connect(thread,         SIGNAL(finished()), thread,         SLOT(deleteLater()));

    thread->start(QThread::HighestPriority);
}

void DirectusHierarchy::SaveSceneAs()
{
    QString title = "Save Scene";
    QString filter = "All files (*.dss)";
    QString dir = "Assets";
    m_sceneFileName = QFileDialog::getSaveFileName(
                this,
                title,
                dir,
                filter
                );

    QThread* thread = new QThread();
    DirectusAssetLoader* sceneLoader = new DirectusAssetLoader();

    sceneLoader->moveToThread(thread);
    sceneLoader->PrepareForScene(m_sceneFileName.toStdString(), m_socket);

    connect(thread,         SIGNAL(started()), sceneLoader,     SLOT(SaveScene()));
    connect(sceneLoader,    SIGNAL(Finished()), thread,         SLOT(quit()));
    connect(sceneLoader,    SIGNAL(Finished()), sceneLoader,    SLOT(deleteLater()));
    connect(thread,         SIGNAL(finished()), thread,         SLOT(deleteLater()));

    thread->start(QThread::HighestPriority);
}

void DirectusHierarchy::LoadModel()
{
    // Display and open file dialog, aqcuire a file name
    QString title = "Load model";
    QString filter = "All models (*.3ds; *.obj; *.fbx; *.blend; *.dae; *.lwo; *.c4d)";
    QString dir = "Assets";
    QString filePath = QFileDialog::getOpenFileName(this, title, dir, filter);

    // Create an asset loader and enable a progress bar dialog
    DirectusAssetLoader* modelLoader = new DirectusAssetLoader();
    modelLoader->PrepareForModel(filePath.toStdString(), m_socket);
    modelLoader->EnableProgressBar(m_mainWindow);

    // Move the model loader to the newly created thread
    QThread* thread = new QThread();
    modelLoader->moveToThread(thread);

    // Emit a signal
    emit ModelLoadingStarted();

    connect(thread,         SIGNAL(started()),  modelLoader,    SLOT(LoadModel()));
    connect(modelLoader,    SIGNAL(Finished()), this,           SLOT(Populate()));
    connect(modelLoader,    SIGNAL(Finished()), thread,         SLOT(quit()));
    connect(modelLoader,    SIGNAL(Finished()), modelLoader,    SLOT(deleteLater()));
    connect(thread,         SIGNAL(finished()), m_directusCore, SLOT(Update()));
    connect(thread,         SIGNAL(finished()), thread,         SLOT(deleteLater()));

    thread->start(QThread::HighestPriority);
}
//========================================================
