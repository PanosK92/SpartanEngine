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
#include "DirectusHierarchy.h"
#include "DirectusQVariantPacker.h"
#include "DirectusAssetLoader.h"
#include <QApplication>
#include <QDrag>
#include <QMenu>
#include <QFileDialog>
#include "FileSystem/FileSystem.h"
#include "Core/GameObject.h"
#include "Logging/Log.h"
#include "Components/AudioListener.h"
#include "Components/AudioSource.h"
#include "Components/Camera.h"
#include "Components/Collider.h"
#include "Components/Constraint.h"
#include "Components/Light.h"
#include "Components/MeshFilter.h"
#include "Components/MeshRenderer.h"
#include "Components/RigidBody.h"
#include "Components/Skybox.h"
#include "Components/Transform.h"
//==================================

//= NAMESPACES ==========
using namespace Directus;
using namespace std;
//=======================

DirectusHierarchy::DirectusHierarchy(QWidget* parent) : QTreeWidget(parent)
{
    m_context = nullptr;

    // Set QTreeWidget properties
    this->setAcceptDrops(true);
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    // Connect context menu signal with the construction of a custom one
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(ShowContextMenu(const QPoint&)));
    connect(this, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(RenameSelected()));
}

void DirectusHierarchy::Initialize(DirectusInspector* inspector, QWidget* mainWindow, DirectusViewport* directusViewport)
{
    m_directusViewport = directusViewport;
    m_context = m_directusViewport->GetEngineContext();
    m_inspector = inspector;
    m_mainWindow = mainWindow;

    m_fileDialog = make_unique<DirectusFileDialog>(mainWindow);
    m_fileDialog->Initialize(m_mainWindow, this, m_directusViewport);

    Populate();
}

void DirectusHierarchy::mousePressEvent(QMouseEvent* event)
{
    // Left click -> GameObject selection
    if (event->button() == Qt::LeftButton)
    {
        // save the position in order to be
        // try and determine later if that's a drag
        m_dragStartPosition = event->pos();

        // Make QTreeWidget deselect any items when
        // you click anywhere else but on an item.
        QModelIndex item = indexAt(event->pos());
        QTreeWidget::mousePressEvent(event);
        if ((item.row() == -1 && item.column() == -1))
        {
            clearSelection();
            const QModelIndex index;
            selectionModel()->setCurrentIndex(index, QItemSelectionModel::Select);
        }
    }

    // Right click -> context menu
    if(event->button() == Qt::RightButton)
    {
        clearSelection();
        selectionModel()->setCurrentIndex(indexAt(event->pos()), QItemSelectionModel::Select);
        emit customContextMenuRequested(event->pos());
    }
}

void DirectusHierarchy::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    // We use the existing event definition
    QTreeWidget::selectionChanged(selected, deselected);

    // We make sure the inspector displays the selected GameObject as well
    if (m_inspector)
    {
        m_inspector->Inspect(GetSelectedGameObject());
    }
}

//= DRAG N DROP RELATED ============================================================================
// Determine whether a drag should begin, and construct a drag object to handle the operation.
void DirectusHierarchy::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;

    if ((event->pos() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance())
        return;

    // Make sure the user actually clicked on something
    auto draggedGameObject = GetSelectedGameObject();
    if (draggedGameObject.expired())
        return;

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;

    QString gameObjectID = QString::fromStdString(GUIDGenerator::ToStr(draggedGameObject._Get()->GetID()));
    mimeData->setText(gameObjectID);
    drag->setMimeData(mimeData);

    drag->exec();
}

// The dragEnterEvent() function is typically used to
// inform Qt about the types of data that the widget accepts.
void DirectusHierarchy::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void DirectusHierarchy::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasText())
    {
        event->ignore();
        return;
    }

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

