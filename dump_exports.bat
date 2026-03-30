@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
cd /d "%~dp0"
"D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe" /exports "bin\x64\Release\PluginVideoRecordHook.dll"
echo.
"D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe" /exports "bin\x64\Release\PluginVideoRecordVkLayer.dll"
exit /b %errorlevel%
