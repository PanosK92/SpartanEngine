//= INCLUDES =========
#include "Hierarchy.h"
#include <vector>
//====================

//= NAMESPACES =====
using namespace std;
//==================

Hierarchy::Hierarchy(QTreeWidget *tree, Socket* socket)
{
    m_tree = tree;
    m_socket = socket;

    Initialize();

    if (m_tree && m_socket)
        Update();
}

Hierarchy::~Hierarchy()
{

}

void Hierarchy::Update()
{
    vector<GameObject*> gameObjects = m_socket->GetAllGameObjects();

    for (int i = 0; i < gameObjects.size(); i++)
    {
        GameObject* gameObject = gameObjects[i];
        QString name = QString::fromStdString(gameObject->GetName());

        //QTreeWidgetItem* item = new QTreeWidgetGameObjectItem(gameObject);

        //m_tree->addTopLevelItem(item);
    }
}

//= PRIVATE ========================
void Hierarchy::Initialize()
{
    // GreyDark     = #292929
    // GreyMedium   = #383838
    // GreyLight    = #404040
}

void Hierarchy::AddRoot()
{

}

void Hierarchy::AddChild()
{

}
//===================================
