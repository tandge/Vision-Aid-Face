@echo off & setlocal enabledelayedexpansion
set "ROOT=%~dp0"
if "!ROOT:~-1!"=="\" set "ROOT=!ROOT:~0,-1!"
set "SRC=!ROOT!"

rem Use Qt 6.6 by default and ignore old system QTDIR values such as Qt 5.12.
rem To override the Qt 6.6 location, set QT66_DIR instead of QTDIR.
if not defined QT66_DIR set "QT66_DIR=C:\Qt6.6"
set "QTDIR=!QT66_DIR!"
if not defined VCDIR set "VCDIR=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build"
if not defined CMAKE_GENERATOR set "CMAKE_GENERATOR=NMake Makefiles"
if not defined QT_TOOLS_DIR set "QT_TOOLS_DIR=C:\Qt6.6\Tools"

echo [INFO] QT66_DIR = !QT66_DIR!

:menu
echo.
echo [1]Desktop-Debug  [2]Desktop-Release  [3]Static Single EXE Release  [4]WASM-Release  [5]Clean  [0]Exit
set /p c=Choice:
if "%c%"=="1" (set "DIR=desktop_debug"&  set "TYPE=Debug"&   set "STATIC=OFF"& goto desktop)
if "%c%"=="2" (set "DIR=desktop_release"& set "TYPE=Release"& set "STATIC=OFF"& goto desktop)
if "%c%"=="3" (set "DIR=desktop_static_release"& set "TYPE=Release"& set "STATIC=ON"& goto desktop)
if "%c%"=="4" (set "DIR=wasm_release"& set "TYPE=Release"& goto wasm)
if "%c%"=="5" (rmdir /s /q "!ROOT!\build" 2>nul& echo Cleaned& goto menu)
if "%c%"=="0" exit /b
goto menu

:desktop
call :resolve_qt_dir || exit /b 1
call :verify_desktop_qt || exit /b 1
if /i "!STATIC!"=="ON" call :resolve_static_qt_dir
if /i "!STATIC!"=="ON" echo [INFO] Static Qt kit = !QT_KIT!
set "OUT=!ROOT!\build\!DIR!"
set "BIN=!OUT!\!TYPE!"
if "!CMAKE_GENERATOR!"=="NMake Makefiles" set "BIN=!OUT!"
echo [INFO] Build dir = !OUT!
if /i "!STATIC!"=="ON" (
    if exist "!OUT!\CMakeCache.txt" (
        echo [INFO] Removing old static build cache.
        rmdir /s /q "!OUT!"
    )
)
if not exist "!OUT!" mkdir "!OUT!"

set "PATH=!QT_KIT!\bin;%PATH%"
call "!QT_KIT!\bin\qt-cmake.bat" -S "!SRC!" -B "!OUT!" -G "!CMAKE_GENERATOR!" -DCMAKE_BUILD_TYPE=!TYPE! -DBNEF_STATIC_EXE=!STATIC!
if errorlevel 1 (echo [ERROR] CMake configure failed& pause& goto menu)
cmake --build "!OUT!" --config !TYPE! --parallel
if errorlevel 1 (echo [ERROR] Build failed& pause& goto menu)

if /i "!STATIC!"=="ON" goto static_done

if not exist "!BIN!\BntechEyeFriend.exe" set "BIN=!OUT!\!TYPE!"
if not exist "!BIN!\BntechEyeFriend.exe" set "BIN=!OUT!"

:: Copy OpenCV / ONNX Runtime DLLs (face module). Set OPENCV_DIR / ONNXRUNTIME_DIR if using custom paths.
if not defined OPENCV_DIR set "OPENCV_DIR=C:\opencv\opencv\build"
if not defined ONNXRUNTIME_DIR set "ONNXRUNTIME_DIR=C:\onnxruntime"
if /i "!TYPE!"=="Debug" (
    if exist "!OPENCV_DIR!\x64\vc16\bin\opencv_world490d.dll" copy /y "!OPENCV_DIR!\x64\vc16\bin\opencv_world490d.dll" "!BIN!\"
) else (
    if exist "!OPENCV_DIR!\x64\vc16\bin\opencv_world490.dll" copy /y "!OPENCV_DIR!\x64\vc16\bin\opencv_world490.dll" "!BIN!\"
)
if exist "!ONNXRUNTIME_DIR!\lib\onnxruntime.dll" copy /y "!ONNXRUNTIME_DIR!\lib\onnxruntime.dll" "!BIN!\"
if exist "!ONNXRUNTIME_DIR!\lib\onnxruntime_providers_shared.dll" copy /y "!ONNXRUNTIME_DIR!\lib\onnxruntime_providers_shared.dll" "!BIN!\"

