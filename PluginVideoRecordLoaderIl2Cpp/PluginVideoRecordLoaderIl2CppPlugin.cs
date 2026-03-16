using System;
using BepInEx;
using BepInEx.Unity.IL2CPP;
using PluginVideoRecordLoaderShared;

namespace PluginVideoRecordLoaderIl2Cpp
{
    [BepInPlugin("rain.pluginvideorecord.loader.il2cpp", "PluginVideoRecord Loader IL2CPP", "1.0.0")]
    public sealed class PluginVideoRecordLoaderIl2CppPlugin : BasePlugin
    {
        private IntPtr nativeModuleHandle = IntPtr.Zero;

        public override void Load()
        {
            PluginVideoRecordNativeLoader.TryLoad(
                ref nativeModuleHandle,
                typeof(PluginVideoRecordLoaderIl2CppPlugin).Assembly.Location,
                Log.LogInfo,
                Log.LogError);
        }
    }
}
