#ifndef DIRECTUS3DRENDERWIDGET_H
#define DIRECTUS3DRENDERWIDGET_H

#include <QWidget>

class Engine;

class Directus3DRenderWidget : public QObject
{
    Q_OBJECT

    public:
      Directus3DRenderWidget(QWidget* parent = NULL);
      virtual ~Directus3DRenderWidget();
      virtual QPaintEngine* paintEngine() const { return NULL; }

    protected:
      virtual void resizeEvent(QResizeEvent* evt);
      virtual void paintEvent(QPaintEvent* evt);

    private:
      Engine* m_engine;
};

#endif // DIRECTUS3DRENDERWIDGET_H
