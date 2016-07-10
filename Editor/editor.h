#pragma once

//= INCLUDES ===========
#include <QMainWindow>
#include "AboutDialog.h"
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

private slots:
    void on_actionAbout_Directus3D_triggered();

private:
    Ui::Editor* ui;
    AboutDialog* m_aboutDialog;
};
