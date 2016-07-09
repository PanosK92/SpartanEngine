#pragma once

//= INCLUDES ===========
#include <QWidget>
#include "Core/Socket.h"
#include <QVariant>
//======================

class DirectusQTHelper
{
public:
    static Socket* GetEngineSocket();
    static QWidget* FindQWidgetByName(QString name);
};

// This makes the process of converting
// to and from void pointers, easier.
template <class T> class VPtr
{
public:
    static T* asPtr(QVariant v)
    {
    return  (T*) v.value<void *>();
    }

    static QVariant asQVariant(T* ptr)
    {
    return qVariantFromValue((void *) ptr);
    }
};