if exist "!BIN!\Qt5Core.dll" del /q "!BIN!\Qt5Core.dll" 2>nul
"!QT_KIT!\bin\windeployqt" --no-translations --compiler-runtime "!BIN!\BntechEyeFriend.exe"
if errorlevel 1 echo [WARN] windeployqt failed, copy Qt DLLs manually

if exist "!BIN!\Qt6Core.dll" (
    dumpbin /headers "!BIN!\Qt6Core.dll" 2>nul | findstr "x64" >nul
    if errorlevel 1 (
        echo [ERROR] Deployed Qt6Core.dll is NOT 64-bit.
        pause
        goto menu
    )
)

call :copy_runtime_data "!BIN!"
echo [OK] Dynamic Qt6 build: !BIN!\BntechEyeFriend.exe

if /i "!TYPE!"=="Release" call :package_dynamic "!BIN!" "!OUT!"
goto menu

:static_done
if not exist "!OUT!\BntechEyeFriend.exe" (
    if exist "!OUT!\Release\BntechEyeFriend.exe" set "OUT=!OUT!\Release"
)
if not defined OPENCV_DIR set "OPENCV_DIR=C:\opencv\opencv\build"
if not defined ONNXRUNTIME_DIR set "ONNXRUNTIME_DIR=C:\onnxruntime"
if exist "!OPENCV_DIR!\x64\vc16\bin\opencv_world490.dll" copy /y "!OPENCV_DIR!\x64\vc16\bin\opencv_world490.dll" "!OUT!\"
if exist "!ONNXRUNTIME_DIR!\lib\onnxruntime.dll" copy /y "!ONNXRUNTIME_DIR!\lib\onnxruntime.dll" "!OUT!\"
if exist "!ONNXRUNTIME_DIR!\lib\onnxruntime_providers_shared.dll" copy /y "!ONNXRUNTIME_DIR!\lib\onnxruntime_providers_shared.dll" "!OUT!\"
call :copy_runtime_data "!OUT!"
echo [OK] Static Qt build output: !OUT!\BntechEyeFriend.exe
echo [NOTE] A true single EXE requires static builds of Qt 6.6, OpenCV and ONNX Runtime.
echo        If CMake linked import libraries for DLL packages, external DLLs are still required.
goto menu

:wasm
call :resolve_wasm_env || exit /b 1
set "OUT=!ROOT!\build\!DIR!"
set "BIN=!OUT!"
echo [INFO] Build dir = !OUT!
if not exist "!OUT!" mkdir "!OUT!"
if errorlevel 1 (echo [ERROR] Cannot create WASM build directory& pause& goto menu)

set "PATH=!WASM_CMAKE_DIR!;!WASM_NINJA_DIR!;!EMSDK!;!EMSDK!\upstream\emscripten;!WASM_QT_KIT!\bin;%PATH%"
set "CC=!EMSDK!\upstream\emscripten\emcc.bat"
set "CXX=!EMSDK!\upstream\emscripten\em++.bat"

rem 检测 CPU 核心数并设置合理的并行编译数
for /f %%i in ('wmic cpu get NumberOfLogicalProcessors /format:value') do (
    for /f %%j in ("%%i") do set "%%j"
)
if not defined NumberOfLogicalProcessors set "NumberOfLogicalProcessors=8"
set "PARALLEL_JOBS=%NumberOfLogicalProcessors%"
echo [INFO] Using %PARALLEL_JOBS% parallel build jobs.

