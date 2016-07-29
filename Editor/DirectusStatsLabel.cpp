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

    string fps = to_string(socket->GetFPS());
    string update = to_string(socket->GetDeltaTime());
    string render = to_string(socket->GetRenderTime());
    string meshes = to_string(socket->GetRenderedMeshesCount());

    string finalText = "FPS: " + fps + ", Update: " + update + ", Render: " + render + ", Meshes Rendered: " + meshes;
    this->setText(QString::fromStdString(finalText));
}
