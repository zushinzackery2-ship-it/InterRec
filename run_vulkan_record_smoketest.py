#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import os
import pathlib
import subprocess
import sys
import tempfile
import time
KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)

PROCESS_CREATE_THREAD = 0x0002
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_WRITE = 0x0020
PROCESS_VM_READ = 0x0010

MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
WAIT_OBJECT_0 = 0x00000000
INFINITE = 0xFFFFFFFF
FILE_MAP_ALL_ACCESS = 0xF001F
FILE_MAP_READ = 0x0004
EVENT_MODIFY_STATE = 0x0002

PROTOCOL_VERSION = 2
SESSION_REGISTRY_NAME = "Local\\PluginVideoRecord.Registry"
STATE_MAPPING_PREFIX = "Local\\PluginVideoRecord.State."
COMMAND_EVENT_PREFIX = "Local\\PluginVideoRecord.Command."
LOG_MAPPING_PREFIX = "Local\\PluginVideoRecord.Log."

KERNEL32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
KERNEL32.OpenProcess.restype = wintypes.HANDLE
KERNEL32.VirtualAllocEx.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    ctypes.c_size_t,
    wintypes.DWORD,
    wintypes.DWORD,
]
KERNEL32.VirtualAllocEx.restype = wintypes.LPVOID
KERNEL32.WriteProcessMemory.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    wintypes.LPCVOID,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
KERNEL32.WriteProcessMemory.restype = wintypes.BOOL
KERNEL32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
KERNEL32.GetModuleHandleW.restype = wintypes.HMODULE
KERNEL32.GetProcAddress.argtypes = [wintypes.HMODULE, wintypes.LPCSTR]
KERNEL32.GetProcAddress.restype = wintypes.LPVOID
KERNEL32.CreateRemoteThread.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    ctypes.c_size_t,
    wintypes.LPVOID,
    wintypes.LPVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
]
KERNEL32.CreateRemoteThread.restype = wintypes.HANDLE
KERNEL32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
KERNEL32.WaitForSingleObject.restype = wintypes.DWORD
KERNEL32.GetExitCodeThread.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
KERNEL32.GetExitCodeThread.restype = wintypes.BOOL
KERNEL32.VirtualFreeEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD]
KERNEL32.VirtualFreeEx.restype = wintypes.BOOL
KERNEL32.CloseHandle.argtypes = [wintypes.HANDLE]
KERNEL32.CloseHandle.restype = wintypes.BOOL
KERNEL32.OpenFileMappingW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
KERNEL32.OpenFileMappingW.restype = wintypes.HANDLE
KERNEL32.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]
KERNEL32.MapViewOfFile.restype = wintypes.LPVOID
KERNEL32.UnmapViewOfFile.argtypes = [wintypes.LPCVOID]
KERNEL32.UnmapViewOfFile.restype = wintypes.BOOL
KERNEL32.OpenEventW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
KERNEL32.OpenEventW.restype = wintypes.HANDLE
KERNEL32.SetEvent.argtypes = [wintypes.HANDLE]
KERNEL32.SetEvent.restype = wintypes.BOOL

USER32 = ctypes.WinDLL("user32", use_last_error=True)
USER32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
USER32.FindWindowW.restype = wintypes.HWND
USER32.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
USER32.FindWindowExW.restype = wintypes.HWND
USER32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
USER32.SendMessageW.restype = wintypes.LPARAM
USER32.IsWindowEnabled.argtypes = [wintypes.HWND]
USER32.IsWindowEnabled.restype = wintypes.BOOL

BM_CLICK = 0x00F5


class SharedState(ctypes.Structure):
    _fields_ = [
        ("protocolVersion", ctypes.c_long),
        ("stateSequence", ctypes.c_long),
        ("connectionState", ctypes.c_long),
        ("recorderState", ctypes.c_long),
        ("availableBackendMask", ctypes.c_long),
        ("selectedBackend", ctypes.c_long),
        ("activeRecordingBackend", ctypes.c_long),
        ("commandSequence", ctypes.c_long),
        ("commandType", ctypes.c_long),
        ("acknowledgedSequence", ctypes.c_long),
        ("lastHandledCommandType", ctypes.c_long),
        ("lastErrorCode", ctypes.c_long),
        ("processId", ctypes.c_long),
        ("heartbeatTickCount", ctypes.c_longlong),
        ("currentOutputPath", ctypes.c_wchar * 520),
        ("lastErrorMessage", ctypes.c_wchar * 256),
    ]


