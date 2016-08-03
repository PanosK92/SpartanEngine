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
#include <QMenu>
#include "Components/Light.h"
#include "Components/Hinge.h"
#include "Components/MeshFilter.h"
#include "IO/FileHelper.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

DirectusHierarchy::DirectusHierarchy(QWidget *parent) : QTreeWidget(parent)
{
    m_socket= nullptr;

    // QTreeWidget properties
    this->setAcceptDrops(true);
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    // Connect context menu signal with the construction of a custom one
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(ShowContextMenu(const QPoint&)));
    connect(this, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(RenameSelected()));
}

void DirectusHierarchy::Initialize(DirectusInspector* inspector, QWidget* mainWindow, DirectusCore* directusCore)
{
    m_directusCore = directusCore;
    m_socket = m_directusCore->GetEngineSocket();
    m_inspector = inspector;
    m_mainWindow = mainWindow;

    m_fileDialog = new DirectusFileDialog(mainWindow);
    m_fileDialog->Initialize(m_mainWindow, m_directusCore);
    connect(m_fileDialog, SIGNAL(AssetLoaded()), this, SLOT(Populate()));

    Populate();
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
// handle them however I want.
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
    GameObject* hovered = ToGameObject(this->itemAt(event->pos()));

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

    if (!gameobject->IsVisibleInHierarchy())
        return;

    // Convert GameObject to QTreeWidgetItem
    QTreeWidgetItem* item = ToQTreeWidgetItem(gameobject);

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
GameObject* DirectusHierarchy::ToGameObject(QTreeWidgetItem* treeItem)
{
    if (!treeItem)
        return nullptr;

    QVariant data = treeItem->data(0, Qt::UserRole);
    GameObject* gameObject = VPtr<GameObject>::asPtr(data);

    return gameObject;
}

// Converts a GameObject to a QTreeWidgetItem
QTreeWidgetItem* DirectusHierarchy::ToQTreeWidgetItem(GameObject* gameobject)
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
QTreeWidgetItem *DirectusHierarchy::GetSelectedQTreeWidgetItem()
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
    QTreeWidgetItem* item = GetSelectedQTreeWidgetItem();

    if (!item)
        return nullptr;

    GameObject* gameobject = ToGameObject(item);

    return gameobject;
}

bool DirectusHierarchy::IsAnyGameObjectSelected()
{
    return GetSelectedGameObject() ? true : false;
}

//= SLOTS ===============================================
void DirectusHierarchy::ClearTree()
{
    // Clears the tree (purely visual)
    clear();
}

void DirectusHierarchy::Populate()
{
    ClearTree();

    if (!m_socket)
        return;

    vector<GameObject*> gameObjects = m_socket->GetRootGameObjects();
    for (int i = 0; i < gameObjects.size(); i++)
            AddGameObject(gameObjects[i], nullptr);

     m_directusCore->Update();
}

void DirectusHierarchy::NewScene()
{
    m_fileDialog->ResetFilePath();
    m_socket->ClearScene();

    Populate();
}

void DirectusHierarchy::OpenScene()
{
    m_fileDialog->LoadScene();
}

void DirectusHierarchy::SaveScene()
{
    if (m_fileDialog->FilePathExists())
        m_fileDialog->SaveScene();
    else
        m_fileDialog->SaveSceneAs();
}

void DirectusHierarchy::SaveSceneAs()
{
    m_fileDialog->SaveSceneAs();
}

void DirectusHierarchy::LoadModel()
{
    m_fileDialog->LoadModel();
}

