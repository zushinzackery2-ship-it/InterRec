@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist obj\temp mkdir obj\temp
set TMP=%CD%\obj\temp
set TEMP=%CD%\obj\temp
set COMMON_BUILD_ARGS=/m:1 /p:Configuration=Release /p:Platform=x64
msbuild PluginVideoRecordHook\PluginVideoRecordHook.vcxproj %COMMON_BUILD_ARGS% "/p:SolutionDir=%CD%\\"
if errorlevel 1 exit /b %errorlevel%
msbuild PluginVideoRecordController\PluginVideoRecordController.vcxproj %COMMON_BUILD_ARGS% "/p:SolutionDir=%CD%\\"
if errorlevel 1 exit /b %errorlevel%
call .\pack.bat
exit /b %errorlevel%
