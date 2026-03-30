@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
cd /d "%~dp0"
msbuild PluginVideoRecordController\PluginVideoRecordController.vcxproj /m:1 /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=%CD%\\"
exit /b %errorlevel%
