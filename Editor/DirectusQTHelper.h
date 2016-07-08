#pragma once

//= INCLUDES ===========
#include <QWidget>
#include "Core/Socket.h"
//======================

class DirectusQTHelper
{
public:
    static Socket* GetEngineSocket();
    static QWidget* FindQWidgetByName(QString name);
};
