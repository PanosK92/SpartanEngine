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
#include <QTextEdit>
#include <QTimer>
#include "Logging/ILogger.h"
#include <string>
#include <memory>
#include <functional>
//==========================

//= FORWARD DECLARATIONS ===========
class DirectusViewport;
namespace Directus { class Socket; }
//==================================

// The engine logger could call Qt directly but this can be dangerous
// in a multithreaded environment. So, the engine never interacts with Qt directly,
// it instead sends LogPackages which the Qt side will check once in a while and log on its own.

struct LogPackage
{
    std::string text;
    int errorLevel;
};

// Implementation of Directus::ILogger so the engine can log into Qt
class EngineLogger : public Directus::ILogger
{
public:
    typedef std::function<void(LogPackage)> logFunc;
    void SetQtCallback(logFunc&& func)
    {
        m_logFunc = std::forward<logFunc>(func);
    }

    virtual void Log(const std::string& text, int errorLevel)
    {
        // Send package to Qt
        LogPackage package;
        package.text = text;
        package.errorLevel = errorLevel;
        m_logFunc(package);
    }

private:
    logFunc m_logFunc;
};

// Actual Qt console
class DirectusConsole : public QTextEdit
{
    Q_OBJECT
public:
    explicit DirectusConsole(QWidget* parent = 0);
    void Log(const std::string& text, int errorLevel);
    void AddLogPackage(LogPackage package) { m_logs.push_back(package); }

public slots:
    void CheckLogPackages();

private:
    Directus::Socket* m_socket;
    std::shared_ptr<EngineLogger> m_engineLogger;
    std::vector<LogPackage> m_logs;
    QTimer* m_timer;
};



