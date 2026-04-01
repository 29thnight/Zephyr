@echo off
REM Build Zephyr VM with Clang-cl PGO (Profile-Guided Optimization)
REM 3-step process: instrument → profile → optimize

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvarsall.bat failed
    exit /b 1
)

set ZEPHYR_ROOT=%~dp0..\..
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set CLANG_CL="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"
set LLVM_PROFDATA="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\llvm-profdata.exe"
set BENCH_DIR=%~dp0

REM ============================================================
REM Step 1: Instrumented build
REM ============================================================
echo.
echo === Step 1/3: Instrumented Build ===
if exist "%ZEPHYR_ROOT%\build" rmdir /s /q "%ZEPHYR_ROOT%\build"
mkdir "%ZEPHYR_ROOT%\build"
pushd "%ZEPHYR_ROOT%\build"

%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=%CLANG_CL% -DCMAKE_CXX_COMPILER=%CLANG_CL% ^
    -DCMAKE_CXX_FLAGS="/EHsc -fprofile-instr-generate" ^
    -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" ^
    "%ZEPHYR_ROOT%" 2>&1
if errorlevel 1 (
    echo ERROR: CMake configure failed
    popd
    exit /b 1
)

%CMAKE% --build . --config Release --target zephyr_cli 2>&1
if errorlevel 1 (
    echo ERROR: build failed
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
REM ============================================================
echo.
echo === Step 2/3: Collecting Profiles ===
set LLVM_PROFILE_FILE=%ZEPHYR_ROOT%\build\zephyr-%%p.profraw

echo   Running fibonacci(35)...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\fib.zph" >nul 2>&1

echo   Running hot_loop(1M)...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\loop.zph" >nul 2>&1

echo   Running array_sum(100K)...
"%ZEPHYR_ROOT%\build\zephyr.exe" run "%BENCH_DIR%scripts\array.zph" >nul 2>&1

echo   Merging profiles...
%LLVM_PROFDATA% merge -output="%ZEPHYR_ROOT%\build\zephyr.profdata" "%ZEPHYR_ROOT%\build\"*.profraw 2>&1
if errorlevel 1 (
    echo ERROR: Profile merge failed
    exit /b 1
)

REM ============================================================
REM Step 3: PGO-optimized build
REM ============================================================
echo.
echo === Step 3/3: PGO-Optimized Build ===
copy "%ZEPHYR_ROOT%\build\zephyr.profdata" "%ZEPHYR_ROOT%\zephyr.profdata" >nul
rmdir /s /q "%ZEPHYR_ROOT%\build"
mkdir "%ZEPHYR_ROOT%\build"
move "%ZEPHYR_ROOT%\zephyr.profdata" "%ZEPHYR_ROOT%\build\zephyr.profdata" >nul
pushd "%ZEPHYR_ROOT%\build"

%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=%CLANG_CL% -DCMAKE_CXX_COMPILER=%CLANG_CL% ^
    -DCMAKE_CXX_FLAGS="/EHsc -fprofile-instr-use=%ZEPHYR_ROOT:\=/%/build/zephyr.profdata" ^
    "%ZEPHYR_ROOT%" 2>&1
if errorlevel 1 (
    echo ERROR: CMake PGO configure failed
    popd
    exit /b 1
)

%CMAKE% --build . --config Release 2>&1
if errorlevel 1 (
    echo ERROR: PGO build failed
    popd
    exit /b 1
)
popd

if exist "%ZEPHYR_ROOT%\build\zephyr.exe" (
    echo.
    echo === PGO Build Complete ===
    echo   %ZEPHYR_ROOT%\build\zephyr.exe
) else (
    echo ERROR: PGO zephyr.exe not found
    exit /b 1
)
