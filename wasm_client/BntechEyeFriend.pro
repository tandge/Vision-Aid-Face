requires(qtHaveModule(core))
requires(qtHaveModule(gui))
requires(qtHaveModule(widgets))
requires(qtHaveModule(multimedia))
requires(qtHaveModule(multimediawidgets))
requires(qtHaveModule(sql))

lessThan(QT_MAJOR_VERSION, 6): error(This project requires Qt 6.6 or newer)
lessThan(QT_MINOR_VERSION, 6): error(This project requires Qt 6.6 or newer)

QT += core gui widgets multimedia multimediawidgets sql

qtHaveModule(texttospeech) {
    QT += texttospeech
    DEFINES += HAS_TEXTTOSPEECH
}

TARGET = BntechEyeFriend
TEMPLATE = app

CONFIG += c++17

# Add CONFIG+=static_exe when using a static Qt build.
static_exe {
    CONFIG += static
    DEFINES += QT_STATIC
    msvc:QMAKE_CFLAGS_RELEASE += /MT
    msvc:QMAKE_CXXFLAGS_RELEASE += /MT
    msvc:QMAKE_LFLAGS_RELEASE += /INCREMENTAL:NO
}

msvc:QMAKE_CXXFLAGS += /utf-8

SOURCES += \
    client/main.cpp \
    client/mainwindow.cpp \
    client/camerawidget.cpp \
    client/pulsedots.cpp \
    client/whisperclient.cpp

HEADERS += \
    client/mainwindow.h \
    client/camerawidget.h \
    client/pulsedots.h \
    client/whisperclient.h

FORMS += \
    client/mainwindow.ui

RESOURCES += \
    client/resources.qrc

INCLUDEPATH += client

!wasm {
    SOURCES += \
        client/facedetector.cpp \
        client/facealigner.cpp \
        client/facerecognizer.cpp \
        client/facedatabase.cpp \
        client/facemanagerdialog.cpp

    HEADERS += \
        client/facedetector.h \
        client/facealigner.h \
        client/facerecognizer.h \
        client/facedatabase.h \
        client/facemanagerdialog.h

    # OpenCV (set OPENCV_DIR environment variable or modify path below)
    OPENCV_DIR = $$(OPENCV_DIR)
    isEmpty(OPENCV_DIR) {
        OPENCV_DIR = C:/opencv/opencv/build
    }
    INCLUDEPATH += $$OPENCV_DIR/include
    LIBS += -L$$OPENCV_DIR/x64/vc16/lib -lopencv_world490

    # ONNX Runtime (set ONNXRUNTIME_DIR environment variable or modify path below)
    ONNXRUNTIME_DIR = $$(ONNXRUNTIME_DIR)
    isEmpty(ONNXRUNTIME_DIR) {
        ONNXRUNTIME_DIR = C:/onnxruntime
    }
    INCLUDEPATH += $$ONNXRUNTIME_DIR/include
    LIBS += -L$$ONNXRUNTIME_DIR/lib -lonnxruntime
}

wasm {
    # 编译优化
    QMAKE_CXXFLAGS += -O2 -ffast-math -fno-exceptions -fno-rtti
    QMAKE_CFLAGS += -O2 -ffast-math -fno-exceptions -fno-rtti

    # 链接优化
    QMAKE_LFLAGS += -s TOTAL_MEMORY=268435456 -s ALLOW_MEMORY_GROWTH=1
    QMAKE_LFLAGS += -s ASYNCIFY=1 -lidbfs.js
    QMAKE_LFLAGS += -s EXPORTED_RUNTIME_METHODS=[ccall,cwrap]
    QMAKE_LFLAGS += -s EXPORTED_FUNCTIONS=[_main,_malloc,_free]
    QMAKE_LFLAGS += -O2
    QMAKE_LFLAGS += -sNO_DISABLE_EXCEPTION_CATCHING=0
    QMAKE_LFLAGS += -sDISABLE_EXCEPTION_CATCHING=1
    QMAKE_LFLAGS += -sASSERTIONS=0
    QMAKE_LFLAGS += -sWASM=1
    QMAKE_LFLAGS += -sMODULARIZE=0
    QMAKE_LFLAGS += -sEXPORT_ALL=0
    QMAKE_LFLAGS += -sSTRICT=0
    QMAKE_LFLAGS += -flto
}