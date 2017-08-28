//= INCLUDES ========================
#include "DirectusProgressBar.h"
#include "ui_AssetLoadingDialog.h"
#include <QTimer>
#include "Resource/ResourceManager.h"
#include <memory>
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

    // Compute progress bar stats
    QString currentJob = QString::fromStdString(importer->GetStatNodeProcessed());
    int jobCount = importer->GetStatNodeCount();
    int jobCurrent = importer->GetStatNodeCurrent();

    // Update progress bar
    m_targetValue = ((float)jobCurrent / (float)jobCount) * (float)m_max;

    LOG_INFO(jobCurrent);
    LOG_INFO(jobCount);
    LOG_INFO(m_targetValue);

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
