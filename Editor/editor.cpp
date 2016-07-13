/*
Copyright(c) 2016 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

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
    HWND hWnd = (HWND)this->winId();
    HINSTANCE hinstance = (HINSTANCE)::GetModuleHandle(NULL);
    ui->directus3D->Initialize(hWnd, hinstance);
    Socket* engineSocket = ui->directus3D->GetEngineSocket();

    // Resolve other dependencies
    ui->directusInspector->Initialize();
    ui->directusHierarchy->SetDirectusInspector(ui->directusInspector);
    ui->directusDirExplorer->SetFileExplorer(ui->directusFileExplorer);
    ui->directusPlayButton->SetDirectus3DWidget(ui->directus3D);

    // Pass the engine socket to the widgets that need it
    ui->directusHierarchy->SetDirectusSocket(engineSocket);
    ui->directusConsole->SetDirectusSocket(engineSocket);
}

Editor::~Editor()
{
    delete ui;
}

void Editor::on_actionAbout_Directus3D_triggered()
{
    m_aboutDialog->show();
}
