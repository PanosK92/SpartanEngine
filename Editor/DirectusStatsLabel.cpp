//= INCLUDES ==================
#include "DirectusStatsLabel.h"
#include "DirectusCore.h"
#include "Socket/Socket.h"
//=============================

//= NAMESPACES ================
using namespace std;
//=============================

DirectusStatsLabel::DirectusStatsLabel(QWidget *parent) : QLineEdit(parent)
{

}

void DirectusStatsLabel::UpdateStats(DirectusCore* directusCore)
{
    Socket* socket = directusCore->GetEngineSocket();

    string fps = "FPS: " + BeautifyFloat(socket->GetFPS());
    string delta = "Delta: " + BeautifyFloat(socket->GetDeltaTime()) + (" ms");
    string render = "Render: " + BeautifyFloat(socket->GetRenderTime()) + (" ms");
    string meshes = "Meshes Rendered: " + to_string(socket->GetRenderedMeshesCount());

    string finalText = fps + ", " + delta + ", " + render + ", " + meshes;
    this->setText(QString::fromStdString(finalText));
}

string DirectusStatsLabel::BeautifyFloat(float value)
{
    int digitsAfterDecimal = 3;
    QString str = QString::number(value, 'f', digitsAfterDecimal);
    str.remove( QRegExp("0+$") ); // Remove any number of trailing 0's
    str.remove( QRegExp("\\.$") ); // If the last character is just a '.' then remove it

    return str.toStdString();
}