// The dropEvent() is used to unpack dropped data
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
    {
        event->acceptProposedAction();
    }

    // Get mime data and convert it's text to std::string
    const QMimeData* mime = event->mimeData();
    std::string text = mime->text().toStdString();

    //= DROP CASE: PREFAB (Assume text is a file path) ===============
    if (FileSystem::IsEnginePrefabFile(text))
    {
        auto gameObj = m_context->GetSubsystem<Scene>()->CreateGameObject();
        gameObj._Get()->LoadFromPrefab(text);
        Populate();
        return;
    }
    //================================================================

    //= DROP CASE: GAMEOBJECT (Assume text is a GameObject ID) =======
    auto draggedGameObj = m_context->GetSubsystem<Scene>()->GetGameObjectByID(GUIDGenerator::ToUnsignedInt(text))._Get();
    QTreeWidgetItem* hoveredItem = this->itemAt(event->pos());
    auto dropTargetGameObj = ToGameObject(hoveredItem).lock();

    if (draggedGameObj && dropTargetGameObj) // It was dropped on a gameobject
    {
        if (draggedGameObj->GetID() != dropTargetGameObj->GetID())
        {
            draggedGameObj->GetTransform()->SetParent(dropTargetGameObj->GetTransform());
            Populate();
        }

    }
    else if (draggedGameObj && !dropTargetGameObj) // It was dropped on nothing
    {
        draggedGameObj->GetTransform()->SetParent(nullptr);
        Populate();
    }   
    //================================================================
}

void DirectusHierarchy::keyPressEvent(QKeyEvent *event)
{
    if(event->key() == Qt::Key_Delete)
    {
        DeleteSelected();
    }
}

//===================================================================================================

//= HELPER FUNCTIONS ================================================================================
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
void DirectusHierarchy::AddGameObject(weak_ptr<GameObject> gameobject, QTreeWidgetItem* parent)
{
    if (gameobject.expired())
        return;

    auto gameObj = gameobject.lock();
    if (!gameObj->IsVisibleInHierarchy())
        return;

    // Convert GameObject to QTreeWidgetItem
    QTreeWidgetItem* item = ToQTreeWidgetItem(gameobject);

    // Add it to the tree
    if (gameObj->GetTransform()->IsRoot()) // This is a root gameobject
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
    auto childrenTrans = gameObj->GetTransform()->GetChildren();
    for (const auto& childTrans : childrenTrans)
    {
        auto child = childTrans->GetGameObject();
        if (!child.expired())
        {
            if (!child.lock()->IsVisibleInHierarchy())
                continue;
        }

        AddGameObject(child, item);
    }
}

// Converts a QTreeWidgetItem to a GameObject
weak_ptr<GameObject> DirectusHierarchy::ToGameObject(QTreeWidgetItem* treeItem)
{
    if (!treeItem)
        return weak_ptr<GameObject>();

    QVariant data = treeItem->data(0, Qt::UserRole);
    string idStr = data.value<QString>().toStdString();
    unsigned int id = GUIDGenerator::ToUnsignedInt(idStr);
    return m_context->GetSubsystem<Scene>()->GetGameObjectByID(id);
}

// Converts a GameObject to a QTreeWidgetItem
QTreeWidgetItem* DirectusHierarchy::ToQTreeWidgetItem(weak_ptr<GameObject> gameObj)
{
    if (gameObj.expired())
        return nullptr;

    // Get data from the GameObject
    QString name = QString::fromStdString(gameObj.lock()->GetName());
    bool isRoot = gameObj.lock()->GetTransform()->IsRoot();

    // Create a tree item
    QTreeWidgetItem* item = isRoot ? new QTreeWidgetItem(this) : new QTreeWidgetItem();
    item->setText(0, name);
    item->setData(0, Qt::UserRole, QVariant(QString::fromStdString(GUIDGenerator::ToStr(gameObj._Get()->GetID()))));

    //= About Qt::UserRole ==============================================================
    // Constant     -> Qt::UserRole
    // Value        -> 0x0100
    // Description  -> The first role that can be used for application-specific purposes.
    //===================================================================================

    item->setFlags(item->flags() | Qt::ItemIsEditable);

    return item;
}

