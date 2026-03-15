#pragma once

#include <Windows.h>
#include <Shlwapi.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../Shared/PluginVideoRecordProtocol.h"
