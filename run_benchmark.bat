@echo off
setlocal

echo =====================================================================
echo           Distributed Rate Limiter - Automated Benchmark Suite        
echo =====================================================================

cd /d "%~dp0"

where python >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Python was not found in your PATH. Please install Python 3.8+.
    exit /b 1
)

echo Running functional validation and high-concurrency benchmark...
python tests/benchmark.py %*

echo =====================================================================
echo Benchmark execution finished.
echo =====================================================================
