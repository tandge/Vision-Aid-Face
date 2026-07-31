cmake_minimum_required(VERSION 3.21)
project(BntechEyeFriend VERSION 1.0.0 LANGUAGES CXX)

option(BNEF_STATIC_EXE "Build a statically linked single EXE (requires static Qt/OpenCV/ONNX Runtime)" OFF)
option(BNEF_WASM_USE_PREBUILT_RUNTIME "Use prebuilt OpenCV/ONNX Runtime web files beside the Qt WASM app" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 默认使用 C:\Qt6.6。该目录可以是 Qt prefix 本身，也可以包含 6.6.x/msvc*_64* 子目录。
if(WIN32 AND NOT CMAKE_PREFIX_PATH)
    file(GLOB _qt66_candidates
        "C:/Qt6.6"
        "C:/Qt6.6/6.6*/msvc*_64*"
        "C:/Qt6.6/msvc*_64*"
        "C:/Qt6.6/6.6*/mingw*_64*"
        "C:/Qt6.6/mingw*_64*"
    )
    if(_qt66_candidates)
        list(APPEND CMAKE_PREFIX_PATH ${_qt66_candidates})
    endif()
endif()

if(BNEF_STATIC_EXE)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_compile_definitions(QT_STATIC)
endif()

set(_bnef_qt_required_components
    Core
    Gui
    Widgets
    Multimedia
    MultimediaWidgets
)
if(NOT EMSCRIPTEN)
    list(APPEND _bnef_qt_required_components Sql)
endif()
find_package(Qt6 6.6 REQUIRED COMPONENTS ${_bnef_qt_required_components})
qt_standard_project_setup()

# 可选: 语音合成模块
find_package(Qt6 6.6 COMPONENTS TextToSpeech)
if(Qt6TextToSpeech_FOUND)
    message(STATUS "Qt6 TextToSpeech found")
else()
    message(WARNING "Qt6 TextToSpeech NOT found - voice playback disabled")
endif()

if(EMSCRIPTEN)
    message(STATUS "Configuring Qt WebAssembly build")
    if(BNEF_WASM_USE_PREBUILT_RUNTIME)
        set(OPENCV_WASM_DIR "$ENV{OPENCV_WASM_DIR}" CACHE PATH "OpenCV WASM build directory")
        if(NOT OPENCV_WASM_DIR)
            set(OPENCV_WASM_DIR "C:/opencv/opencv/build_wasm" CACHE PATH "OpenCV WASM build directory" FORCE)
        endif()
        set(ONNXRUNTIME_WASM_DIR "$ENV{ONNXRUNTIME_WASM_DIR}" CACHE PATH "ONNX Runtime WASM directory")
        if(NOT ONNXRUNTIME_WASM_DIR)
            set(ONNXRUNTIME_WASM_DIR "C:/onnxruntime/wasm" CACHE PATH "ONNX Runtime WASM directory" FORCE)
        endif()
        message(STATUS "OpenCV WASM runtime directory: ${OPENCV_WASM_DIR}")
        message(STATUS "ONNX Runtime WASM runtime directory: ${ONNXRUNTIME_WASM_DIR}")
    endif()
endif()

if(NOT EMSCRIPTEN)
    if(WIN32 AND NOT OpenCV_DIR AND EXISTS "C:/opencv/opencv/build/OpenCVConfig.cmake")
        set(OpenCV_DIR "C:/opencv/opencv/build" CACHE PATH "OpenCV package directory")
    endif()
    if(BNEF_STATIC_EXE)
        set(OpenCV_STATIC ON)
    endif()

    find_package(OpenCV QUIET)
    if(NOT OpenCV_FOUND AND WIN32)
        message(WARNING "OpenCVConfig.cmake did not match this MSVC version; falling back to opencv_world library lookup.")
        set(_opencv_root "$ENV{OPENCV_DIR}")
        if(NOT _opencv_root)
            set(_opencv_root "C:/opencv/opencv/build")
        endif()
        find_path(OpenCV_INCLUDE_DIRS NAMES opencv2/opencv.hpp
                  PATHS ${_opencv_root}/include C:/opencv/opencv/build/include)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            find_library(OpenCV_WORLD_LIB NAMES opencv_world490d opencv_world4100d opencv_world480d
                         PATHS ${_opencv_root}/x64/vc16/lib ${_opencv_root}/x64/vc17/lib)
        else()
            find_library(OpenCV_WORLD_LIB NAMES opencv_world490 opencv_world4100 opencv_world480
                         PATHS ${_opencv_root}/x64/vc16/lib ${_opencv_root}/x64/vc17/lib)
        endif()
        if(OpenCV_INCLUDE_DIRS AND OpenCV_WORLD_LIB)
            set(OpenCV_FOUND TRUE)
            set(OpenCV_LIBS ${OpenCV_WORLD_LIB})
            message(STATUS "OpenCV fallback include: ${OpenCV_INCLUDE_DIRS}")
            message(STATUS "OpenCV fallback lib: ${OpenCV_WORLD_LIB}")
        endif()
    endif()
    if(NOT OpenCV_FOUND)
        message(FATAL_ERROR "OpenCV not found. Set OpenCV_DIR or OPENCV_DIR to your OpenCV build directory.")
    endif()
    find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h
              PATHS $ENV{ONNXRUNTIME_DIR}/include
                    C:/onnxruntime/include
                    ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include)
    find_library(ONNXRUNTIME_LIB NAMES onnxruntime_static onnxruntime
                 PATHS $ENV{ONNXRUNTIME_DIR}/lib
                       C:/onnxruntime/lib
                       ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/lib)
    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB)
        message(FATAL_ERROR "ONNX Runtime not found. Set ONNXRUNTIME_DIR environment variable or place it in third_party/onnxruntime.")
    endif()
