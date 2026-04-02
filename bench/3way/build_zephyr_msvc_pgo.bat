@echo off
REM Build Zephyr VM with MSVC PGO (Profile-Guided Optimization)
REM 3-step process: instrument (/LTCG:PGI) -> profile -> optimize (/LTCG:PGO)

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvarsall.bat failed
    exit /b 1
)

set ZEPHYR_ROOT=%~dp0..\..
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set BENCH_DIR=%~dp0

echo.
echo === Step 1/3: Instrumented Build ===
if exist "%ZEPHYR_ROOT%\build" rmdir /s /q "%ZEPHYR_ROOT%\build"
mkdir "%ZEPHYR_ROOT%\build"
pushd "%ZEPHYR_ROOT%\build"

%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DZEPHYR_ENABLE_PGO=ON "%ZEPHYR_ROOT%" 2>&1
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

if not exist "%ZEPHYR_ROOT%\build\zephyr.exe" (
    echo ERROR: zephyr.exe not found
    exit /b 1
)

echo.
echo === Step 2/3: Collecting Profiles ===
echo   Running fib...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\fib.zph" >nul 2>&1
echo   Running loop...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\loop.zph" >nul 2>&1
echo   Running array...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\array.zph" >nul 2>&1
echo   Running vector_math...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\vector_math.zph" >nul 2>&1
echo   Running struct...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\struct.zph" >nul 2>&1

echo.
echo === Step 3/3: PGO Relink ===
pushd "%ZEPHYR_ROOT%\build"

REM Delete only the exe to force relink with PGO data
del /q zephyr.exe 2>nul

REM Patch the link flags to use /LTCG:PGO instead of /LTCG:PGI
REM The .pgc/.pgd files are already in the build directory
powershell -Command "(Get-Content CMakeFiles\zephyr_cli.dir\link.txt) -replace '/LTCG:PGI','/LTCG:PGO' | Set-Content CMakeFiles\zephyr_cli.dir\link.txt"

%CMAKE% --build . --config Release --target zephyr_cli 2>&1
if errorlevel 1 (
    echo ERROR: PGO relink failed
    popd
    exit /b 1
)
popd

if exist "%ZEPHYR_ROOT%\build\zephyr.exe" (
    echo.
    echo === MSVC PGO Build OK ===
) else (
    echo ERROR: PGO zephyr.exe not found
    exit /b 1
)
