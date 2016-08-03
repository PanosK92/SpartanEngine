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

//= INCLUDES ==================
#include "DirectusConsole.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

DirectusConsole::DirectusConsole(QWidget *parent) : QListWidget(parent)
{
    m_socket = nullptr;
    m_engineLogger = nullptr;
}

void DirectusConsole::Initialize(DirectusCore* directusCore)
{
    m_socket = directusCore->GetEngineSocket();

    // Create an engineLogger (implements
    // ILogger interface)and pass it to the engine
    m_engineLogger = new EngineLogger(this);
    m_socket->SetLogger(m_engineLogger);
}

EngineLogger::EngineLogger(QListWidget* list)
{
    m_list = list;
}

void EngineLogger::Log(string log, int type)
{
    if (!m_list)
        return;

    // 0 = Info,
    // 1 = Warning,
    // 2 = Error,
    // 3 = Undefined

    QListWidgetItem* listItem = new QListWidgetItem(QString::fromStdString(log));
    m_list->addItem(listItem);
    m_list->scrollToBottom();
}