class SessionRegistrySlot(ctypes.Structure):
    _fields_ = [
        ("occupied", ctypes.c_long),
        ("targetPid", ctypes.c_long),
        ("controllerPid", ctypes.c_long),
        ("reserved", ctypes.c_long),
        ("sessionId", ctypes.c_longlong),
        ("targetProcessStartTime", ctypes.c_longlong),
        ("targetHeartbeatTickCount", ctypes.c_longlong),
        ("controllerHeartbeatTickCount", ctypes.c_longlong),
    ]


class SessionRegistry(ctypes.Structure):
    _fields_ = [
        ("protocolVersion", ctypes.c_long),
        ("slots", SessionRegistrySlot * 16),
    ]


class LogEntry(ctypes.Structure):
    _fields_ = [
        ("sequence", ctypes.c_long),
        ("text", ctypes.c_wchar * 256),
    ]


class LogBuffer(ctypes.Structure):
    _fields_ = [
        ("protocolVersion", ctypes.c_long),
        ("writeSequence", ctypes.c_long),
        ("entries", LogEntry * 256),
    ]


def inject_dll(process_id: int, dll_path: pathlib.Path) -> None:
    access = (
        PROCESS_CREATE_THREAD
        | PROCESS_QUERY_INFORMATION
        | PROCESS_VM_OPERATION
        | PROCESS_VM_WRITE
        | PROCESS_VM_READ
    )
    process_handle = KERNEL32.OpenProcess(access, False, process_id)
    if not process_handle:
        raise ctypes.WinError(ctypes.get_last_error())

    remote_buffer = None
    thread_handle = None
    try:
        dll_buffer = ctypes.create_unicode_buffer(str(dll_path))
        byte_count = ctypes.sizeof(dll_buffer)
        remote_buffer = KERNEL32.VirtualAllocEx(process_handle, None, byte_count, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
        if not remote_buffer:
            raise ctypes.WinError(ctypes.get_last_error())

        written = ctypes.c_size_t(0)
        if not KERNEL32.WriteProcessMemory(
            process_handle,
            remote_buffer,
            ctypes.byref(dll_buffer),
            byte_count,
            ctypes.byref(written),
        ):
            raise ctypes.WinError(ctypes.get_last_error())

        kernel32_module = KERNEL32.GetModuleHandleW("kernel32.dll")
        load_library = KERNEL32.GetProcAddress(kernel32_module, b"LoadLibraryW")
        thread_id = wintypes.DWORD(0)
        thread_handle = KERNEL32.CreateRemoteThread(
            process_handle,
            None,
            0,
            load_library,
            remote_buffer,
            0,
            ctypes.byref(thread_id),
        )
        wait_result = KERNEL32.WaitForSingleObject(thread_handle, INFINITE)
        if wait_result != WAIT_OBJECT_0:
            raise RuntimeError(f"WaitForSingleObject failed: {wait_result:#x}")

        exit_code = wintypes.DWORD(0)
        if not KERNEL32.GetExitCodeThread(thread_handle, ctypes.byref(exit_code)):
            raise ctypes.WinError(ctypes.get_last_error())
        if exit_code.value == 0:
            raise RuntimeError("Remote LoadLibraryW returned null")
    finally:
        if thread_handle:
            KERNEL32.CloseHandle(thread_handle)
        if remote_buffer:
            KERNEL32.VirtualFreeEx(process_handle, remote_buffer, 0, MEM_RELEASE)
        KERNEL32.CloseHandle(process_handle)


def resolve_session_id(target_pid: int, timeout_seconds: float) -> int:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        registry_mapping = KERNEL32.OpenFileMappingW(FILE_MAP_ALL_ACCESS, False, SESSION_REGISTRY_NAME)
        if registry_mapping:
            registry_view = KERNEL32.MapViewOfFile(
                registry_mapping, FILE_MAP_ALL_ACCESS, 0, 0, ctypes.sizeof(SessionRegistry)
            )
            if registry_view:
                try:
                    registry = ctypes.cast(registry_view, ctypes.POINTER(SessionRegistry)).contents
                    if registry.protocolVersion == PROTOCOL_VERSION:
                        for slot in registry.slots:
                            if slot.occupied and slot.sessionId != 0 and slot.targetPid == target_pid:
                                return int(slot.sessionId)
                finally:
                    KERNEL32.UnmapViewOfFile(registry_view)
            KERNEL32.CloseHandle(registry_mapping)
        time.sleep(0.2)

    raise RuntimeError("session registry not available")


def connect_ipc(session_id: int) -> tuple[wintypes.HANDLE, ctypes.POINTER(SharedState), wintypes.HANDLE]:
    state_name = f"{STATE_MAPPING_PREFIX}{session_id:016X}"
    event_name = f"{COMMAND_EVENT_PREFIX}{session_id:016X}"

    mapping = KERNEL32.OpenFileMappingW(FILE_MAP_ALL_ACCESS, False, state_name)
    if not mapping:
        raise RuntimeError(f"state mapping not available: {state_name}")

    view = KERNEL32.MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, ctypes.sizeof(SharedState))
    event = KERNEL32.OpenEventW(EVENT_MODIFY_STATE, False, event_name)
    if view and event:
        state_ptr = ctypes.cast(view, ctypes.POINTER(SharedState))
        return mapping, state_ptr, event

    if view:
        KERNEL32.UnmapViewOfFile(view)
    KERNEL32.CloseHandle(mapping)
    raise RuntimeError(f"command event not available: {event_name}")