void DirectusHierarchy::ShowContextMenu(const QPoint &pos)
{
    bool selected = IsAnyGameObjectSelected();

    QMenu contextMenu(tr("Context menu"), this);

    //= ACTIONS ======================================
    QAction actionCopy("Copy", this);
    actionCopy.setEnabled(false);

    QAction actionPaste("Paste", this);
    actionPaste.setEnabled(false);

    QAction actionRename("Rename", this);
    actionRename.setEnabled(selected);

    QAction actionDuplicate("Duplicate", this);
    actionDuplicate.setEnabled(false);

    QAction actionDelete("Delete", this);
    actionDelete.setEnabled(selected);

    QAction actionCreateEmpty("Create Empty", this);
    actionCreateEmpty.setEnabled(true);

    //= 3D Object =======================================
    QMenu menu3DObject("3D Object", this);
    menu3DObject.setEnabled(true);

    QAction actionCube("Cube", this);
    actionCube.setEnabled(true);

    QAction actionQuad("Quad", this);
    actionCube.setEnabled(true);

    menu3DObject.addAction(&actionCube);
    menu3DObject.addAction(&actionQuad);
    //====================================================

     //= LIGHT ===========================================
    QMenu menuLight("Light", this);
    menuLight.setEnabled(true);

    QAction actionDirectionalLight("Directional Light", this);
    actionDirectionalLight.setEnabled(true);

    QAction actionPointLight("Point Light", this);
    actionPointLight.setEnabled(true);

    menuLight.addAction(&actionDirectionalLight);
    menuLight.addAction(&actionPointLight);
    //====================================================

    QAction actionCamera("Camera", this);
    actionCamera.setEnabled(true);
    //=================================================

    //= SIGNAL - SLOT connections =========================================================
    connect(&actionRename,              SIGNAL(triggered()), this,  SLOT(RenameSelected()));
    connect(&actionDelete,              SIGNAL(triggered()), this,  SLOT(DeleteSelected()));
    connect(&actionCreateEmpty,         SIGNAL(triggered()), this,  SLOT(CreateEmptyGameObject()));
    connect(&actionCube,                SIGNAL(triggered()), this,  SLOT(CreateCube()));
    connect(&actionQuad,                SIGNAL(triggered()), this,  SLOT(CreateQuad()));
    connect(&actionDirectionalLight,    SIGNAL(triggered()), this,  SLOT(CreateDirectionalLight()));
    connect(&actionPointLight,          SIGNAL(triggered()), this,  SLOT(CreatePointLight()));
    connect(&actionCamera,              SIGNAL(triggered()), this,  SLOT(CreateCamera()));
    //=====================================================================================

    contextMenu.addAction(&actionCopy);
    contextMenu.addAction(&actionPaste);
    contextMenu.addSeparator();
    contextMenu.addAction(&actionRename);
    contextMenu.addAction(&actionDuplicate);
    contextMenu.addAction(&actionDelete);
    contextMenu.addSeparator();
    contextMenu.addAction(&actionCreateEmpty);
    contextMenu.addSeparator();
    contextMenu.addMenu(&menu3DObject);
    contextMenu.addMenu(&menuLight);
    contextMenu.addAction(&actionCamera);

    contextMenu.exec(mapToGlobal(pos));
}

void DirectusHierarchy::ShowContextMenuLight()
{
    bool selected = IsAnyGameObjectSelected();

    QMenu contextMenu(tr("Context menu"), this);

    QAction actionCreateEmpty("Create Empty", this);
    actionCreateEmpty.setEnabled(true);

    //= 3D Object =======================================
    QMenu menu3DObject("3D Object", this);
    menu3DObject.setEnabled(true);

    QAction actionCube("Cube", this);
    actionCube.setEnabled(true);

    QAction actionQuad("Quad", this);
    actionCube.setEnabled(true);

    menu3DObject.addAction(&actionCube);
    menu3DObject.addAction(&actionQuad);
    //====================================================

     //= LIGHT ===========================================
    QMenu menuLight("Light", this);
    menuLight.setEnabled(true);

    QAction actionDirectionalLight("Directional Light", this);
    actionDirectionalLight.setEnabled(true);

    QAction actionPointLight("Point Light", this);
    actionPointLight.setEnabled(true);

    menuLight.addAction(&actionDirectionalLight);
    menuLight.addAction(&actionPointLight);
    //====================================================

    QAction actionCamera("Camera", this);
    actionCamera.setEnabled(true);
    //=================================================

    //= SIGNAL - SLOT connections =========================================================
    connect(&actionCreateEmpty,         SIGNAL(triggered()), this,  SLOT(CreateEmptyGameObject()));
    connect(&actionCube,                SIGNAL(triggered()), this,  SLOT(CreateCube()));
    connect(&actionQuad,                SIGNAL(triggered()), this,  SLOT(CreateQuad()));
    connect(&actionDirectionalLight,    SIGNAL(triggered()), this,  SLOT(CreateDirectionalLight()));
    connect(&actionPointLight,          SIGNAL(triggered()), this,  SLOT(CreatePointLight()));
    connect(&actionCamera,              SIGNAL(triggered()), this,  SLOT(CreateCamera()));
    //=====================================================================================

    contextMenu.addAction(&actionCreateEmpty);
    contextMenu.addMenu(&menu3DObject);
    contextMenu.addMenu(&menuLight);
    contextMenu.addAction(&actionCamera);

    contextMenu.exec(QCursor::pos());
}

// Called when the user click rename from the context menu
void DirectusHierarchy::RenameSelected()
{
    // Get the currently selected item
    QTreeWidgetItem* item = GetSelectedQTreeWidgetItem();
    GameObject* gameObject = GetSelectedGameObject();

    if (!gameObject)
        return;

    // If this action is called by the tree itself,
    // the item will already be in edit mode (double clicked)
    if (this->state() != QTreeWidget::EditingState)
        this->editItem(item);

    // Set the name of the gameobject from the item
    string name = item->text(0).toStdString();
    gameObject->SetName(name);
}

void DirectusHierarchy::DeleteSelected()
{
    // Get the currently selected GameObject
    GameObject* gameObject = GetSelectedGameObject();
    if (!gameObject)
        return;

    // Delete it
    m_socket->DestroyGameObject(gameObject);

    // Refresh the hierarchy
    Populate();
}
//========================================================

