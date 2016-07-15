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

//==============================
#include "DirectusAssetLoader.h"
#include "Loading/ImageLoader.h"
#include "IO/FileHelper.h"
#include <QMutex>
//==============================

DirectusAssetLoader::DirectusAssetLoader(QObject* parent) : QObject(parent)
{

}

void DirectusAssetLoader::PrepareForScene(std::string filePath, Socket* socket)
{
    m_socket = socket;
    m_filePath = filePath;
}

void DirectusAssetLoader::PrepareForModel(std::string filePath, Socket* socket)
{
     m_socket = socket;
     m_filePath = filePath;
}

void DirectusAssetLoader::PrepareForImage(std::string filePath, int width, int height)
{
    m_filePath = filePath;
    m_width = width;
    m_height = height;
}

void DirectusAssetLoader::LoadSceneFromFile()
{
    m_socket->LoadSceneFromFile(m_filePath);
}

void DirectusAssetLoader::SaveSceneToFile()
{
    m_socket->SaveSceneToFile(m_filePath);
}

void DirectusAssetLoader::LoadModelFromFile()
{
    m_socket->LoadModel(m_filePath);
}

QPixmap DirectusAssetLoader::LoadImageFromFile()
{
    ImageLoader* engineImageLoader = new ImageLoader();
    QPixmap pixmap;
    if (FileHelper::FileExists(m_filePath))
    {
        engineImageLoader->Load(m_filePath, m_width, m_height);

        QImage image;
        image =  QImage(
                    (const uchar*)engineImageLoader->GetRGBA(),
                    m_width,
                    m_height,
                    QImage::Format_RGBA8888
                    );

        pixmap = QPixmap::fromImage(image);

        delete engineImageLoader;
    }

    return pixmap;
}

void DirectusAssetLoader::LoadScene()
{
    QMutex mutex;

    mutex.lock();
    LoadSceneFromFile();
    mutex.unlock();

    emit Finished();
}

void DirectusAssetLoader::SaveScene()
{
    QMutex mutex;

    mutex.lock();
    SaveSceneToFile();
    mutex.unlock();

    emit Finished();
}

void DirectusAssetLoader::LoadModel()
{
    QMutex mutex;

    mutex.lock();
    LoadModelFromFile();
    mutex.unlock();

    emit Finished();
}

void DirectusAssetLoader::LoadTexture()
{
    QMutex mutex;

    mutex.lock();
    m_pixmap = LoadImageFromFile();
    mutex.unlock();

    emit ImageReady(m_pixmap);
    emit Finished();
}

void DirectusAssetLoader::DeleteLater()
{

}
