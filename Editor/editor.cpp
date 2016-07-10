//= INCLUDES =========
#include "editor.h"
#include "ui_editor.h"
//====================

Editor::Editor(QWidget* parent) : QMainWindow(parent), ui(new Ui::Editor)
{
    ui->setupUi(this);

    // Create whatever we need here
    m_aboutDialog = new AboutDialog(this);

    // Get engine socket
    Socket* engineSocket = ui->directus3DWidget->GetEngineSocket();

    // Pass the engine socket to the widgets that need it
    ui->hierarchyTree->SetEngineSocket(engineSocket);
    ui->consoleList->SetEngineSocket(engineSocket);

    // Resolve other dependencies
    ui->projectTreeView->SetFileExplorer(ui->projectListView);
}

Editor::~Editor()
{
    delete ui;
}

void Editor::on_actionAbout_Directus3D_triggered()
{
    m_aboutDialog->show();
}
