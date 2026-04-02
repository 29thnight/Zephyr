@echo off
REM Build Zephyr VM with MSVC PGO (Profile-Guided Optimization)
REM 3-step process: instrument (/LTCG:PGI) -> profile -> optimize (/LTCG:PGO)
REM Uses native MSVC cl.exe (not clang-cl)

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvarsall.bat failed
    exit /b 1
)

set ZEPHYR_ROOT=%~dp0..\..
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set BENCH_DIR=%~dp0

REM ============================================================
REM Step 1: Instrumented build (/GL + /LTCG:PGI)
REM ============================================================
echo.
echo === Step 1/3: Instrumented Build (MSVC PGI) ===
if exist "%ZEPHYR_ROOT%\build" rmdir /s /q "%ZEPHYR_ROOT%\build"
mkdir "%ZEPHYR_ROOT%\build"
pushd "%ZEPHYR_ROOT%\build"

%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_FLAGS="/GL" ^
    -DCMAKE_EXE_LINKER_FLAGS="/LTCG:PGI" ^
    "%ZEPHYR_ROOT%" 2>&1
if errorlevel 1 (
    echo ERROR: CMake configure failed
    popd
    exit /b 1
)

%CMAKE% --build . --config Release --target zephyr_cli 2>&1
if errorlevel 1 (
    echo ERROR: Instrumented build failed
    popd
    exit /b 1
)
popd

if not exist "%ZEPHYR_ROOT%\build\zephyr.exe" (
    echo ERROR: Instrumented zephyr.exe not found
    exit /b 1
)

REM ============================================================
REM Step 2: Collect profiles by running benchmarks
REM MSVC PGO generates .pgc files next to the .exe automatically.
REM ============================================================
echo.
echo === Step 2/3: Collecting Profiles ===

echo   Running fibonacci(35)...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\fib.zph" >nul 2>&1

echo   Running hot_loop(1M)...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\loop.zph" >nul 2>&1

echo   Running array_sum(100K)...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\array.zph" >nul 2>&1

echo   Running vector_math(1M)...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\vector_math.zph" >nul 2>&1

echo   Running struct(1M)...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\struct.zph" >nul 2>&1

REM Merge .pgc files into .pgd (optional — /LTCG:PGO auto-finds .pgd)
echo   Merging profile data...
pushd "%ZEPHYR_ROOT%\build"
pgomgr /merge zephyr.pgd 2>nul
popd

REM ============================================================
REM Step 3: PGO-optimized build (/GL + /LTCG:PGO)
REM ============================================================
echo.
echo === Step 3/3: PGO-Optimized Build (MSVC PGO) ===

REM Preserve .pgd file across rebuild
copy "%ZEPHYR_ROOT%\build\zephyr.pgd" "%ZEPHYR_ROOT%\zephyr.pgd" >nul 2>&1
rmdir /s /q "%ZEPHYR_ROOT%\build"
mkdir "%ZEPHYR_ROOT%\build"
move "%ZEPHYR_ROOT%\zephyr.pgd" "%ZEPHYR_ROOT%\build\zephyr.pgd" >nul 2>&1
pushd "%ZEPHYR_ROOT%\build"

%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_FLAGS="/GL" ^
    -DCMAKE_EXE_LINKER_FLAGS="/LTCG:PGO" ^
    "%ZEPHYR_ROOT%" 2>&1
if errorlevel 1 (
    echo ERROR: CMake PGO configure failed
    popd
    exit /b 1
)

%CMAKE% --build . --config Release --target zephyr_cli 2>&1
if errorlevel 1 (
    echo ERROR: PGO build failed
    popd
    exit /b 1
)
popd

if exist "%ZEPHYR_ROOT%\build\zephyr.exe" (
    echo.
    echo === MSVC PGO Build Complete ===
    echo   %ZEPHYR_ROOT%\build\zephyr.exe
) else (
    echo ERROR: PGO zephyr.exe not found
    exit /b 1
)
