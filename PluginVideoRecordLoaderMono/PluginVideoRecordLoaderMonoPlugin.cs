using System;
using BepInEx;
using PluginVideoRecordLoaderShared;

namespace PluginVideoRecordLoaderMono
{
    [BepInPlugin("rain.pluginvideorecord.loader.mono", "PluginVideoRecord Loader Mono", "1.0.0")]
    public sealed class PluginVideoRecordLoaderMonoPlugin : BaseUnityPlugin
    {
        private IntPtr nativeModuleHandle = IntPtr.Zero;

        private void Awake()
        {
            PluginVideoRecordNativeLoader.TryLoad(
                ref nativeModuleHandle,
                typeof(PluginVideoRecordLoaderMonoPlugin).Assembly.Location,
                Logger.LogInfo,
                Logger.LogError);
        }
    }
}
