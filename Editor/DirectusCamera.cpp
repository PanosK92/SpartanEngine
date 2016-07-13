//=========================
#include "DirectusCamera.h"
//=========================

DirectusCamera::DirectusCamera(QWidget *parent) : QWidget(parent)
{

}

void DirectusCamera::Initialize()
{
    m_gridLayout = new QGridLayout();
    m_validator = new QDoubleValidator(-2147483647, 2147483647, 4);

    //= TITLE =================================================
    m_image = new QWidget(this);
    m_image->setStyleSheet("background-image: url(:/Images/camera.png); background-repeat: no-repeat; background-position: left;");
    m_title = new QLabel("Camera");
    //=========================================================

    //= BACKGROUND ============================================
     m_backgroundLabel = new QLabel("Background");
    //=========================================================

    //= PROJECTION ============================================
    m_projectionLabel = new QLabel("Projection");
    m_projectionComboBox = new QComboBox();
    m_projectionComboBox->addItem("Perspective");
    m_projectionComboBox->addItem("Orthographic");
    //=========================================================

    //= FOV ===================================================
    m_fovLabel = new QLabel("Field of view");
    m_fovSlider = new QSlider(Qt::Horizontal);
    m_fovLineEdit = CreateQLineEdit();
    //=========================================================

    //= CLIPPING PLANES =======================================
    m_clippingPlanesLabel = new QLabel("Clipping planes");
    m_clippingNear = CreateQLineEdit();
    m_clippingFar = CreateQLineEdit();
    m_clippingPlanesNearLabel = new DirectusAdjustLabel();
    m_clippingPlanesNearLabel->setText("Near");
    m_clippingPlanesNearLabel->AdjustQLineEdit(m_clippingNear);
    m_clippingPlanesFarLabel = new DirectusAdjustLabel();
    m_clippingPlanesFarLabel->setText("Far");
    m_clippingPlanesFarLabel->AdjustQLineEdit(m_clippingFar);
    //=========================================================

    // addWidget(*Widget, row, column, rowspan, colspan)

    // Row 0
    m_gridLayout->addWidget(m_image, 0, 0, 1, 1);
    m_gridLayout->addWidget(m_title, 0, 1, 1, 2);

    // Row 1
    m_gridLayout->addWidget(m_backgroundLabel, 1, 0, 1, 1);

    // Row 2
    m_gridLayout->addWidget(m_projectionLabel, 2, 0, 1, 1);
    m_gridLayout->addWidget(m_projectionComboBox, 2, 1, 1, 1);

    // Row 3
    m_gridLayout->addWidget(m_fovLabel, 3, 0, 1, 1);
    m_gridLayout->addWidget(m_fovSlider, 3, 1, 1, 1);
    m_gridLayout->addWidget(m_fovLineEdit, 3, 2, 1, 1);

    // Row 4 and 5
    m_gridLayout->addWidget(m_clippingPlanesLabel, 4, 0, 1, 1);
    m_gridLayout->addWidget(m_clippingPlanesNearLabel, 4, 1, 1, 1);
    m_gridLayout->addWidget(m_clippingNear, 4, 2, 1, 1);
    m_gridLayout->addWidget(m_clippingPlanesFarLabel, 5, 1, 1, 1);
    m_gridLayout->addWidget(m_clippingFar, 5, 2, 1, 1);

    // Connect textEdit(QString) signal with the appropriate slot
    // NOTE: Unlike textChanged(), this signal is not emitted when the
    // text is changed programmatically, for example, by calling setText().
    //connect(m_posX, SIGNAL(textChanged(QString)), this, SLOT(UpdateEnginePos()));
    //connect(m_posY, SIGNAL(textChanged(QString)), this, SLOT(UpdateEnginePos()));
    //connect(m_posZ, SIGNAL(textChanged(QString)), this, SLOT(UpdateEnginePos()));
    //connect(m_rotX, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineRot()));
    //connect(m_rotY, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineRot()));
    //connect(m_rotZ, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineRot()));
    //connect(m_scaX, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineSca()));
    //connect(m_scaY, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineSca()));
    //connect(m_scaZ, SIGNAL(textChanged(QString)), this, SLOT(UpdateEngineSca()));

    this->setLayout(m_gridLayout);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    this->hide();
}

void DirectusCamera::Map(GameObject* gameobject)
{
    m_inspectedCamera = nullptr;

    // Catch evil case
    if (!gameobject)
    {
        this->hide();
        return;
    }

    // Catch the seed of the evil
    m_inspectedCamera = gameobject->GetComponent<Camera>();
    if (!m_inspectedCamera)
    {
        this->hide();
        return;
    }

    // Do the actual mapping
    SetProjection(m_inspectedCamera->GetProjection());
    SetFOV(m_inspectedCamera->GetFieldOfView());
    SetNearPlane(m_inspectedCamera->GetNearPlane());
    SetFarPlane( m_inspectedCamera->GetFarPlane());

    // Make this widget visible
    this->show();
}

void DirectusCamera::SetProjection(Projection projection)
{
    m_projectionComboBox->setCurrentIndex((int)projection);
}

void DirectusCamera::SetNearPlane(float nearPlane)
{
    m_clippingNear->setText(QString::number(nearPlane));
}

void DirectusCamera::SetFarPlane(float farPlane)
{
    m_clippingFar->setText(QString::number(farPlane));
}

void DirectusCamera::SetFOV(float fov)
{
    m_fovLineEdit->setText(QString::number(fov));
}

QLineEdit* DirectusCamera::CreateQLineEdit()
{
    QLineEdit* lineEdit = new QLineEdit();
    lineEdit->setValidator(m_validator);

    return lineEdit;
}
