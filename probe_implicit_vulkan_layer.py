import ctypes
import json
import pathlib
import sys
import winreg


class VkLayerProperties(ctypes.Structure):
    _fields_ = [
        ("layerName", ctypes.c_char * 256),
        ("specVersion", ctypes.c_uint32),
        ("implementationVersion", ctypes.c_uint32),
        ("description", ctypes.c_char * 256),
    ]


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: probe_implicit_vulkan_layer.py <manifest-path>")
        return 2

    manifest_path = pathlib.Path(sys.argv[1]).resolve()
    print(f"[probe] manifest={manifest_path}")

    vulkan = ctypes.WinDLL("vulkan-1", use_last_error=True)

    vkEnumerateInstanceLayerProperties = vulkan.vkEnumerateInstanceLayerProperties
    vkEnumerateInstanceLayerProperties.argtypes = [
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(VkLayerProperties),
    ]
    vkEnumerateInstanceLayerProperties.restype = ctypes.c_int32

    key_path = "Software\\Khronos\\Vulkan\\ImplicitLayers"
    try:
        key = winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, key_path, 0, winreg.KEY_SET_VALUE)
    except OSError as exc:
        print(f"[probe] failed to open HKCU implicit layer key: {exc}")
        return 1

    try:
        try:
            winreg.SetValueEx(key, str(manifest_path), 0, winreg.REG_DWORD, 0)
        except OSError as exc:
            print(f"[probe] failed to set registry value: {exc}")
            return 1

        count = ctypes.c_uint32(0)
        result = vkEnumerateInstanceLayerProperties(ctypes.byref(count), None)
        print(f"[probe] enumerate count result={result} count={count.value}")
        if result != 0 or count.value == 0:
            return 1

        layers = (VkLayerProperties * count.value)()
        result = vkEnumerateInstanceLayerProperties(ctypes.byref(count), layers)
        print(f"[probe] enumerate list result={result} count={count.value}")
        found = False
        for index in range(count.value):
            name = layers[index].layerName.split(b"\0", 1)[0].decode("utf-8", errors="ignore")
            print(f"[probe] layer[{index}]={name}")
            if name == "VK_LAYER_PVRC_capture":
                found = True

        print(f"[probe] found_pvrc={int(found)}")
        return 0 if found else 3
    finally:
        try:
            winreg.DeleteValue(key, str(manifest_path))
        except OSError:
            pass
        winreg.CloseKey(key)


if __name__ == "__main__":
    raise SystemExit(main())
