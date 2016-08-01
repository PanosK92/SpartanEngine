//= INCLUDES ==================
#include "DirectusStatsLabel.h"
#include "DirectusCore.h"
#include "Core/Socket.h"
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

    string fps = BeautifyFloat(socket->GetFPS());
    string update = BeautifyFloat(socket->GetDeltaTime());
    string render = BeautifyFloat(socket->GetRenderTime());
    string meshes = to_string(socket->GetRenderedMeshesCount());

    string finalText = "FPS: " + fps + ", Update: " + update + ", Render: " + render + ", Meshes Rendered: " + meshes;
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
