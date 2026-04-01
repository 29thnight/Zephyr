@echo off
REM 3-Way Benchmark: Build Lua and Gravity with MSVC
REM Run from: bench\3way\

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvarsall.bat failed
    exit /b 1
)

if not defined LUA_SRC set LUA_SRC=C:\Users\lance\Downloads\lua-5.5.0\src
if not defined GRAVITY_SRC set GRAVITY_SRC=C:\Users\lance\Downloads\gravity-master
set BENCH_DIR=%~dp0

REM ============================================================
REM Build Lua 5.5.0
REM ============================================================
echo.
echo === Building Lua 5.5.0 ===
if not exist "%BENCH_DIR%lua-5.5.0" mkdir "%BENCH_DIR%lua-5.5.0"

pushd "%BENCH_DIR%lua-5.5.0"

set LUA_CORE=lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c
set LUA_LIB=lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c
set LUA_MAIN=lua.c

echo Compiling Lua sources...
for %%f in (%LUA_CORE% %LUA_LIB% %LUA_MAIN%) do (
    cl /nologo /O2 /MD /I"%LUA_SRC%" /c "%LUA_SRC%\%%f"
    if errorlevel 1 (
        echo ERROR: Failed to compile %%f
        popd
        exit /b 1
    )
)

echo Linking lua.exe...
link /nologo /OUT:lua.exe *.obj 2>&1
if errorlevel 1 (
    echo ERROR: Failed to link lua.exe
    popd
    exit /b 1
)

echo Lua built successfully: %BENCH_DIR%lua-5.5.0\lua.exe
popd

REM ============================================================
REM Build Gravity
REM ============================================================
echo.
echo === Building Gravity ===
if not exist "%BENCH_DIR%gravity" mkdir "%BENCH_DIR%gravity"

pushd "%BENCH_DIR%gravity"

set CMAKE="%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

echo Configuring Gravity with CMake...
%CMAKE% -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "%GRAVITY_SRC%" 2>&1
if errorlevel 1 (
    echo ERROR: CMake configure failed
    popd
    exit /b 1
)

echo Building Gravity...
%CMAKE% --build . --config Release 2>&1
if errorlevel 1 (
    echo ERROR: Gravity build failed
    popd
    exit /b 1
)

echo Gravity built successfully.
popd

echo.
echo === Build Complete ===
if exist "%BENCH_DIR%lua-5.5.0\lua.exe" echo   Lua:     %BENCH_DIR%lua-5.5.0\lua.exe
if exist "%BENCH_DIR%gravity\gravity.exe" echo   Gravity: %BENCH_DIR%gravity\gravity.exe
echo.
echo Run benchmarks with: python runner.py
