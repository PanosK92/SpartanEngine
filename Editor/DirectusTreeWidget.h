#pragma once

//= INCLUDES ===========
#include <QTreeWidget>
#include "Core/Socket.h"
//======================

class DirectusTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit DirectusTreeWidget(QWidget* parent = 0);

private:
    Socket* m_socket;
    void Update();

signals:

public slots:
};