def read_logs(session_id: int) -> list[str]:
    mapping = KERNEL32.OpenFileMappingW(FILE_MAP_READ, False, f"{LOG_MAPPING_PREFIX}{session_id:016X}")
    if not mapping:
        return []

    try:
        view = KERNEL32.MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, ctypes.sizeof(LogBuffer))
        if not view:
            return []

        try:
            log_buffer = ctypes.cast(view, ctypes.POINTER(LogBuffer)).contents
            lines: list[str] = []
            if log_buffer.protocolVersion != PROTOCOL_VERSION:
                return lines

            write_sequence = log_buffer.writeSequence
            first_sequence = max(1, write_sequence - 255)
            for sequence in range(first_sequence, write_sequence + 1):
                entry = log_buffer.entries[(sequence - 1) % 256]
                if entry.sequence == sequence and entry.text:
                    lines.append(entry.text)
            return lines
        finally:
            KERNEL32.UnmapViewOfFile(view)
    finally:
        KERNEL32.CloseHandle(mapping)


def send_command(state_ptr: ctypes.POINTER(SharedState), event_handle: wintypes.HANDLE, command_type: int) -> None:
    base_address = ctypes.addressof(state_ptr.contents)
    command_type_ref = ctypes.c_long.from_address(base_address + SharedState.commandType.offset)
    command_sequence_ref = ctypes.c_long.from_address(base_address + SharedState.commandSequence.offset)
    next_sequence = command_sequence_ref.value + 1
    command_type_ref.value = command_type
    command_sequence_ref.value = next_sequence
    if not KERNEL32.SetEvent(event_handle):
        raise ctypes.WinError(ctypes.get_last_error())


