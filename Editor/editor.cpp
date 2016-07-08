#include "editor.h"
#include "ui_editor.h"

Editor::Editor(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Editor)
{
    ui->setupUi(this);

    Socket* engineSocket = ui->directus3DWidget->GetEngineSocket();
    ui->hierarchyTree->SetEngineSocket(engineSocket);
    ui->consoleList->SetEngineSocket(engineSocket);
}

Editor::~Editor()
{
    delete ui;
}
