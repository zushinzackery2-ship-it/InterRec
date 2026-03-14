#include "pch.h"

#include "PluginVideoRecordMainWindow.h"

int APIENTRY wWinMain(HINSTANCE instanceHandle, HINSTANCE, LPWSTR, int showCommand)
{
    PluginVideoRecord::PluginVideoRecordMainWindow mainWindow;
    return mainWindow.Run(instanceHandle, showCommand);
}
