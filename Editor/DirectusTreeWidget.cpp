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

DirectusTreeWidget::DirectusTreeWidget(QWidget *parent) : QTreeWidget(parent)
{

}

void DirectusTreeWidget::SetEngineSocket(Socket *socket)
{
    m_socket = socket;
    Populate();
}

void DirectusTreeWidget::Clear()
{
    //m_socket->ClearScene();
    clear();
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
    Populate();
}

void DirectusTreeWidget::NewScene()
{
    m_socket->ClearScene();
    Populate();
}

void DirectusTreeWidget::OpenScene()
{
    QString title = "Load Scene";
    QString filter = "All files (*.dss)";
    QString dir = "../Assets";
    QString fileName = QFileDialog::getOpenFileName(
                this,
                title,
                dir,
                filter
                );

    m_socket->LoadSceneFromFile(fileName.toStdString());

    Populate();
}

void DirectusTreeWidget::SaveScene()
{

}

void DirectusTreeWidget::SaveSceneAs()
{
    QTreeWidgetItem* item = this->selectedItems()[0];
    QVariant data = item->data(0, Qt::UserRole);
    GameObject* test = VPtr<GameObject>::asPtr(data);
    LOG(test->GetName());
}
