#include "editor.h"
#include "ui_editor.h"

Editor::Editor(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Editor)
{
    ui->setupUi(this);

    m_engineSocket = ui->directus3DWidget->GetEngineSocket();
}

Editor::~Editor()
{
    delete ui;
}
