#-------------------------------------------------
#
# Project created by QtCreator 2016-07-07T21:21:30
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = Editor
TEMPLATE = app


SOURCES += main.cpp\
        editor.cpp \
    DirectusTreeWidget.cpp \
    Directus3DWidget.cpp \
    DirectusQTHelper.cpp \
    DirectusListWidget.cpp \
    DirectusPlayButton.cpp

HEADERS  += editor.h \
    DirectusTreeWidget.h \
    Directus3DWidget.h \
    DirectusQTHelper.h \
    DirectusListWidget.h \
    DirectusPlayButton.h

FORMS    += editor.ui

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../Binaries/ -lDirectus3d
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../Binaries/ -lDirectus3d

INCLUDEPATH += $$PWD/../Directus3D
DEPENDPATH += $$PWD/../Directus3D

DISTFILES +=

ParentDirectory = D:\Projects\Directus3D\Binaries

DESTDIR = "$$ParentDirectory"
RCC_DIR = "$$ParentDirectory\RCCFiles"
UI_DIR = "$$ParentDirectory\UICFiles"
MOC_DIR = "$$ParentDirectory\MOCFiles"
OBJECTS_DIR = "$$ParentDirectory\ObjFiles"

RESOURCES += \
    Images/images.qrc
