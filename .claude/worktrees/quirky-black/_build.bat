@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
set SRC=%1
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%SRC%\build" mkdir "%SRC%\build"
pushd "%SRC%\build"
%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release "%SRC%" 2>&1
if errorlevel 1 (
    echo ERROR: CMake configure failed
    popd
    exit /b 1
)
%CMAKE% --build . --config Release --target zephyr_cli 2>&1
if errorlevel 1 (
    echo ERROR: Build failed
    popd
    exit /b 1
)
popd
echo BUILD OK
