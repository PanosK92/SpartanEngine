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
    m_socket = DirectusQTHelper::GetEngineSocket();
}

// Updates the tree to represent the current state
// of the game engine's gameobjects
void DirectusTreeWidget::Update()
{
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
