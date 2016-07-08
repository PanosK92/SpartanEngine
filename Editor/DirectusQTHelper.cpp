//= INCLUDES ================
#include "DirectusQTHelper.h"
#include "Directus3DWidget.h"
#include <QApplication>
#include <QObject>
//===========================

Socket* DirectusQTHelper::GetEngineSocket()
{
    QWidget* widget = FindQWidgetByName("directus3DWidget");

    Directus3DWidget* directus3DWidget = nullptr;
    if (widget)
        directus3DWidget = widget->findChild<Directus3DWidget*>("directus3DWidget");

    if (directus3DWidget)
        return directus3DWidget->GetEngineSocket();

    return nullptr;
}

QWidget* DirectusQTHelper::FindQWidgetByName(QString name)
{
    foreach(QWidget* widget, QApplication::allWidgets())
        if (widget->objectName() == name)
              return widget;

    return nullptr;
}
