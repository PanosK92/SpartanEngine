#-------------------------------------------------
#
# Project created by QtCreator 2016-07-07T21:21:30
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = "Directus3D"
TEMPLATE = app

# Generates VS files - DISABLED FOR VS 15 PREVIEW
# TEMPLATE=vcapp

SOURCES += main.cpp\
        editor.cpp \
    DirectusPlayButton.cpp \
    AboutDialog.cpp \
    DirectusDirExplorer.cpp \
    DirectusFileExplorer.cpp \
    DirectusConsole.cpp \
    DirectusInspector.cpp \
    DirectusHierarchy.cpp \
    DirectusTransform.cpp \
    DirectusAdjustLabel.cpp \
    DirectusCamera.cpp \
    DirectusMeshRenderer.cpp \
    DirectusMaterial.cpp \
    DirectusLight.cpp \
    DirectusRigidBody.cpp \
    DirectusCollider.cpp \
    DirectusScript.cpp \
    DirectusMeshCollider.cpp \
    DirectusCore.cpp \
    DirectusAssetLoader.cpp \
    DirectusComboLabelText.cpp \
    DirectusComboSliderText.cpp \
    DirectusMeshFilter.cpp \
    DirectusColorPicker.cpp \
    DirectusStatsLabel.cpp \
    DirectusDropDownButton.cpp \
    DirectusProgressBar.cpp \
    DirectusFileDialog.cpp \
    DirectusIconProvider.cpp \
    DirectusMaterialDropTarget.cpp \
    DirectusMaterialTextureDropTarget.cpp \
    DirectusAudioSource.cpp \
    DirectusAudioListener.cpp

HEADERS  += editor.h \
    DirectusPlayButton.h \
    AboutDialog.h \
    DirectusDirExplorer.h \
    DirectusFileExplorer.h \
    DirectusConsole.h \
    DirectusInspector.h \
    DirectusHierarchy.h \
    DirectusTransform.h \
    DirectusAdjustLabel.h \
    DirectusCamera.h \
    DirectusMeshRenderer.h \
    DirectusMaterial.h \
    DirectusLight.h \
    DirectusRigidBody.h \
    DirectusCollider.h \
    DirectusScript.h \
    DirectusMeshCollider.h \
    DirectusCore.h \
    DirectusQVariantPacker.h \
    DirectusAssetLoader.h \
    DirectusComboLabelText.h \
    DirectusComboSliderText.h \
    DirectusMeshFilter.h \
    DirectusColorPicker.h \
    DirectusStatsLabel.h \
    DirectusDropDownButton.h \
    DirectusProgressBar.h \
    DirectusFileDialog.h \
    DirectusIconProvider.h \
    DirectusMaterialDropTarget.h \
    DirectusMaterialTextureDropTarget.h \
    DirectusAudioSource.h \
    DirectusAudioListener.h \
    DirectusIComponent.h

FORMS    += editor.ui \
    AboutDialog.ui \
    AssetLoadingDialog.ui

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../Binaries/ -lDirectus3DRuntime
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../Binaries/ -lDirectus3DRuntime

INCLUDEPATH += $$PWD/../Directus3D
DEPENDPATH += $$PWD/../Directus3D

DISTFILES +=

ParentDirectory = $$PWD/../../Binaries/

DESTDIR = "$$ParentDirectory"
RCC_DIR = "$$ParentDirectory\RCCFiles"
UI_DIR = "$$ParentDirectory\UICFiles"
MOC_DIR = "$$ParentDirectory\MOCFiles"
OBJECTS_DIR = "$$ParentDirectory\ObjFiles"

RESOURCES += \
    Images/images.qrc

# Set .exe icon
win32:RC_ICONS += Images/icon.ico
