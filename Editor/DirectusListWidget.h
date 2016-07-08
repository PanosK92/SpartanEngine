#pragma once

//= INCLUDES ===========
#include <QListWidget>
#include "Core/Socket.h"
#include "IO/ILogger.h"
#include <string>
//======================

class DirectusListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit DirectusListWidget(QWidget *parent = 0);
    void SetEngineSocket(Socket* socket);
private:
    Socket* m_socket;
    ILogger* m_engineLogger;

signals:

public slots:
};

class EngineLogger : public ILogger
{
public:
    EngineLogger(QListWidget* list);
    virtual void Log(std::string log, int type);
private:
    QListWidget* m_list;
};
