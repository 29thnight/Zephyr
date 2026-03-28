Set-Location 'C:\Users\lance\OneDrive\Documents\Project Zephyr'

# 벤치마크로 프로파일 수집
Write-Host "Running benchmark for PGO profile..."
.\x64\Release\zephyr_bench.exe

# CLI로 워크로드 실행
Write-Host "Running CLI workload for PGO profile..."
1..10 | ForEach-Object {
    .\x64\Release\zephyr_cli.exe run docs\pgo_workload.zph
}

# .pgc 파일 확인
Write-Host "Checking for .pgc files..."
Get-ChildItem -Path "x64\Release" -Filter "*.pgc" | Select-Object Name, Length