//= GAMEOBJECT ADDITIONS =================================
void DirectusHierarchy::CreateEmptyGameObject()
{
    // Create an empty GameObject and get it's Transform
    GameObject* gameobject = new GameObject();
    Transform* transform = gameobject->GetTransform();

    // Make it a child of the selected GameObject (if there is one)
    GameObject* selectedGameObject = GetSelectedGameObject();
    if (selectedGameObject)
        transform->SetParent(selectedGameObject->GetTransform());

    // Refresh the hierarchy
    Populate();
}

void DirectusHierarchy::CreateEmptyGameObjectRoot()
{
    // No worries about memory leaks, all GameObjects
    // are managed by the engine.
    new GameObject();

    // Refresh the hierarchy
    Populate();
}

void DirectusHierarchy::CreateCube()
{
    // Create GameObject
    GameObject* gameobject = new GameObject();
    gameobject->SetName("Cube");

    // Add a mesh component
    MeshFilter* meshComp = gameobject->AddComponent<MeshFilter>();
    meshComp->CreateCube();

    // Add a mesh renderer
    MeshRenderer* meshRendererComp = gameobject->AddComponent<MeshRenderer>();
    meshRendererComp->SetMaterialStandardDefault();

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreateQuad()
{
    // Create GameObject
    GameObject* gameobject = new GameObject();
    gameobject->SetName("Quad");

    // Add a mesh component
    MeshFilter* meshComp = gameobject->AddComponent<MeshFilter>();
    meshComp->CreateQuad();

    // Add a mesh renderer
    MeshRenderer* meshRenderer = gameobject->AddComponent<MeshRenderer>();
    meshRenderer->SetMaterialStandardDefault();

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreateDirectionalLight()
{
    // Create GameObject
    GameObject* gameobject = new GameObject();
    gameobject->SetName("Directional light");

    // Add component
    Light* light = gameobject->AddComponent<Light>();
    light->SetLightType(Directional);

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreatePointLight()
{
    // Create GameObject
    GameObject* gameobject = new GameObject();
    gameobject->SetName("Point light");

    // Add component
    Light* light = gameobject->AddComponent<Light>();
    light->SetLightType(Point);

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreateCamera()
{
    // Create GameObject
    GameObject* gameobject = new GameObject();
    gameobject->SetName("Camera");

    // Add component
    Camera* light = gameobject->AddComponent<Camera>();

    // Refresh hierarchy
    Populate();
}
//========================================================

//= COMPONENT ADDITIONS ==================================
void DirectusHierarchy::AddCameraComponent()
{
    // Get the currently selected GameObject
    GameObject* gameobject = GetSelectedGameObject();
    if (!gameobject)
        return;

    gameobject->AddComponent<Camera>();

    // Update the engine and the inspector
    m_directusCore->Update();
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddMeshFilterComponent()
{
    // Get the currently selected GameObject
    GameObject* gameobject = GetSelectedGameObject();
    if (!gameobject)
        return;

    gameobject->AddComponent<MeshFilter>();

    // Update the engine and the inspector
    m_directusCore->Update();
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddMeshRendererComponent()
{
    // Get the currently selected GameObject
    GameObject* gameobject = GetSelectedGameObject();
    if (!gameobject)
        return;

    gameobject->AddComponent<MeshRenderer>();

    // Update the engine and the inspector
    m_directusCore->Update();
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddLightComponent()
{
    // Get the currently selected GameObject
    GameObject* gameobject = GetSelectedGameObject();
    if (!gameobject)
        return;

    gameobject->AddComponent<Light>();

    // Update the engine and the inspector
    m_directusCore->Update();
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddRigidBodyComponent()
{
    // Get the currently selected GameObject
    GameObject* gameobject = GetSelectedGameObject();
    if (!gameobject)
        return;

    gameobject->AddComponent<RigidBody>();

    // Update the engine and the inspector
    m_directusCore->Update();
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddColliderComponent()
{
    // Get the currently selected GameObject
    GameObject* gameobject = GetSelectedGameObject();
    if (!gameobject)
        return;

    gameobject->AddComponent<Collider>();

    // Update the engine and the inspector
    m_directusCore->Update();
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddMeshColliderComponent()
{
    // Get the currently selected GameObject
    GameObject* gameobject = GetSelectedGameObject();
    if (!gameobject)
        return;

    gameobject->AddComponent<MeshCollider>();

    // Update the engine and the inspector
    m_directusCore->Update();
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddHingeComponent()
{
    // Get the currently selected GameObject
    GameObject* gameobject = GetSelectedGameObject();
    if (!gameobject)
        return;

    gameobject->AddComponent<Hinge>();

    // Update the engine and the inspector
    m_directusCore->Update();
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddSkyboxComponent()
{
    // Get the currently selected GameObject
    GameObject* gameobject = GetSelectedGameObject();
    if (!gameobject)
        return;

    gameobject->AddComponent<Skybox>();

    // Update the engine and the inspector
    m_directusCore->Update();
    m_inspector->Inspect(gameobject);
}
//========================================================
