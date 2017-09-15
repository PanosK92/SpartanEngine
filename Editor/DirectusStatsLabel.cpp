//= INCLUDES ==================
#include "DirectusStatsLabel.h"
#include "DirectusViewport.h"
#include "Core/Scene.h"
#include "Graphics/Renderer.h"
#include "Core/Timer.h"
//=============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

DirectusStatsLabel::DirectusStatsLabel(QWidget *parent) : QLineEdit(parent)
{

}

void DirectusStatsLabel::UpdateStats(DirectusViewport* directusViewport)
{ 
    Context* context = directusViewport->GetEngineContext();

    float fps = context->GetSubsystem<Scene>()->GetFPS();
    double render = context->GetSubsystem<Renderer>()->GetRenderTime();
    double frame = context->GetSubsystem<Timer>()->GetDeltaTimeMs();
    int meshes = context->GetSubsystem<Renderer>()->GetRenderedMeshesCount();

    string fpsStr = "FPS: " + FormatFloat(fps, 2);
    string frameStr = "Frame: " + FormatDouble(frame, 2) + (" ms");
    string renderStr = "Render: " + FormatDouble(render, 2) + (" ms");
    string meshesStr = "Meshes Rendered: " + to_string(meshes);

    string finalText = fpsStr + ", " + frameStr + ", " + renderStr + ", " + meshesStr;
    this->setText(QString::fromStdString(finalText));
}

string DirectusStatsLabel::FormatFloat(float value, int digitsAfterDecimal)
{
    QString str = QString::number(value, 'f', digitsAfterDecimal);
    str.remove( QRegExp("0+$") ); // Remove any number of trailing 0's
    str.remove( QRegExp("\\.$") ); // If the last character is just a '.' then remove it

    return str.toStdString();
}

string DirectusStatsLabel::FormatDouble(double value, int digitsAfterDecimal)
{
    QString str = QString::number(value, 'f', digitsAfterDecimal);
    str.remove( QRegExp("0+$") ); // Remove any number of trailing 0's
    str.remove( QRegExp("\\.$") ); // If the last character is just a '.' then remove it

    return str.toStdString();
}