call "!WASM_QT_KIT!\bin\qt-cmake.bat" -S "!SRC!" -B "!OUT!" -G Ninja -DCMAKE_MAKE_PROGRAM="!WASM_NINJA!" -DCMAKE_BUILD_TYPE=!TYPE! -DCMAKE_PREFIX_PATH="!WASM_QT_KIT!" -DQT_HOST_PATH="!WASM_QT_HOST_PATH!" -DBNEF_STATIC_EXE=OFF
if errorlevel 1 (echo [ERROR] WASM CMake configure failed& pause& goto menu)
"!WASM_CMAKE!" --build "!OUT!" --config !TYPE! --parallel %PARALLEL_JOBS%
if errorlevel 1 (echo [ERROR] WASM build failed& pause& goto menu)
call :copy_wasm_runtime "!BIN!"
call :copy_runtime_data "!BIN!"
echo [OK] WASM build output: !BIN!
echo [NOTE] Start a local web server in the output directory to run BntechEyeFriend.html.
goto menu

:resolve_qt_dir
set "QT_KIT="
if exist "!QTDIR!\bin\qt-cmake.bat" set "QT_KIT=!QTDIR!"
if defined QT_KIT exit /b 0

rem Fast path for the usual Qt 6.6.3 installation layout.
if exist "!QTDIR!\6.6.3\msvc2019_64\bin\qt-cmake.bat" set "QT_KIT=!QTDIR!\6.6.3\msvc2019_64"
if defined QT_KIT exit /b 0
if exist "!QTDIR!\6.6.3\mingw_64\bin\qt-cmake.bat" set "QT_KIT=!QTDIR!\6.6.3\mingw_64"
if defined QT_KIT exit /b 0

rem Search common Qt layouts, for example C:\Qt6.6\6.6.3\msvc2019_64.
for /d %%V in ("!QTDIR!\6.6*") do (
    for /d %%D in ("%%~fV\msvc*_64*" "%%~fV\mingw*_64*") do (
        if exist "%%~fD\bin\qt-cmake.bat" (
            set "QT_KIT=%%~fD"
            exit /b 0
        )
    )
)

rem Search when QTDIR itself directly contains the kit directory.
for /d %%D in ("!QTDIR!\msvc*_64*" "!QTDIR!\mingw*_64*") do (
    if exist "%%~fD\bin\qt-cmake.bat" (
        set "QT_KIT=%%~fD"
        exit /b 0
    )
)

echo [ERROR] Qt 6.6 kit not found under !QTDIR!
echo [HINT] Expected a file like C:\Qt6.6\6.6.3\msvc2019_64\bin\qt-cmake.bat
pause
exit /b 1

:verify_desktop_qt
echo [INFO] Qt kit = !QT_KIT!
if not exist "!QT_KIT!\bin\qt-cmake.bat" (
    echo [ERROR] qt-cmake.bat not found in "!QT_KIT!\bin"
    echo         Please install Qt 6.6 or set QTDIR to a Qt 6.6 kit, for example C:\Qt6.6\6.6.3\msvc2019_64.
    pause
    exit /b 1
)

if exist "!QT_KIT!\bin\Qt6Core.dll" (
    echo !QT_KIT!|findstr /i "msvc" >nul
    if not errorlevel 1 call :init_msvc || exit /b 1
    dumpbin /headers "!QT_KIT!\bin\Qt6Core.dll" 2>nul | findstr "x64" >nul
    if errorlevel 1 (
        echo [ERROR] Qt6Core.dll is NOT 64-bit. Please use a 64-bit Qt 6.6 kit.
        pause
        exit /b 1
    )
    echo [OK] Qt 6.6 64-bit verified.
) else (
    echo [INFO] QtCore DLL not found; assuming static Qt kit.
    echo !QT_KIT!|findstr /i "msvc" >nul
    if not errorlevel 1 call :init_msvc || exit /b 1
)
exit /b 0