// Returns the currently selected item
QTreeWidgetItem* DirectusHierarchy::GetSelectedQTreeWidgetItem()
{
    QList<QTreeWidgetItem*> selectedItems = this->selectedItems();
    if (selectedItems.count() == 0)
        return nullptr;

    QTreeWidgetItem* item = selectedItems[0];

    return item;
}

// Returns the currently selected item as a GameObject pointer
weak_ptr<GameObject> DirectusHierarchy::GetSelectedGameObject()
{
    QTreeWidgetItem* item = GetSelectedQTreeWidgetItem();
    return ToGameObject(item);
}

bool DirectusHierarchy::IsAnyGameObjectSelected()
{
    return !GetSelectedGameObject().expired();
}

void DirectusHierarchy::GetTreeItemDescendants(vector<QTreeWidgetItem*>* collection, QTreeWidgetItem* item)
{
    if (!item)
        return;

    for(int i = 0; i < item->childCount(); i++)
    {
        collection->push_back(item->child(i));
        GetTreeItemDescendants(collection, item->child(i));
    }
}

vector<QTreeWidgetItem*> DirectusHierarchy::GetAllTreeItems()
{
    vector<QTreeWidgetItem*> collection;

    for (int i = 0; i < topLevelItemCount(); i++)
    {
        QTreeWidgetItem* item = topLevelItem(i);
        collection.push_back(item);
        GetTreeItemDescendants(&collection, item);
    }

    return collection;
}

void DirectusHierarchy::ExpandTreeItemGoingUp(QTreeWidgetItem* item)
{
    QTreeWidgetItem* parent = item->parent();

    if (parent)
    {
        expandItem(parent);
        ExpandTreeItemGoingUp(parent);
    }
}
//========================================================================================

//= SLOTS ===============================================
void DirectusHierarchy::ClearTree()
{
    // Clears the tree (purely visual)
    clear();
}

void DirectusHierarchy::Populate()
{
    ClearTree();

    if (!m_context)
        return;

    auto gameObjects = m_context->GetSubsystem<Scene>()->GetRootGameObjects();
    for (const auto& gameObj : gameObjects)
    {
        AddGameObject(gameObj, nullptr);
    }
}

void DirectusHierarchy::NewScene()
{
    m_context->GetSubsystem<Scene>()->Clear();
    Populate();
}

void DirectusHierarchy::ShowOpenSceneDialog()
{
    m_fileDialog->OpenScene();
}

void DirectusHierarchy::SaveScene()
{
    m_fileDialog->SaveScene();
}

void DirectusHierarchy::ShowSaveSceneAsDialog()
{
    m_fileDialog->SaveSceneAs();
}

