#include "pch.h"

#include "PluginVideoRecordWasapiRenderHookInternal.h"

namespace
{
    bool IsReadableRange(const void* address, size_t size)
    {
        if (!address || size == 0)
        {
            return false;
        }

        auto current = static_cast<const std::uint8_t*>(address);
        const auto end = current + size;
        while (current < end)
        {
            MEMORY_BASIC_INFORMATION memory = {};
            if (VirtualQuery(current, &memory, sizeof(memory)) != sizeof(memory))
            {
                return false;
            }

            if (memory.State != MEM_COMMIT ||
                (memory.Protect & PAGE_GUARD) != 0 ||
                (memory.Protect & PAGE_NOACCESS) != 0)
            {
                return false;
            }

            const auto regionEnd =
                static_cast<const std::uint8_t*>(memory.BaseAddress) + memory.RegionSize;
            if (regionEnd <= current)
            {
                return false;
            }

            current = regionEnd;
        }

        return true;
    }
}

namespace PluginVideoRecord::WasapiRenderHookInternal
{
    HookRuntime& Runtime()
    {
        static HookRuntime runtime;
        return runtime;
    }

    std::wstring BuildHresultText(const wchar_t* text, HRESULT hr)
    {
        wchar_t buffer[256] = {};
        swprintf_s(buffer, L"%ls HRESULT=0x%08X", text, static_cast<unsigned int>(hr));
        return buffer;
    }

    std::wstring BuildWin32Text(const wchar_t* text, DWORD error)
    {
        wchar_t buffer[256] = {};
        swprintf_s(buffer, L"%ls Win32=%lu", text, static_cast<unsigned long>(error));
        return buffer;
    }

    bool TryCopyMemory(void* destination, const void* source, size_t size)
    {
        if (!destination || !source || size == 0 || !IsReadableRange(source, size))
        {
            return false;
        }

        __try
        {
            CopyMemory(destination, source, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryCopyMemoryCached(
        void* destination,
        const void* source,
        size_t size,
        const void*& cachedAddress,
        size_t& cachedBytes,
        bool& cachedValid)
    {
        if (!destination || !source || size == 0)
        {
            return false;
        }

        if (!cachedValid || cachedAddress != source || cachedBytes < size)
        {
            if (!IsReadableRange(source, size))
            {
                cachedValid = false;
                cachedAddress = nullptr;
                cachedBytes = 0;
                return false;
            }

            cachedAddress = source;
            cachedBytes = size;
            cachedValid = true;
        }

        __try
        {
            CopyMemory(destination, source, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            cachedValid = false;
            cachedAddress = nullptr;
            cachedBytes = 0;
            return false;
        }
    }

    bool TryGetVtable(void* object, void*** vtable)
    {
        if (!object || !vtable)
        {
            return false;
        }

        void** localVtable = nullptr;
        if (!TryCopyMemory(&localVtable, object, sizeof(localVtable)) || !localVtable)
        {
            return false;
        }

        *vtable = localVtable;
        return true;
    }

    bool PatchVtableSlot(
        void** vtable,
        size_t slotIndex,
        void* replacement,
        VtablePatch& patch,
        std::wstring& error)
    {
        if (!vtable || !replacement)
        {
            error = L"WASAPI vtable hook 参数无效。";
            return false;
        }

        void** slot = vtable + slotIndex;
        if (!IsReadableRange(slot, sizeof(void*)))
        {
            error = L"WASAPI vtable slot 不可读。";
            return false;
        }

        if (*slot == replacement)
        {
            patch.slot = slot;
            patch.replacement = replacement;
            patch.installed = true;
            return true;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            error = BuildWin32Text(L"WASAPI vtable slot 改写权限切换失败。", GetLastError());
            return false;
        }

        patch.slot = slot;
        patch.original = *slot;
        patch.replacement = replacement;
        *slot = replacement;

        DWORD ignoredProtect = 0;
        VirtualProtect(slot, sizeof(void*), oldProtect, &ignoredProtect);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
        patch.installed = true;
        return true;
    }

    void RestoreVtablePatch(VtablePatch& patch)
    {
        if (!patch.installed || !patch.slot)
        {
            return;
        }

        DWORD oldProtect = 0;
        if (VirtualProtect(patch.slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            if (*patch.slot == patch.replacement && patch.original)
            {
                *patch.slot = patch.original;
                FlushInstructionCache(GetCurrentProcess(), patch.slot, sizeof(void*));
            }

            DWORD ignoredProtect = 0;
            VirtualProtect(patch.slot, sizeof(void*), oldProtect, &ignoredProtect);
        }

        patch = {};
    }
}
