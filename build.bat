@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64

cd /d "%~dp0"

msbuild PluginVideoRecord.sln /m:1 /p:Configuration=Release /p:Platform=x64 %*
exit /b %errorlevel%