:resolve_wasm_env
if not defined EMSDK set "EMSDK=C:\emsdk"
if not defined WASM_QT_KIT set "WASM_QT_KIT=C:\Qt6.6\6.6.3\wasm_singlethread"
if not defined WASM_QT_HOST_PATH set "WASM_QT_HOST_PATH=C:\Qt6.6\6.6.3\msvc2019_64"
if not defined ONNXRUNTIME_WASM_DIR set "ONNXRUNTIME_WASM_DIR=C:\onnxruntime\wasm"
if not defined OPENCV_WASM_DIR set "OPENCV_WASM_DIR=C:\opencv\opencv\build_wasm"
set "WASM_CMAKE="
set "WASM_NINJA="
set "WASM_CMAKE_DIR="
set "WASM_NINJA_DIR="

if exist "!QT_TOOLS_DIR!\CMake_64\bin\cmake.exe" set "WASM_CMAKE=!QT_TOOLS_DIR!\CMake_64\bin\cmake.exe"
if not defined WASM_CMAKE set "WASM_CMAKE=cmake"
for %%I in ("!WASM_CMAKE!") do set "WASM_CMAKE_DIR=%%~dpI"
if "!WASM_CMAKE_DIR:~-1!"=="\" set "WASM_CMAKE_DIR=!WASM_CMAKE_DIR:~0,-1!"

if exist "!QT_TOOLS_DIR!\Ninja\ninja.exe" set "WASM_NINJA=!QT_TOOLS_DIR!\Ninja\ninja.exe"
if not defined WASM_NINJA set "WASM_NINJA=ninja"
for %%I in ("!WASM_NINJA!") do set "WASM_NINJA_DIR=%%~dpI"
if "!WASM_NINJA_DIR:~-1!"=="\" set "WASM_NINJA_DIR=!WASM_NINJA_DIR:~0,-1!"

if not exist "!EMSDK!\emsdk_env.bat" (
    echo [ERROR] emsdk_env.bat not found in "!EMSDK!".
    echo [HINT] Environment.txt expects Emscripten 3.1.37 under C:\emsdk.
    pause
    exit /b 1
)
call "!EMSDK!\emsdk_env.bat"
if errorlevel 1 (
    echo [ERROR] Failed to initialize Emscripten environment.
    pause
    exit /b 1
)

if not exist "!WASM_QT_KIT!\bin\qt-cmake.bat" (
    echo [ERROR] Qt WASM kit not found: !WASM_QT_KIT!
    echo [HINT] Expected: C:\Qt6.6\6.6.3\wasm_singlethread
    pause
    exit /b 1
)
if not exist "!WASM_QT_HOST_PATH!\bin\qtpaths6.exe" (
    echo [ERROR] Qt host path not found: !WASM_QT_HOST_PATH!
    echo [HINT] Install/set a desktop Qt 6.6 host kit for QT_HOST_PATH.
    pause
    exit /b 1
)
"!WASM_CMAKE!" --version >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake not found. Checked Qt Tools and PATH.
    echo [HINT] Install Qt Tools CMake or set QT_TOOLS_DIR.
    pause
    exit /b 1
)
"!WASM_NINJA!" --version >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Ninja not found. Checked Qt Tools and PATH.
    echo [HINT] Install Qt Tools Ninja or set QT_TOOLS_DIR.
    pause
    exit /b 1
)
echo [INFO] EMSDK = !EMSDK!
echo [INFO] Qt WASM kit = !WASM_QT_KIT!
echo [INFO] Qt host path = !WASM_QT_HOST_PATH!
echo [INFO] CMake = !WASM_CMAKE!
echo [INFO] Ninja = !WASM_NINJA!
echo [INFO] OpenCV WASM dir = !OPENCV_WASM_DIR!
echo [INFO] ONNX Runtime WASM dir = !ONNXRUNTIME_WASM_DIR!
exit /b 0

