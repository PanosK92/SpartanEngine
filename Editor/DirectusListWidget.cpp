//= INCLUDES ==================
#include "DirectusListWidget.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

DirectusListWidget::DirectusListWidget(QWidget *parent) : QListWidget(parent)
{
    m_socket = nullptr;
    m_engineLogger = nullptr;
}

void DirectusListWidget::SetEngineSocket(Socket* socket)
{
    m_socket = socket;

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
}
