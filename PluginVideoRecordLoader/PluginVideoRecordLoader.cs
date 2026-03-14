using System;
using System.IO;
using System.Runtime.InteropServices;
using BepInEx;

namespace PluginVideoRecordLoader
{
    [BepInPlugin("rain.pluginvideorecord.loader", "PluginVideoRecord Loader", "1.0.0")]
    public sealed class PluginVideoRecordLoaderPlugin : BaseUnityPlugin
    {
        private IntPtr nativeModuleHandle = IntPtr.Zero;

        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string fileName);

        private void Awake()
        {
            string pluginDirectory = Path.GetDirectoryName(Info.Location) ?? string.Empty;
            string nativeDllPath = Path.Combine(pluginDirectory, "PluginVideoRecordHook.dll");
            if (!File.Exists(nativeDllPath))
            {
                Logger.LogError($"找不到原生录屏模块: {nativeDllPath}");
                return;
            }

            nativeModuleHandle = LoadLibraryW(nativeDllPath);
            if (nativeModuleHandle == IntPtr.Zero)
            {
                int lastError = Marshal.GetLastWin32Error();
                Logger.LogError($"LoadLibrary 失败，错误码: {lastError}");
                return;
            }

            Logger.LogInfo($"已加载原生录屏模块: {nativeDllPath}");
        }
    }
}
