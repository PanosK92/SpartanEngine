//= INCLUDES ========================
#include "DirectusProgressBar.h"
#include "ui_AssetLoadingDialog.h"
#include <QTimer>
#include <memory>
#include "Resource/ResourceManager.h"
#include "Scene/Scene.h"
//===================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

DirectusProgressBar::DirectusProgressBar(QWidget *parent) : QDialog(parent), ui(new Ui::AssetLoadingDialog)
{
    ui->setupUi(this);
    m_mainWindow = nullptr;
    m_isVisible = false;
    m_min = 0;
    m_max = 1000;
    m_targetValue = 0;

    // WINDOW FLAGS - How the window appears
    Qt::WindowFlags flags = windowFlags() | Qt::MSWindowsFixedSizeDialogHint;
    Qt::WindowFlags helpFlag = Qt::WindowContextHelpButtonHint | Qt::WindowCloseButtonHint;
    flags = flags & (~helpFlag);
    setWindowFlags(flags);

    // Progress bar update frequency
    m_timerProgressUpdate = new QTimer(this);
    m_timerProgressUpdate->start(200);

    // Progress bar smooth effect update frequency
    m_timerSmoothBar = new QTimer(this);
    m_timerSmoothBar->start(10);

    connect(m_timerProgressUpdate, SIGNAL(timeout()), this, SLOT(UpdateProgressBar()));
    connect(m_timerSmoothBar, SIGNAL(timeout()), this, SLOT(IncrementTowardsTargetValue()));
}

void DirectusProgressBar::Initialize(QWidget* mainWindow, Context* engineContext)
{
    m_mainWindow = mainWindow;
    m_engineContext = engineContext;

    // Set progress bar properties
    ui->progressBarLoadingDialog->setTextVisible(true); // show percentage
    ui->progressBarLoadingDialog->setMinimum(m_min);
    ui->progressBarLoadingDialog->setMaximum(m_max);
}

void DirectusProgressBar::IncrementTowardsTargetValue()
{
    if (!m_isVisible)
        return;

    QProgressBar* progressBar = ui->progressBarLoadingDialog;

    int currentValue = progressBar->value();
    if (currentValue >= m_targetValue)
    {
        progressBar->setValue(m_targetValue);
        return;
    }

    currentValue++;
    progressBar->setValue(currentValue);
}

void DirectusProgressBar::UpdateProgressBar()
{
    if (!m_isVisible)
        return;

    QLabel* label = ui->labelLoadingDialog;
    ModelImporter* importer = m_engineContext->GetSubsystem<ResourceManager>()->GetModelImporter()._Get();
    Scene* scene = m_engineContext->GetSubsystem<Scene>();

    // Compute progress bar stats
    QString currentJob = "";
    float percentage = 0.0f;

    // Determine where we should get the loading stats from
    if (importer->IsLoading())
    {
        currentJob = QString::fromStdString(importer->GetStatus());
        percentage = importer->GetPercentage();
    }
    else if (scene->IsLoading())
    {
        currentJob = QString::fromStdString(scene->GetStatus());
        percentage = scene->GetPercentage();
    }

    // Update progress bar
    m_targetValue = percentage * (float)m_max;

    // Update label
    label->setText(currentJob);
}

void DirectusProgressBar::Show()
{
    m_mainWindow->children();
    m_mainWindow->setEnabled(false);
    show();
    m_isVisible = true;
}

void DirectusProgressBar::Hide()
{
    m_mainWindow->setEnabled(true);
    ui->progressBarLoadingDialog->setValue(0);
    hide();
    m_isVisible = false;
}

void DirectusProgressBar::Kill()
{
    m_mainWindow->setEnabled(true);
    deleteLater();
}
