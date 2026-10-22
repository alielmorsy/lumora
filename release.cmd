@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if "%LUMORA_BUILD_DIR%"=="" set LUMORA_BUILD_DIR=%~dp0build-release
cmake -S "%~dp0." -B "%LUMORA_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release >nul || exit /b 1
cmake --build "%LUMORA_BUILD_DIR%" %*
