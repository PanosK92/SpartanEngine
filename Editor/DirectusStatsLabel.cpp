//= INCLUDES ==================
#include "DirectusStatsLabel.h"
#include "DirectusCore.h"
#include "Socket/Socket.h"
//=============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

DirectusStatsLabel::DirectusStatsLabel(QWidget *parent) : QLineEdit(parent)
{

}

void DirectusStatsLabel::UpdateStats(DirectusCore* directusCore)
{
    Socket* socket = directusCore->GetEngineSocket();

    string fps = "FPS: " + FormatFloat(socket->GetFPS(), 2);
    string frame = "Frame: " + FormatFloat(socket->GetDeltaTime(), 2) + (" ms");
    string render = "Render: " + FormatFloat(socket->GetRenderTime(), 2) + (" ms");
    string meshes = "Meshes Rendered: " + to_string(socket->GetRenderedMeshesCount());

    string finalText = fps + ", " + frame + ", " + render + ", " + meshes;
    this->setText(QString::fromStdString(finalText));
}

string DirectusStatsLabel::FormatFloat(float value, int digitsAfterDecimal)
{
    QString str = QString::number(value, 'f', digitsAfterDecimal);
    str.remove( QRegExp("0+$") ); // Remove any number of trailing 0's
    str.remove( QRegExp("\\.$") ); // If the last character is just a '.' then remove it

    return str.toStdString();
}
