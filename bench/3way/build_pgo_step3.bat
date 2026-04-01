@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

set ZEPHYR_ROOT=%~dp0..\..
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set CLANG_CL="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"
set PROFDATA=%ZEPHYR_ROOT%\build\zephyr.profdata

echo Profdata: %PROFDATA%
if not exist "%PROFDATA%" (
    echo ERROR: profdata not found
    exit /b 1
)

cd /d "%ZEPHYR_ROOT%\build"

echo Configuring PGO build...
%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=%CLANG_CL% -DCMAKE_CXX_COMPILER=%CLANG_CL% "-DCMAKE_CXX_FLAGS=/EHsc -fprofile-instr-use=%PROFDATA:\=/%" "%ZEPHYR_ROOT%" 2>&1

echo Building...
%CMAKE% --build . --config Release --target zephyr_cli 2>&1

if exist "%ZEPHYR_ROOT%\build\zephyr.exe" (
    echo === PGO Build OK ===
) else (
    echo ERROR: zephyr.exe not found
)
