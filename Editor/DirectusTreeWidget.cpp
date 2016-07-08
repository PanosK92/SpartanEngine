//= INCLUDES ==================
#include "DirectusTreeWidget.h"
#include "DirectusQTHelper.h"
#include <vector>
//=============================

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
    this->clear();
}

void DirectusTreeWidget::Populate()
{
    Clear();

    if (!m_socket)
        return;

    vector<GameObject*> gameObjects = m_socket->GetAllGameObjects();
    for (int i = 0; i < gameObjects.size(); i++)
    {
        GameObject* gameObject = gameObjects[i];
        QString name = QString::fromStdString(gameObject->GetName());

        QTreeWidgetItem* item = new QTreeWidgetItem(this);

        item->setTextColor(0, QColor("#B4B4B4"));
        item->setText(0, name);

        this->addTopLevelItem(item);
    }
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
    //m_socket->LoadSceneFromFile();
    Populate();
}

void DirectusTreeWidget::SaveScene()
{

}

void DirectusTreeWidget::SaveSceneAs()
{

}
