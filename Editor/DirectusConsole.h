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

//= INCLUDES ===============
#include <QListWidget>
#include "Logging/ILogger.h"
#include <string>
#include <memory>
//==========================

class DirectusCore;
namespace Directus
{
    class Socket;
}

class EngineLogger : public Directus::ILogger
{
public:
    EngineLogger(QListWidget* list);
    virtual void Log(const std::string& log, int type);
private:
    QListWidget* m_list;
};

class DirectusConsole : public QListWidget
{
    Q_OBJECT
public:
    explicit DirectusConsole(QWidget *parent = 0);
    void Initialize(DirectusCore* directusCore);

private:
    Directus::Socket* m_socket;
    std::shared_ptr<EngineLogger> m_engineLogger;

signals:

public slots:
};
