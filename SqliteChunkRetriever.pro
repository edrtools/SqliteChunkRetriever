#-------------------------------------------------
#
# Project created by QtCreator 2016-08-08T11:36:28
#
#-------------------------------------------------

QT       -= gui

TARGET = SqliteChunkRetriever
TEMPLATE = lib

DEFINES += SQLITECHUNKRETRIEVER_LIBRARY

SOURCES += sqlitechunkretriever.cpp

HEADERS += sqlitechunkretriever.h\
        sqlitechunkretriever_global.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}
