@echo off
REM Build Zephyr VM with MSVC

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvarsall.bat failed
    exit /b 1
)

set ZEPHYR_ROOT=%~dp0..\..
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

echo === Building Zephyr ===
echo Source: %ZEPHYR_ROOT%

if not exist "%ZEPHYR_ROOT%\build" mkdir "%ZEPHYR_ROOT%\build"
pushd "%ZEPHYR_ROOT%\build"

set CLANG_CL="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"

echo Configuring with Clang-cl...
%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=%CLANG_CL% -DCMAKE_CXX_COMPILER=%CLANG_CL% "%ZEPHYR_ROOT%" 2>&1
if errorlevel 1 (
    echo ERROR: CMake configure failed
    popd
    exit /b 1
)

echo Building...
%CMAKE% --build . --config Release 2>&1
if errorlevel 1 (
    echo ERROR: Build failed
    popd
    exit /b 1
)

popd

if exist "%ZEPHYR_ROOT%\build\zephyr.exe" (
    echo.
    echo === Zephyr built successfully ===
    echo   %ZEPHYR_ROOT%\build\zephyr.exe
) else (
    echo ERROR: zephyr.exe not found after build
    exit /b 1
)
