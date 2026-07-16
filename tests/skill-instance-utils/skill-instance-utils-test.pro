QT -= gui
QT += core

TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
TARGET = skill-instance-utils-test

msvc:QMAKE_CXXFLAGS += /utf-8

INCLUDEPATH += ../../src/core

SOURCES += \
    skill-instance-utils-test.cpp \
    ../../src/core/skill-instance-utils.cpp

HEADERS += ../../src/core/skill-instance-utils.h
