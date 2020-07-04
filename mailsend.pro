TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

unix:LIBS += -lpthread
win32:LIBS += -lws2_32

SOURCES += \
        main.cpp

HEADERS += \
    platform.h
