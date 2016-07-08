#include "editor.h"
#include "ui_editor.h"

Editor::Editor(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Editor)
{
    ui->setupUi(this);

    m_engineSocket = ui->directus3DWidget->GetEngineSocket();
    m_hierarchy = new Hierarchy(ui->hierarchyTree, m_engineSocket);
}

Editor::~Editor()
{
    delete ui;
}
