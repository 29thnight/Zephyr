@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvarsall.bat failed
    exit /b 1
)

set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set SRC=C:\Users\lance\source\Zephyr
set BLD=C:\Users\lance\source\Zephyr\build-msvc

if not exist "%BLD%" mkdir "%BLD%"
cd /d "%BLD%"

echo === Configuring MSVC Release ===
%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release "%SRC%" 2>&1
if errorlevel 1 (
    echo ERROR: CMake configure failed
    exit /b 1
)

echo === Building ===
%CMAKE% --build . --config Release 2>&1
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

if exist "%BLD%\zephyr.exe" (
    echo === SUCCESS: %BLD%\zephyr.exe ===
) else (
    echo ERROR: zephyr.exe not found
    exit /b 1
)
