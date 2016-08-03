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

//= INCLUDES ==================
#include "DirectusFileDialog.h"
#include <QThread>
//=============================

#define NO_PATH "-1"

DirectusFileDialog::DirectusFileDialog(QWidget* parent) : QFileDialog(parent)
{
    Qt::WindowFlags flags = windowFlags() | Qt::MSWindowsFixedSizeDialogHint;
    Qt::WindowFlags helpFlag = Qt::WindowContextHelpButtonHint;
    flags = flags & (~helpFlag);
    setWindowFlags(flags);

    connect(this, SIGNAL(fileSelected(QString)), this, SLOT(FileDialogAccepted(QString)));
}

void DirectusFileDialog::Initialize(QWidget* mainWindow, DirectusCore* directusCore)
{
    m_mainWindow = mainWindow;
    m_directusCore = directusCore;
    m_socket = m_directusCore->GetEngineSocket();

    m_assetLoader = new DirectusAssetLoader();
    m_assetLoader->Initialize(m_mainWindow, m_socket);

    m_lastSceneFilePath = NO_PATH;
}

void DirectusFileDialog::ForgetLastUsedFilePath()
{
    m_lastSceneFilePath = NO_PATH;
}

bool DirectusFileDialog::FilePathExists()
{
    if (m_lastSceneFilePath == NO_PATH)
        return false;

    return true;
}

void DirectusFileDialog::LoadModel()
{
    setWindowTitle("Load model");
    QString selfilter = tr("Model (*.3ds; *.obj; *.fbx; *.blend; *.dae; *.lwo; *.c4d)");
    QDir dir(selfilter);
    setFilter(dir.filter());
    setDirectory("Assets");
    show();

    m_assetOperation = "Load Model";
}

void DirectusFileDialog::LoadScene()
{
    setWindowTitle("Load Scene");
    //m_fileDialog->setFilter("Scene (*.dss)");
    setDirectory("Assets");
    show();

    m_assetOperation = "Load Scene";
}

void DirectusFileDialog::SaveSceneAs()
{
    setWindowTitle("Save Scene");
    //m_fileDialog->setFilter("Scene (*.dss)");
    setDirectory("Assets");
    show();

    m_assetOperation = "Save Scene As";
}

void DirectusFileDialog::SaveScene()
{
    FileDialogAccepted(m_lastSceneFilePath);
    m_assetOperation = "Save Scene";
}

void DirectusFileDialog::FileDialogAccepted(QString filePath)
{
    // Create a thread and move the asset loader to it
    QThread* thread = new QThread();
    m_assetLoader->SetFilePath(filePath.toStdString());
    m_assetLoader->moveToThread(this->thread()); // works like a reset, necessery to avoid crash
    m_assetLoader->moveToThread(thread);

    // Stop the engine (in case it's running)
    m_directusCore->Stop();
    connect(thread, SIGNAL(started()), m_directusCore, SLOT(Lock()));
    if (m_assetOperation  == "Load Model")
    {
        connect(thread,         SIGNAL(started()),  m_assetLoader,  SLOT(LoadModel()));
        connect(m_assetLoader,  SIGNAL(Finished()), this,           SLOT(Populate()));
        connect(m_assetLoader,  SIGNAL(Finished()), thread,         SLOT(quit()));
        connect(thread,         SIGNAL(finished()), m_directusCore, SLOT(Update()));
    }
    else if (m_assetOperation == "Save Scene As")
    {
        m_lastSceneFilePath = filePath;

        connect(thread,         SIGNAL(started()),  m_assetLoader,  SLOT(SaveScene()));
        connect(m_assetLoader,  SIGNAL(Finished()), thread,         SLOT(quit()));
    }
    else if (m_assetOperation == "Save Scene")
    {
        m_lastSceneFilePath = filePath;

        connect(thread,         SIGNAL(started()),  m_assetLoader,  SLOT(SaveScene()));
        connect(m_assetLoader,  SIGNAL(Finished()), thread,         SLOT(quit()));
    }
    else if (m_assetOperation == "Load Scene")
    {
        m_lastSceneFilePath = filePath;

        connect(thread,         SIGNAL(started()),  m_assetLoader,      SLOT(LoadScene()));
        connect(thread,         SIGNAL(finished()), m_directusCore,     SLOT(Update()));
        connect(m_assetLoader,  SIGNAL(Finished()), this,               SLOT(Populate()));
        connect(m_assetLoader,  SIGNAL(Finished()), thread,             SLOT(quit()));
    }
    connect(thread,         SIGNAL(started()), m_directusCore,          SLOT(Unlock()));
    connect(thread,         SIGNAL(finished()), thread,                 SLOT(deleteLater()));

    thread->start(QThread::HighestPriority);
}