void DirectusHierarchy::ShowOpenModelDialog()
{
    m_fileDialog->OpenModel();
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

    QAction actionSpotLight("Spot Light", this);
    actionSpotLight.setEnabled(true);

    menuLight.addAction(&actionDirectionalLight);
    menuLight.addAction(&actionPointLight);
    menuLight.addAction(&actionSpotLight);
    //====================================================

    //= AUDIO ============================================
    QMenu menuAudio("Audio", this);
    menuAudio.setEnabled(true);

    QAction actionAudioSource("Audio Source", this);
    actionAudioSource.setEnabled(true);

    menuAudio.addAction(&actionAudioSource);
    //====================================================

    //= CAMERA ===========================================
    QAction actionCamera("Camera", this);
    actionCamera.setEnabled(true);
    //====================================================

    //= SIGNAL - SLOT connections ==================================================================
    connect(&actionRename,              SIGNAL(triggered()), this,  SLOT(RenameSelected()));
    connect(&actionDelete,              SIGNAL(triggered()), this,  SLOT(DeleteSelected()));
    connect(&actionCreateEmpty,         SIGNAL(triggered()), this,  SLOT(CreateEmptyGameObject()));
    connect(&actionCube,                SIGNAL(triggered()), this,  SLOT(CreateCube()));
    connect(&actionQuad,                SIGNAL(triggered()), this,  SLOT(CreateQuad()));
    connect(&actionDirectionalLight,    SIGNAL(triggered()), this,  SLOT(CreateDirectionalLight()));
    connect(&actionPointLight,          SIGNAL(triggered()), this,  SLOT(CreatePointLight()));
    connect(&actionSpotLight,           SIGNAL(triggered()), this,  SLOT(CreateSpotLight()));
    connect(&actionAudioSource,         SIGNAL(triggered()), this,  SLOT(CreateAudioSource()));
    connect(&actionCamera,              SIGNAL(triggered()), this,  SLOT(CreateCamera()));
    //==============================================================================================

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
    contextMenu.addMenu(&menuAudio);
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

    //= LIGHT ============================================
    QMenu menuLight("Light", this);
    menuLight.setEnabled(true);

    QAction actionDirectionalLight("Directional Light", this);
    actionDirectionalLight.setEnabled(true);

    QAction actionPointLight("Point Light", this);
    actionPointLight.setEnabled(true);

    QAction actionSpotLight("Spot Light", this);
    actionSpotLight.setEnabled(true);

    menuLight.addAction(&actionDirectionalLight);
    menuLight.addAction(&actionPointLight);
    menuLight.addAction(&actionSpotLight);
    //====================================================

    //= CAMERA ===========================================
    QAction actionCamera("Camera", this);
    actionCamera.setEnabled(true);
    //====================================================

    //= SIGNAL - SLOT connections ==================================================================
    connect(&actionCreateEmpty,         SIGNAL(triggered()), this,  SLOT(CreateEmptyGameObject()));
    connect(&actionCube,                SIGNAL(triggered()), this,  SLOT(CreateCube()));
    connect(&actionQuad,                SIGNAL(triggered()), this,  SLOT(CreateQuad()));
    connect(&actionDirectionalLight,    SIGNAL(triggered()), this,  SLOT(CreateDirectionalLight()));
    connect(&actionPointLight,          SIGNAL(triggered()), this,  SLOT(CreatePointLight()));
    connect(&actionSpotLight,           SIGNAL(triggered()), this,  SLOT(CreateSpotLight()));
    connect(&actionCamera,              SIGNAL(triggered()), this,  SLOT(CreateCamera()));
    //==============================================================================================

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

    auto gameObject = GetSelectedGameObject();
    if (gameObject.expired())
        return;

    // If this action is called by the tree itself,
    // the item will already be in edit mode (double clicked)
    if (this->state() != QTreeWidget::EditingState)
        this->editItem(item);

    // Set the name of the gameobject from the item
    string name = item->text(0).toStdString();
    gameObject.lock()->SetName(name);
}

void DirectusHierarchy::DeleteSelected()
{
    // Get the currently selected GameObject
    auto gameObject = GetSelectedGameObject();
    if (gameObject.expired())
        return;

    // Delete it
    m_context->GetSubsystem<Scene>()->RemoveGameObject(gameObject);

    // Refresh the hierarchy
    Populate();
}

void DirectusHierarchy::SelectGameObject(GameObject* gameObject)
{
    vector<QTreeWidgetItem*> items = GetAllTreeItems();
    for (const auto& item : items)
    {
        auto gameObjectItem = ToGameObject(item);
        if (gameObjectItem.expired())
            continue;

        if (gameObjectItem._Get()->GetID() == gameObject->GetID())
        {
            // Select
            this->clearSelection();
            item->setSelected(true);

            // Expand
            ExpandTreeItemGoingUp(item);
            return;
        }
    }
}
//========================================================

//= GAMEOBJECT ADDITIONS =================================
void DirectusHierarchy::CreateEmptyGameObject()
{
    // Create an empty GameObject and get it's Transform
    auto gameobject = m_context->GetSubsystem<Scene>()->CreateGameObject().lock();
    Transform* transform = gameobject->GetTransform();

    // Make it a child of the selected GameObject (if there is one)
    auto selectedGameObject = GetSelectedGameObject().lock();
    if (selectedGameObject)
    {
        transform->SetParent(selectedGameObject->GetTransform());
    }

    // Refresh the hierarchy
    Populate();
}