endif()

set(SOURCES
    client/main.cpp
    client/mainwindow.cpp
    client/camerawidget.cpp
    client/pulsedots.cpp
    client/whisperclient.cpp
)

set(HEADERS
    client/mainwindow.h
    client/camerawidget.h
    client/pulsedots.h
    client/whisperclient.h
)

# Qt Designer .ui files (AUTOUIC 会自动调用 uic 生成 ui_*.h)
set(FORMS
    client/mainwindow.ui
)

set(RESOURCES
    client/resources.qrc
)

if(NOT EMSCRIPTEN)
    list(APPEND SOURCES
        client/facedetector.cpp
        client/facealigner.cpp
        client/facerecognizer.cpp
        client/facedatabase.cpp
        client/facemanagerdialog.cpp
    )
    list(APPEND HEADERS
        client/facedetector.h
        client/facealigner.h
        client/facerecognizer.h
        client/facedatabase.h
        client/facemanagerdialog.h
    )
endif()

qt_add_executable(${PROJECT_NAME} MANUAL_FINALIZATION WIN32 ${SOURCES} ${HEADERS} ${FORMS} ${RESOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

if(NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Sql
        ${OpenCV_LIBS}
        ${ONNXRUNTIME_LIB}
    )

    if(Qt6TextToSpeech_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::TextToSpeech)
        target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_TEXTTOSPEECH)
    endif()

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${ONNXRUNTIME_INCLUDE_DIR}
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/client
)

if(EMSCRIPTEN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BNEF_WASM_BUILD)

    # 编译优化参数
    target_compile_options(${PROJECT_NAME} PRIVATE
        "-O2"  # 使用 O2 优化（比 O3 快，且效果接近）
        "-ffast-math"  # 启用快速数学运算
        "-fno-exceptions"  # 禁用异常（如果代码中不需要）
        "-fno-rtti"  # 禁用 RTTI（如果代码中不需要）
    )

    # 链接优化参数
    target_link_options(${PROJECT_NAME} PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=128MB"
        "-sMAXIMUM_MEMORY=2GB"
        "-sASYNCIFY=1"
        "-lidbfs.js"
        "-O2"  # 链接时优化
        "-sNO_DISABLE_EXCEPTION_CATCHING=0"  # 禁用异常捕获
        "-sDISABLE_EXCEPTION_CATCHING=1"
        "-sASSERTIONS=0"  # 禁用断言检查
        "-sWASM=1"  # 强制生成 wasm（默认，但明确指定）
        "-sMODULARIZE=0"  # 不使用模块化
        "-sEXPORT_ALL=0"  # 不导出所有符号
        "-sSTRICT=0"  # 禁用严格模式检查
        "-sNO_FILESYSTEM=0"  # 保留文件系统支持（因为使用了 idbfs.js）
        "-flto"  # 启用链接时优化
    )

    # 并行编译参数
    set(CMAKE_CXX_COMPILER_LAUNCHER "em++")
    set(CMAKE_C_COMPILER_LAUNCHER "emcc")
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

if(BNEF_STATIC_EXE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE /INCREMENTAL:NO)
    endif()

    # Link common Qt static plugins when they are available in the static Qt kit.
    set(_bnef_qt_static_plugins)
    foreach(_plugin IN ITEMS
        Qt6::QWindowsIntegrationPlugin
        Qt6::QWindowsVistaStylePlugin
        Qt6::QSQLiteDriverPlugin
        Qt6::QWindowsMediaPlugin
        Qt6::QTextToSpeechSapiPlugin
    )
        if(TARGET ${_plugin})
            list(APPEND _bnef_qt_static_plugins ${_plugin})
        endif()
    endforeach()
    if(_bnef_qt_static_plugins)
        qt_import_plugins(${PROJECT_NAME} INCLUDE ${_bnef_qt_static_plugins})
    endif()
endif()

qt_finalize_executable(${PROJECT_NAME})

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