:resolve_static_qt_dir
if exist "!QT_KIT!\bin\Qt6Core.dll" (
    if exist "!QTDIR!\6.6.3\msvc2019_64_static\bin\qt-cmake.bat" (
        set "QT_KIT=!QTDIR!\6.6.3\msvc2019_64_static"
        exit /b 0
    )
    for /d %%V in ("!QTDIR!\6.6*") do (
        for /d %%D in ("%%~fV\msvc*_64_static") do (
            if exist "%%~fD\bin\qt-cmake.bat" (
                set "QT_KIT=%%~fD"
                exit /b 0
            )
        )
    )
    for /d %%D in ("!QTDIR!\msvc*_64_static") do (
        if exist "%%~fD\bin\qt-cmake.bat" (
            set "QT_KIT=%%~fD"
            exit /b 0
        )
    )
    echo [ERROR] Static Qt kit not found under !QTDIR!.
    echo [HINT] Expected: !QTDIR!\6.6.3\msvc2019_64_static\bin\qt-cmake.bat
    pause
    exit /b 1
)
exit /b 0

:init_msvc
if defined VSCMD_VER exit /b 0
echo [VS] vcvars64.bat ...
call "!VCDIR!\vcvars64.bat"
if errorlevel 1 (
    echo [ERROR] Failed to initialize VC++ x64 environment. Check VCDIR.
    pause
    exit /b 1
)
exit /b 0

:copy_runtime_data
set "DST=%~1"
if exist "!ROOT!\models" (
    xcopy /s /e /i /y "!ROOT!\models" "!DST!\models\" >nul
    echo [OK] Copied models.
) else (
    echo [WARN] models directory not found in project root.
)
if exist "!ROOT!\db" (
    xcopy /s /e /i /y "!ROOT!\db" "!DST!\db\" >nul
    echo [OK] Copied db.
) else (
    echo [WARN] db directory not found in project root.
)
exit /b 0

:copy_wasm_runtime
set "DST=%~1"
if exist "!ONNXRUNTIME_WASM_DIR!" (
    if not exist "!DST!\onnxruntime" mkdir "!DST!\onnxruntime"
    copy /y "!ONNXRUNTIME_WASM_DIR!\*.js" "!DST!\onnxruntime\" >nul 2>nul
    copy /y "!ONNXRUNTIME_WASM_DIR!\*.wasm" "!DST!\onnxruntime\" >nul 2>nul
    copy /y "!ONNXRUNTIME_WASM_DIR!\VERSION_NUMBER" "!DST!\onnxruntime\" >nul 2>nul
    copy /y "!ONNXRUNTIME_WASM_DIR!\SOURCE.txt" "!DST!\onnxruntime\" >nul 2>nul
    echo [OK] Copied ONNX Runtime WASM files.
) else (
    echo [WARN] ONNX Runtime WASM directory not found: !ONNXRUNTIME_WASM_DIR!
)
if exist "!OPENCV_WASM_DIR!\bin" (
    if not exist "!DST!\opencv" mkdir "!DST!\opencv"
    copy /y "!OPENCV_WASM_DIR!\bin\*.js" "!DST!\opencv\" >nul 2>nul
    copy /y "!OPENCV_WASM_DIR!\bin\*.wasm" "!DST!\opencv\" >nul 2>nul
    echo [OK] Copied OpenCV WASM files.
) else (
    echo [WARN] OpenCV WASM bin directory not found: !OPENCV_WASM_DIR!\bin
)
exit /b 0

:package_dynamic
set "BIN=%~1"
set "OUTDIR=%~2"
set "DIST=!OUTDIR!_package"
set "PKG=!DIST!\BntechEyeFriend"
set "ZIP=!DIST!\BntechEyeFriend-qt66-win64.zip"
echo [PKG] Creating distributable zip ...
if exist "!PKG!" rmdir /s /q "!PKG!"
if exist "!ZIP!" powershell -NoProfile -Command "Remove-Item -LiteralPath '!ZIP!' -Force" >nul 2>nul
mkdir "!PKG!"
robocopy "!BIN!" "!PKG!" /E /R:1 /W:1 /XF "*.zip" >nul
if errorlevel 8 (
    echo [ERROR] robocopy failed
    pause
    exit /b 1
)
powershell -NoProfile -Command "Compress-Archive -Path '!PKG!\*' -DestinationPath '!ZIP!' -Force"
if errorlevel 1 (
    echo [ERROR] Compress-Archive failed
    pause
    exit /b 1
)
echo [OK] Package: !ZIP!
exit /b 0
