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

DirectusConsole::DirectusConsole(QWidget *parent) : QListWidget(parent)
{
    m_socket = nullptr;
}

void DirectusConsole::Initialize()
{
    // Create an implemntation of EngineLogger
    m_engineLogger = std::make_shared<EngineLogger>(this);

    // Set the logger implementation for the engine to use
    Log::SetLogger(m_engineLogger);
}

EngineLogger::EngineLogger(QListWidget* list)
{
    m_list = list;
}

/*
    I have to replace your QListWidget with a QListView and implement my
    own data model inheriting QAbstractListModel. I could pass the results
    to the model and it will pass the items data when needed. This should
    should help with performance & memory issues the logging currenty suffers from.
*/

void EngineLogger::Log(const string& log, int type)
{
    if (!m_list)
        return;

    // 0 = Info,
    // 1 = Warning,
    // 2 = Error,

    QColor textColor = (type == 0) ? QColor(144, 144, 144) : (type == 1) ? QColor(232, 232, 144) : QColor(214, 115, 115);

    auto item = new QListWidgetItem(QString::fromStdString(log));
    item->setTextColor(textColor);

    m_list->addItem(item);
    m_list->scrollToBottom();
}
