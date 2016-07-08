#ifndef DIRECTUS3DWIDGET_H
#define DIRECTUS3DWIDGET_H

//= INCLUDES =============
#include <QObject>
#include <QWidget>
#include <QPaintEngine>
#include <QResizeEvent>
#include "Core/Engine.h"
#include "Core/Socket.h"
//========================

class QDirectus3DWidget : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY(QDirectus3DWidget)

    public:
      QDirectus3DWidget(QWidget* parent = NULL);
      virtual ~QDirectus3DWidget();
      virtual QPaintEngine* paintEngine() const { return NULL; }
      Socket* GetEngineSocket();

    protected:
      virtual void resizeEvent(QResizeEvent* evt);
      virtual void paintEvent(QPaintEvent* evt);

    private:
      Engine* m_engine;
      Socket* m_socket;

      void InitializeEngine();
      void ShutdownEngine();
      void Render();
      void Resize(int width, int height);
};

#endif // DIRECTUS3DWIDGET_H
