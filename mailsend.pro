TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

unix:LIBS += -lpthread
win32:LIBS += -lws2_32

SOURCES += \
        MailSend.cpp \
        main.cpp

HEADERS += \
    MailSend.h
