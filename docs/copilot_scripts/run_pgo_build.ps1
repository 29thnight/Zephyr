$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

if (-not (Test-Path $msbuild)) {
    throw "MSBuild not found at $msbuild"
}

& $msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGInstrument /v:minimal
if ($LASTEXITCODE -ne 0) {
    throw "PGInstrument build failed."
}

& "x64\Release\zephyr_bench.exe"
if ($LASTEXITCODE -ne 0) {
    throw "Benchmark run for PGO profiling failed."
}

& $msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGOptimize /v:minimal
if ($LASTEXITCODE -ne 0) {
    throw "PGOptimize build failed."
}
