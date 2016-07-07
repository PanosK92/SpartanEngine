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
    Directus3DWidget.cpp

HEADERS  += editor.h \
    Directus3DWidget.h

FORMS    += editor.ui

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../Binaries/ -lDirectus3d
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../Binaries/ -lDirectus3d

INCLUDEPATH += $$PWD/../Directus3D
DEPENDPATH += $$PWD/../Directus3D
