#ifndef HIERARCHY_H
#define HIERARCHY_H

//= INCLUDES ===========
#include <QTreeWidget>
#include "Core/Socket.h"
//======================

class Hierarchy
{
public:
    Hierarchy(QTreeWidget* tree, Socket* socket);
    ~Hierarchy();

    void Update();
private:
    QTreeWidget* m_tree;
    Socket* m_socket;

    void Initialize();
    void AddRoot();
    void AddChild();
};

#endif // HIERARCHY_H
