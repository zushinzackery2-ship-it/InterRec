@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64

cd /d "%~dp0"

if not exist obj mkdir obj
if not exist obj\temp mkdir obj\temp
set TMP=%CD%\obj\temp
set TEMP=%CD%\obj\temp
set COMMON_BUILD_ARGS=/m:1 /p:Configuration=Release /p:Platform=x64

msbuild PluginVideoRecordHook\PluginVideoRecordHook.vcxproj %COMMON_BUILD_ARGS% "/p:SolutionDir=%CD%\\" %*
if errorlevel 1 goto :error

msbuild PluginVideoRecordVkLayer\PluginVideoRecordVkLayer.vcxproj %COMMON_BUILD_ARGS% "/p:SolutionDir=%CD%\\" %*
if errorlevel 1 goto :error

msbuild PluginVideoRecordController\PluginVideoRecordController.vcxproj %COMMON_BUILD_ARGS% "/p:SolutionDir=%CD%\\" %*
if errorlevel 1 goto :error

if not "%PluginVideoRecordMonoBepInExCoreDir%"=="" if not "%PluginVideoRecordMonoManagedDir%"=="" (
    if exist "%PluginVideoRecordMonoBepInExCoreDir%\BepInEx.dll" if exist "%PluginVideoRecordMonoManagedDir%\UnityEngine.CoreModule.dll" if exist "%PluginVideoRecordMonoManagedDir%\UnityEngine.dll" (
        msbuild PluginVideoRecordLoaderMono\PluginVideoRecordLoaderMono.csproj %COMMON_BUILD_ARGS% "/p:SolutionDir=%CD%\\" %*
        if errorlevel 1 goto :error
    ) else (
        echo [skip] PluginVideoRecordLoaderMono references are incomplete.
    )
) else (
    echo [skip] PluginVideoRecordLoaderMono requires local Mono BepInEx/Unity reference paths.
)

exit /b 0

:error
exit /b %errorlevel%
