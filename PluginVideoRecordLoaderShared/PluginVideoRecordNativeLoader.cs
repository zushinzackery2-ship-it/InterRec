using System;
using System.IO;
using System.Runtime.InteropServices;

namespace PluginVideoRecordLoaderShared
{
    internal static class PluginVideoRecordNativeLoader
    {
        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string fileName);

        internal static bool TryLoad(
            ref IntPtr nativeModuleHandle,
            string assemblyLocation,
            Action<string> logInfo,
            Action<string> logError)
        {
            if (nativeModuleHandle != IntPtr.Zero)
            {
                logInfo("原生录屏模块已经处于加载状态。");
                return true;
            }

            if (string.IsNullOrWhiteSpace(assemblyLocation))
            {
                logError("无法确定插件程序集路径。");
                return false;
            }

            // 统一从插件程序集所在目录找原生 DLL，避免 Mono / IL2CPP 两套 Loader 依赖不同的 BepInEx API。
            string pluginDirectory = Path.GetDirectoryName(assemblyLocation) ?? string.Empty;
            if (string.IsNullOrWhiteSpace(pluginDirectory))
            {
                logError($"无法确定插件目录: {assemblyLocation}");
                return false;
            }

            string nativeDllPath = Path.Combine(pluginDirectory, "PluginVideoRecordHook.dll");
            if (!File.Exists(nativeDllPath))
            {
                logError($"找不到原生录屏模块: {nativeDllPath}");
                return false;
            }

            nativeModuleHandle = LoadLibraryW(nativeDllPath);
            if (nativeModuleHandle == IntPtr.Zero)
            {
                int lastError = Marshal.GetLastWin32Error();
                logError($"LoadLibrary 失败，错误码: {lastError}");
                return false;
            }

            logInfo($"已加载原生录屏模块: {nativeDllPath}");
            return true;
        }
    }
}