def build_vulkan_layer_manifest(dll_path: pathlib.Path) -> str:
    escaped_path = str(dll_path).replace("\\", "\\\\")
    return (
        "{\n"
        '  "file_format_version": "1.1.2",\n'
        '  "layer": {\n'
        '    "name": "VK_LAYER_PVRC_capture",\n'
        '    "type": "GLOBAL",\n'
        f'    "library_path": "{escaped_path}",\n'
        '    "api_version": "1.3.0",\n'
        '    "implementation_version": "1",\n'
        '    "description": "PVRC Vulkan capture layer",\n'
        '    "functions": {\n'
        '      "vkNegotiateLoaderLayerInterfaceVersion": "vkNegotiateLoaderLayerInterfaceVersion",\n'
        '      "vkGetInstanceProcAddr": "vkGetInstanceProcAddr",\n'
        '      "vkGetDeviceProcAddr": "vkGetDeviceProcAddr"\n'
        "    },\n"
        '    "disable_environment": {\n'
        '      "PVRC_DISABLE_VULKAN_LAYER": "1"\n'
        "    }\n"
        "  }\n"
        "}\n"
    )


def ensure_vulkan_layer_manifest(manifest_path: pathlib.Path, dll_path: pathlib.Path) -> pathlib.Path:
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    desired_text = build_vulkan_layer_manifest(dll_path)
    existing_text = manifest_path.read_text(encoding="utf-8") if manifest_path.exists() else None
    if existing_text != desired_text:
        manifest_path.write_text(desired_text, encoding="utf-8")
    return manifest_path


def build_parser() -> argparse.ArgumentParser:
    repo_root = pathlib.Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Smoke test Vulkan recording via InterRec IPC")
    parser.add_argument("--exe", default="")
    parser.add_argument("--dll", default=str(repo_root / "bin" / "x64" / "Release" / "PluginVideoRecordHook.dll"))
    parser.add_argument("--layer-dll", default=str(repo_root / "bin" / "x64" / "Release" / "PluginVideoRecordVkLayer.dll"))
    parser.add_argument("--controller", default=str(repo_root / "bin" / "x64" / "Release" / "PluginVideoRecordController.exe"))
    parser.add_argument("--manifest", default="")
    parser.add_argument("--startup-delay", type=float, default=1.0)
    parser.add_argument("--record-seconds", type=float, default=3.0)
    parser.add_argument("--ipc-timeout", type=float, default=10.0)
    parser.add_argument("--backend-timeout", type=float, default=10.0)
    parser.add_argument("--direct-ipc", action="store_true")
    parser.add_argument("--no-layer", action="store_true")
    return parser


def wait_window(title: str, timeout_seconds: float) -> wintypes.HWND:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        hwnd = USER32.FindWindowW("PluginVideoRecordControllerWindow", title)
        if hwnd:
            return hwnd
        time.sleep(0.2)

    raise RuntimeError(f"controller window not found: {title}")


def click_button(parent: wintypes.HWND, text: str) -> None:
    button = USER32.FindWindowExW(parent, None, "Button", text)
    if not button:
        raise RuntimeError(f"button not found: {text}")
    USER32.SendMessageW(button, BM_CLICK, 0, 0)


