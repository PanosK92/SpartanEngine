//= INCLUDES ================
#include "DirectusQTHelper.h"
#include "Directus3DWidget.h"
#include <QApplication>
#include <QObject>
//===========================

Socket* DirectusQTHelper::GetEngineSocket()
{
    Directus3DWidget*  directus3DWidget = (Directus3DWidget*)FindQWidgetByName("directus3DWidget");

    if (directus3DWidget)
        return directus3DWidget->GetEngineSocket();

    return directus3DWidget->GetEngineSocket();
}

QWidget* DirectusQTHelper::FindQWidgetByName(QString name)
{
    foreach(QWidget* widget, QApplication::allWidgets())
        if (widget->objectName() == name)
              return widget;

    return nullptr;
}
