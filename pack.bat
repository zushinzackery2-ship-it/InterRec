@echo off
setlocal

cd /d "%~dp0"

set PACKED_DIR=packed
set RUNTIME_DIR=%PACKED_DIR%\runtime
set SYMBOLS_DIR=%PACKED_DIR%\symbols
set SOURCE_DIR=bin\x64\Release

if exist "%PACKED_DIR%" rd /s /q "%PACKED_DIR%"
mkdir "%PACKED_DIR%"
mkdir "%RUNTIME_DIR%"
mkdir "%SYMBOLS_DIR%"

copy /y "%SOURCE_DIR%\PluginVideoRecordHook.dll" "%RUNTIME_DIR%\" >nul
if errorlevel 1 goto :error

copy /y "%SOURCE_DIR%\PluginVideoRecordVkLayer.dll" "%RUNTIME_DIR%\" >nul
if errorlevel 1 goto :error

copy /y "%SOURCE_DIR%\PluginVideoRecordController.exe" "%RUNTIME_DIR%\" >nul
if errorlevel 1 goto :error

if exist "%SOURCE_DIR%\PluginVideoRecordLoaderMono.dll" (
    copy /y "%SOURCE_DIR%\PluginVideoRecordLoaderMono.dll" "%RUNTIME_DIR%\" >nul
)

copy /y "%SOURCE_DIR%\PluginVideoRecordHook.pdb" "%SYMBOLS_DIR%\" >nul
if errorlevel 1 goto :error

copy /y "%SOURCE_DIR%\PluginVideoRecordVkLayer.pdb" "%SYMBOLS_DIR%\" >nul
if errorlevel 1 goto :error

copy /y "%SOURCE_DIR%\PluginVideoRecordController.pdb" "%SYMBOLS_DIR%\" >nul
if errorlevel 1 goto :error

copy /y "README_PACKED.txt" "%PACKED_DIR%\" >nul
if errorlevel 1 goto :error

echo === Pack OK: %PACKED_DIR% ===
exit /b 0

:error
echo === Pack FAILED ===
exit /b 1
