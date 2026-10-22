@echo off
rem Usage: build.cmd [cmake --build args]. LUMORA_PRESET picks debug (default) or release;
rem LUMORA_BUILD_DIR overrides the preset's build\<preset> folder.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if "%LUMORA_PRESET%"=="" set LUMORA_PRESET=debug
if "%LUMORA_BUILD_DIR%"=="" set LUMORA_BUILD_DIR=%~dp0..\build\%LUMORA_PRESET%
pushd "%~dp0.."
cmake --preset %LUMORA_PRESET% -B "%LUMORA_BUILD_DIR%" >nul
set _err=%errorlevel%
popd
if not "%_err%"=="0" exit /b %_err%
cmake --build "%LUMORA_BUILD_DIR%" %*
