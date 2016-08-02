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

#pragma once

//= INCLUDES ==================
#include <QObject>
#include <QPixmap>
#include "Core/Socket.h"
#include "AssetLoadingDialog.h"
//=============================

class DirectusAssetLoader : public QObject
{
    Q_OBJECT
public:
    enum AssetOperation
    {
        Load_Model,
        Load_Scene,
        Save_Scene,
        Save_Scene_As,
        Load_Texture,
    };

    explicit DirectusAssetLoader(QObject* parent = nullptr);
    void Initialize(QWidget* mainWindow, Socket* socket);
    void SetFilePath(std::string filePath);
    void GetAssetOperation(AssetOperation assetOperation);
    void PrepareForTexture(std::string filePath, int width, int height);
    AssetOperation GetAssetOperation();

private:
    void LoadSceneFromFile();
    void SaveSceneToFile();
    void LoadModelFromFile();
    QPixmap LoadTextureFromFile();

    QPixmap m_pixmap;
    std::string m_filePath;
    int m_width;
    int m_height;
    AssetOperation m_assetOperation;

    QWidget* m_mainWindow;
    Socket* m_socket;
    AssetLoadingDialog* m_loadingDialog;

signals:
    void ImageReady(QPixmap);
    void Started();
    void Finished();

public slots:
    void LoadScene();
    void SaveScene();
    void LoadModel();
    void LoadTexture();
};
