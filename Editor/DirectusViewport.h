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

//= INCLUDES =================
#include <QObject>
#include <QWidget>
#include <QPaintEngine>
#include <QResizeEvent>
#include "Core/Engine.h"
#include "Socket/Socket.h"
#include <QTimer>
#include "DirectusStatsLabel.h"
//=============================

class DirectusInspector;
namespace Directus
{
    class GameObject;
}

class DirectusViewport : public QWidget
{
	Q_OBJECT
    Q_DISABLE_COPY(DirectusViewport)

public:
    DirectusViewport(QWidget* parent = NULL);
    virtual ~DirectusViewport();
    Directus::Socket* GetEngineSocket();
    void Initialize(void* hwnd, void* hinstance, DirectusStatsLabel* directusStatsLabel);
    bool IsRunning();

protected:
    virtual QPaintEngine* paintEngine() const { return NULL; }
	virtual void resizeEvent(QResizeEvent* evt);
	virtual void paintEvent(QPaintEvent* evt);
    virtual void mousePressEvent(QMouseEvent* event);

private:
    void SetResolution(float width, float height);

    Directus::Socket* m_socket;
    Directus::Engine* m_engine;
    QTimer* m_timerUpdate;
    QTimer* m_timer500Mil;
    QTimer* m_timer60FPS;
    bool m_isRunning;
    bool m_locked;
    DirectusStatsLabel* m_directusStatsLabel;

signals:
    void EngineStarting();
    void EngineStopping();
    void GameObjectPicked(Directus::GameObject*);

public slots:
    void Start();
    void Stop();
    void Update();
    void Update60FPS();
    void Update500Mil();
    void LockUpdate();
    void UnlockUpdate();
    void ToggleDebugDraw();
};
