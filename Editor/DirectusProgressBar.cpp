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

    // WINDOW FLAGS - How the window appears
    Qt::WindowFlags flags = windowFlags() | Qt::MSWindowsFixedSizeDialogHint;
    Qt::WindowFlags helpFlag = Qt::WindowContextHelpButtonHint | Qt::WindowCloseButtonHint;
    flags = flags & (~helpFlag);
    setWindowFlags(flags);

    // Progress bar update frequency
    m_timer = new QTimer(this);
    m_timer->start(200);

    connect(m_timer, SIGNAL(timeout()), this, SLOT(UpdateProgressBar()));
}

void DirectusProgressBar::Initialize(QWidget* mainWindow, Context* engineContext)
{
    m_mainWindow = mainWindow;
    m_engineContext = engineContext;

    // Make progress bar text visible
    ui->progressBarLoadingDialog->setTextVisible(true);
}

void DirectusProgressBar::UpdateProgressBar()
{
    if (!m_isVisible)
        return;

    QProgressBar* progressBar = ui->progressBarLoadingDialog;
    QLabel* label = ui->labelLoadingDialog;
    ModelImporter* importer = m_engineContext->GetSubsystem<ResourceManager>()->GetModelImporter()._Get();

    // Compute progress bar stats
    QString currentJob = QString::fromStdString(importer->GetStatNodeProcessed());
    int jobCount = importer->GetStatNodeCount();
    int jobCurrent = importer->GetStatNodeCurrent();
    float progressPercentage = ((float)jobCurrent / (float)jobCount) * 100.0f;

    // Update progress bar
    progressBar->setValue(progressPercentage);

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
