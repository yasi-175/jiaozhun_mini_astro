QT += core gui widgets charts

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
TEMPLATE = app
TARGET = jiaozhun_miniastro

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/encoder_worker.cpp \
    src/encoder_reader.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/qhyccd_minimal.cpp

HEADERS += \
    src/encoder_worker.h \
    src/encoder_reader.h \
    src/mainwindow.h \
    src/qhyccd_minimal.h