void DirectusHierarchy::CreateEmptyGameObjectRoot()
{
    m_context->GetSubsystem<Scene>()->CreateGameObject().lock();
    Populate();
}

void DirectusHierarchy::CreateCube()
{
    // Create GameObject
    auto gameobject = m_context->GetSubsystem<Scene>()->CreateGameObject().lock();
    gameobject->SetName("Cube");

    // Add a mesh component
    MeshFilter* meshComp = gameobject->AddComponent<MeshFilter>()._Get();
    meshComp->SetMesh(MeshFilter::Cube);

    // Add a mesh renderer
    MeshRenderer* meshRenderer = gameobject->AddComponent<MeshRenderer>()._Get();
    meshRenderer->SetMaterialByType(MaterialType::Material_Basic);

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreateQuad()
{
    // Create GameObject
    auto gameobject = m_context->GetSubsystem<Scene>()->CreateGameObject().lock();
    gameobject->SetName("Quad");

    // Add a mesh component
    MeshFilter* meshComp = gameobject->AddComponent<MeshFilter>()._Get();
    meshComp->SetMesh(MeshFilter::Quad);

    // Add a mesh renderer
    MeshRenderer* meshRenderer = gameobject->AddComponent<MeshRenderer>()._Get();
    meshRenderer->SetMaterialByType(MaterialType::Material_Basic);

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreateDirectionalLight()
{
    // Create GameObject
    auto gameobject = m_context->GetSubsystem<Scene>()->CreateGameObject().lock();
    gameobject->SetName("Directional light");

    // Add component
    Light* light = gameobject->AddComponent<Light>()._Get();
    light->SetLightType(Directional);

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreatePointLight()
{
    // Create GameObject
    auto gameobject = m_context->GetSubsystem<Scene>()->CreateGameObject().lock();
    gameobject->SetName("Point light");

    // Add component
    Light* light = gameobject->AddComponent<Light>()._Get();
    light->SetLightType(Point);

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreateSpotLight()
{
    // Create GameObject
    auto gameobject = m_context->GetSubsystem<Scene>()->CreateGameObject().lock();
    gameobject->SetName("Spot light");

    // Add component
    Light* light = gameobject->AddComponent<Light>()._Get();
    light->SetLightType(Spot);

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreateCamera()
{
    // Create GameObject
    auto gameobject = m_context->GetSubsystem<Scene>()->CreateGameObject().lock();
    gameobject->SetName("Camera");

    // Add component
    gameobject->AddComponent<Camera>();

    // Refresh hierarchy
    Populate();
}

void DirectusHierarchy::CreateAudioSource()
{
    // Create GameObject
    auto gameobject = m_context->GetSubsystem<Scene>()->CreateGameObject().lock();
    gameobject->SetName("Audio source");

    // Add component
    gameobject->AddComponent<AudioSource>();

    // Refresh hierarchy
    Populate();
}
//========================================================

//= COMPONENT ADDITIONS ==================================
void DirectusHierarchy::AddCameraComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<Camera>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddMeshFilterComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<MeshFilter>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddMeshRendererComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<MeshRenderer>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddLightComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<Light>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddRigidBodyComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<RigidBody>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddColliderComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<Collider>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddConstraintComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<Constraint>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddSkyboxComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<Skybox>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddAudioListenerComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<AudioListener>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}

void DirectusHierarchy::AddAudioSourceComponent()
{
    // Get the currently selected GameObject
    auto gameobject = GetSelectedGameObject();
    if (gameobject.expired())
        return;

    gameobject._Get()->AddComponent<AudioSource>();

    // Update the engine and the inspector
    m_inspector->Inspect(gameobject);
}
//========================================================
