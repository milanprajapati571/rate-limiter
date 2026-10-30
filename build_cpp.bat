@echo off
setlocal EnableDelayedExpansion

echo =====================================================================
echo                RateLimiter C++ Build Script              
echo =====================================================================

cd /d "%~dp0"

echo [1/2] Configuring CMake project...
cmake -B cpp_rate_limiter/build -S cpp_rate_limiter
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed.
    exit /b %ERRORLEVEL%
)

echo [2/2] Compiling C++ Rate Limiter (Release)...
cmake --build cpp_rate_limiter/build --config Release
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed.
    exit /b %ERRORLEVEL%
)

echo =====================================================================
echo [SUCCESS] Build completed successfully!
echo Executable located at: cpp_rate_limiter\build\Release\cpp_rate_limiter.exe
echo =====================================================================
