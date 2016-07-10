#pragma once

//= INCLUDES ===========
#include <QObject>
#include <QWidget>
#include <QPaintEngine>
#include <QResizeEvent>
#include "Core/Engine.h"
#include "Core/Socket.h"
//======================

class Directus3DWidget : public QWidget
{
	Q_OBJECT
	Q_DISABLE_COPY(Directus3DWidget)

public:
	Directus3DWidget(QWidget* parent = NULL);
	virtual ~Directus3DWidget();
	Socket* GetEngineSocket();

    // I will take care of the drawing
	virtual QPaintEngine* paintEngine() const { return NULL; }

protected:
	virtual void resizeEvent(QResizeEvent* evt);
	virtual void paintEvent(QPaintEvent* evt);

private:
	void InitializeEngine();
	void ShutdownEngine();
	void Render();
	void Resize(int width, int height);

	Socket* m_socket;
	Engine* m_engine;
};
