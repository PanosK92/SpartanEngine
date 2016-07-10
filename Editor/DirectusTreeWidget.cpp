//= INCLUDES ====================
#include "DirectusTreeWidget.h"
#include "DirectusQTHelper.h"
#include <vector>
#include <QFileDialog>
#include "Components/Transform.h"
#include "IO/Log.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

#define NO_PATH "-1"

DirectusTreeWidget::DirectusTreeWidget(QWidget *parent) : QTreeWidget(parent)
{
    m_socket= nullptr;
    m_sceneFileName = NO_PATH;
}

void DirectusTreeWidget::SetEngineSocket(Socket *socket)
{
    m_socket = socket;
    Populate();
}

void DirectusTreeWidget::mousePressEvent(QMouseEvent *event)
{
    // I implement this myself because the QTreeWidget doesn't
    // deselect any items when you click anywhere else but on an item.
    QModelIndex item = indexAt(event->pos());
    bool selected = selectionModel()->isSelected(indexAt(event->pos()));
    QTreeWidget::mousePressEvent(event);
    if ((item.row() == -1 && item.column() == -1) || selected)
    {
        clearSelection();
        const QModelIndex index;
        selectionModel()->setCurrentIndex(index, QItemSelectionModel::Select);
    }
}

void DirectusTreeWidget::Clear()
{
    //m_socket->ClearScene();
    clear();
}

void DirectusTreeWidget::AddRoot(QTreeWidgetItem* item)
{
    this->addTopLevelItem(item);
}

void DirectusTreeWidget::AddChild(QTreeWidgetItem* parent, QTreeWidgetItem* child)
{
    parent->addChild(child);
}

// Adds a gameobject, including any children, to the tree
void DirectusTreeWidget::AddGameObject(GameObject* gameobject, QTreeWidgetItem* parent)
{
    if (!gameobject)
        return;

    // Convert GameObject to QTreeWidgetItem
    QTreeWidgetItem* item = GameObjectToQTreeItem(gameobject);

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

GameObject *DirectusTreeWidget::GetSelectedGameObject()
{
    QList<QTreeWidgetItem*> selectedItems = this->selectedItems();
    if (selectedItems.count() == 0)
        return nullptr;

    QTreeWidgetItem* item = selectedItems[0];
    QVariant data = item->data(0, Qt::UserRole);
    GameObject* gameobject = VPtr<GameObject>::asPtr(data);

    return gameobject;
}

bool DirectusTreeWidget::IsAnyGameObjectSelected()
{
    return GetSelectedGameObject() ? true : false;
}

// Converts a GameObject to a QTreeWidgetItem
QTreeWidgetItem *DirectusTreeWidget::GameObjectToQTreeItem(GameObject* gameobject)
{
	// Get data from the GameObject
	QString name = QString::fromStdString(gameobject->GetName());
	bool isRoot = gameobject->GetTransform()->IsRoot();

	// Create a tree item
	QTreeWidgetItem* item = isRoot ? new QTreeWidgetItem(this) : new QTreeWidgetItem();
	item->setTextColor(0, QColor("#B4B4B4"));
	item->setText(0, name);
	item->setData(0, Qt::UserRole, VPtr<GameObject>::asQVariant(gameobject));

	//= About Qt::UserRole ==============================================================
	// Constant     -> Qt::UserRole
	// Value        -> 0x0100
	// Description  -> The first role that can be used for application-specific purposes.
	//===================================================================================

	return item;
}

//= SLOTS ===============================================
void DirectusTreeWidget::Populate()
{
    Clear();

    if (!m_socket)
        return;

    vector<GameObject*> gameObjects = m_socket->GetRootGameObjects();
    for (int i = 0; i < gameObjects.size(); i++)
            AddGameObject(gameObjects[i], nullptr);
}

void DirectusTreeWidget::CreateEmptyGameObject()
{
    GameObject* gameobject = new GameObject();
    Transform* transform = gameobject->GetTransform();

    GameObject* selectedGameObject = GetSelectedGameObject();
    if (selectedGameObject)
        transform->SetParent(selectedGameObject->GetTransform());

    Populate();
}

void DirectusTreeWidget::NewScene()
{
    m_sceneFileName = NO_PATH;
    m_socket->ClearScene();
    Populate();
}

void DirectusTreeWidget::OpenScene()
{
    QString title = "Load Scene";
    QString filter = "All files (*.dss)";
    QString dir = "../Assets";
    m_sceneFileName = QFileDialog::getOpenFileName(
                this,
                title,
                dir,
                filter
                );

    m_socket->LoadSceneFromFile(m_sceneFileName.toStdString());

    Populate();
}

void DirectusTreeWidget::SaveScene()
{
    if (m_sceneFileName == NO_PATH)
    {
        SaveSceneAs();
        return;
    }

    m_socket->SaveSceneToFile(m_sceneFileName.toStdString());
}

void DirectusTreeWidget::SaveSceneAs()
{
    QString title = "Save Scene";
    QString filter = "All files (*.dss)";
    QString dir = "../Assets";
    m_sceneFileName = QFileDialog::getSaveFileName(
                this,
                title,
                dir,
                filter
                );

    m_socket->SaveSceneToFile(m_sceneFileName.toStdString());
}
//========================================================
