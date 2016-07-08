#pragma once

//= INCLUDES ===========
#include <QMainWindow>
#include "Core/Socket.h"
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
    Ui::Editor* ui;
    Socket* m_engineSocket;
};
