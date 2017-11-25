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

//= INCLUDES ===============
#include "DirectusConsole.h"
#include "DirectusViewport.h"
#include "Logging/Log.h"
//==========================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

DirectusConsole::DirectusConsole(QWidget *parent) : QTextEdit(parent)
{
    m_socket = nullptr;

    this->setReadOnly(true);

    // Create an implementation of EngineLogger
    m_engineLogger = make_shared<EngineLogger>();
    m_engineLogger->SetQtCallback([this](LogPackage package){ AddLogPackage(package); });

    // Set the logger implementation for the engine to use
    Log::SetLogger(m_engineLogger);

    // Timer which checks if the thumbnail image is loaded (async)
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(CheckLogPackages()));
    m_timer->start(500);
}

void DirectusConsole::Log(const string& text, bool append)
{
    if (append)
    {
        this->append(QString::fromStdString(text));
    }
    else
    {
        this->setText(QString::fromStdString(text));
    }
}

void DirectusConsole::AddLogPackage(LogPackage package)
{
    // 0 = Info
    // 1 = Warning
    // 2 = Error
    string colorStart = (package.errorLevel == 0) ? "<font color=\"#A1A1A1\">" : (package.errorLevel == 1) ? "<font color=\"#C8CC5E\">" : "<font color=\"#BD5151\">";
    string colorEnd = "</font>";

    // Construct html version of text
    package.text = colorStart + package.text + colorEnd;

    m_logs.push_back(package);
}

void DirectusConsole::CheckLogPackages()
{
    for (const auto& package : m_logs)
    {
        Log(package.text, true);
    }

    m_logs.clear();
    m_logs.shrink_to_fit();
}

void DirectusConsole::SetDisplayInfo(bool display)
{
    this->append(display ? "info on" : "info off");
}

void DirectusConsole::SetDisplayWarnings(bool display)
{
     this->append(display ? "warnings on" : "warnings off");
}

void DirectusConsole::SetDisplayErrors(bool display)
{
     this->append(display ? "errors on" : "errors off");
}
