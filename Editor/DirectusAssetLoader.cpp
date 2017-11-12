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

//===================================
#include "DirectusAssetLoader.h"
#include "DirectusProgressBar.h"
#include <QMutex>
#include "Core/Scene.h"
#include "FileSystem/FileSystem.h"
#include "Resource/ResourceManager.h"
//===================================

//= NAMESPACES ==========
using namespace Directus;
//=======================

DirectusAssetLoader::DirectusAssetLoader(QObject* parent) : QObject(parent)
{
    m_mainWindow = nullptr;
    m_context = nullptr;
}

void DirectusAssetLoader::Initialize(QWidget* mainWindow, Context* context)
{
    m_mainWindow = mainWindow;
    m_context = context;

    if (!mainWindow)
        return;

    m_loadingDialog = new DirectusProgressBar(m_mainWindow);
    m_loadingDialog->Initialize(m_mainWindow, m_context);

    // When the loading dialog should show up
    connect(this, SIGNAL(Started()),    m_loadingDialog, SLOT(Show()));
    connect(this, SIGNAL(Finished()),   m_loadingDialog, SLOT(Hide()));
}

std::string DirectusAssetLoader::GetFilePath()
{
    return m_filePath;
}

void DirectusAssetLoader::SetFilePath(std::string filePath)
{
    m_filePath = filePath;
}

void DirectusAssetLoader::PrepareForTexture(std::string filePath, int width, int height)
{
    m_filePath = filePath;
    m_width = width;
    m_height = height;
}

void DirectusAssetLoader::LoadSceneFromFile()
{
    emit Started(); 
    m_context->GetSubsystem<Scene>()->LoadFromFile(m_filePath);
    emit Finished();
}

void DirectusAssetLoader::SaveSceneToFile()
{
    emit Started();
    m_context->GetSubsystem<Scene>()->SaveToFile(m_filePath);
    emit Finished();
}

void DirectusAssetLoader::LoadModelFromFile()
{
    emit Started();
    m_context->GetSubsystem<ResourceManager>()->Load<Model>(m_filePath);
    emit Finished();
}

QPixmap DirectusAssetLoader::LoadTextureFromFile()
{
    QPixmap pixmap;

    if (!m_context)
    {
        LOG_WARNING("DirectusAssetLoader: Can't load texture from file, Context is uninitialized.");
        return pixmap;
    }

    ImageImporter* imageLoader = m_context->GetSubsystem<ResourceManager>()->GetImageImporter()._Get();
    if (!imageLoader)
    {
        LOG_WARNING("DirectusAssetLoader: Can't load texture from file, ImageLoader uninitialized.");
        return pixmap;
    }

    if (!FileSystem::FileExists(m_filePath))
    {
        LOG_WARNING("DirectusAssetLoader: Can't load texture from file, file path is invalid.");
        return pixmap;
    }

    emit Started();

    ImageData imageData = ImageData(m_filePath, m_width, m_height);
    imageLoader->Load(imageData);
    auto image =  QImage((const uchar*)imageData.rgba.data(), m_width, m_height, QImage::Format_RGBA8888);
    pixmap = QPixmap::fromImage(image);

    emit Finished();

    return pixmap;
}

void DirectusAssetLoader::LoadScene()
{
    LoadSceneFromFile();
}

void DirectusAssetLoader::SaveScene()
{
    SaveSceneToFile();
    emit Finished();
}

void DirectusAssetLoader::LoadModel()
{
    LoadModelFromFile();
    emit Finished();
}

void DirectusAssetLoader::LoadTexture()
{
    QMutex mutex;

    mutex.lock();
    m_pixmap = LoadTextureFromFile();
    mutex.unlock();

    emit ImageReady(m_pixmap);
    emit Finished();
}
