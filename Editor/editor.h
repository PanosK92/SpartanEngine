#ifndef EDITOR_H
#define EDITOR_H

//= INCLUDES ===========
#include <QMainWindow>
#include "Core/Socket.h"
#include "Hierarchy.h"
//======================

namespace Ui {
class Editor;
}

class Editor : public QMainWindow
{
    Q_OBJECT

public:
    explicit Editor(QWidget *parent = 0);
    ~Editor();

private:
    Ui::Editor *ui;
    Socket* m_engineSocket;
    Hierarchy* m_hierarchy;
};

#endif // EDITOR_H