def wait_button_enabled(parent: wintypes.HWND, text: str, timeout_seconds: float) -> wintypes.HWND:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        button = USER32.FindWindowExW(parent, None, "Button", text)
        if button and USER32.IsWindowEnabled(button):
            return button
        time.sleep(0.1)

    raise RuntimeError(f"button not enabled: {text}")


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if not args.exe:
        raise SystemExit("--exe is required for smoketest target")

    exe_path = pathlib.Path(args.exe).resolve()
    dll_path = pathlib.Path(args.dll).resolve()
    layer_dll_path = pathlib.Path(args.layer_dll).resolve()
    workdir = exe_path.parent

    process: subprocess.Popen[str] | None = None
    controller_process: subprocess.Popen[str] | None = None
    mapping = None
    state_ptr = None
    event_handle = None
    try:
        launch_env = None
        if not args.no_layer:
            manifest_path = (
                pathlib.Path(args.manifest).resolve()
                if args.manifest
                else pathlib.Path(tempfile.gettempdir()) / "InterRecSmoke" / "PluginVideoRecordVkLayer.json"
            )
            manifest_path = ensure_vulkan_layer_manifest(manifest_path, layer_dll_path)
            launch_env = dict(os.environ)
            launch_env["VK_LAYER_PATH"] = str(manifest_path.parent)
            launch_env["VK_INSTANCE_LAYERS"] = "VK_LAYER_PVRC_capture"
            launch_env.pop("PVRC_DISABLE_VULKAN_LAYER", None)
            print(f"[smoke] Vulkan explicit layer enabled: {manifest_path}")

        process = subprocess.Popen([str(exe_path)], cwd=str(workdir), env=launch_env)
        time.sleep(max(args.startup_delay, 0.0))
        if args.no_layer:
            inject_dll(process.pid, dll_path)
            print(f"[smoke] injected hook dll: {dll_path}")

        session_id = resolve_session_id(process.pid, args.ipc_timeout)
        mapping, state_ptr, event_handle = connect_ipc(session_id)
        state = state_ptr.contents
        print(
            f"[smoke] session={session_id:016X} "
            f"IPC connected: availableMask={state.availableBackendMask} "
            f"selected={state.selectedBackend} active={state.activeRecordingBackend} "
            f"state={state.recorderState}"
        )

        backend_deadline = time.time() + max(args.backend_timeout, 0.0)
        while time.time() < backend_deadline:
            state = state_ptr.contents
            if state.availableBackendMask != 0:
                break
            time.sleep(0.2)

        state = state_ptr.contents
        print(
            f"[smoke] backend after wait: availableMask={state.availableBackendMask} "
            f"selected={state.selectedBackend} active={state.activeRecordingBackend} "
            f"state={state.recorderState}"
        )

        if args.direct_ipc:
            send_command(state_ptr, event_handle, 1)
            print("[smoke] start command sent")
        else:
            controller_path = pathlib.Path(args.controller).resolve()
            controller_process = subprocess.Popen([str(controller_path)], cwd=str(controller_path.parent))
            controller_hwnd = wait_window("InterRec Controller", 10.0)
            wait_button_enabled(controller_hwnd, "开始录制", 5.0)
            click_button(controller_hwnd, "开始录制")
            print("[smoke] start button clicked via controller")
        time.sleep(0.8)
        state = state_ptr.contents
        print(
            f"[smoke] post-start availableMask={state.availableBackendMask} selected={state.selectedBackend} "
            f"active={state.activeRecordingBackend} recorderState={state.recorderState} "
            f"cmdSeq={state.commandSequence} ack={state.acknowledgedSequence} handled={state.lastHandledCommandType} "
            f"output={state.currentOutputPath} error={state.lastErrorMessage}"
        )
        time.sleep(max(args.record_seconds, 0.0))
        if args.direct_ipc:
            send_command(state_ptr, event_handle, 2)
            print("[smoke] stop command sent")
        else:
            controller_hwnd = wait_window("InterRec Controller", 5.0)
            wait_button_enabled(controller_hwnd, "结束录制", 5.0)
            click_button(controller_hwnd, "结束录制")
            print("[smoke] stop button clicked via controller")
        time.sleep(3.0)

        state = state_ptr.contents
        output_path = pathlib.Path(state.currentOutputPath) if state.currentOutputPath else None
        print(
            f"[smoke] final availableMask={state.availableBackendMask} selected={state.selectedBackend} "
            f"active={state.activeRecordingBackend} recorderState={state.recorderState} "
            f"cmdSeq={state.commandSequence} ack={state.acknowledgedSequence} handled={state.lastHandledCommandType}"
        )
        print(f"[smoke] output={state.currentOutputPath}")
        print(f"[smoke] error={state.lastErrorMessage}")
        logs = read_logs(session_id)
        if logs:
            print("[smoke] recent logs:")
            for line in logs[-20:]:
                print(line)
        if output_path and output_path.exists():
            print(f"[smoke] output exists, size={output_path.stat().st_size}")
        else:
            print("[smoke] output file missing")
    finally:
        if state_ptr:
            KERNEL32.UnmapViewOfFile(state_ptr)
        if mapping:
            KERNEL32.CloseHandle(mapping)
        if event_handle:
            KERNEL32.CloseHandle(event_handle)
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        if controller_process and controller_process.poll() is None:
            controller_process.terminate()
            try:
                controller_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                controller_process.kill()
                controller_process.wait(timeout=5)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
