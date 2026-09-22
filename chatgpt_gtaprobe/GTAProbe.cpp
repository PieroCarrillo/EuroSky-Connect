#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "MinHook.h"

namespace {
constexpr uintptr_t kPreferredBase = 0x00400000u;
constexpr uintptr_t kLaunchBunnyHopVA = 0x006C0390u;

// GTA SA 1.0 US globals (plugin-sdk verified)
constexpr uintptr_t kFrameCounterVA     = 0x00B7CB4Cu;
constexpr uintptr_t kGameFpsVA          = 0x00B7CB50u;
constexpr uintptr_t kTimeStepNcVA       = 0x00B7CB58u;
constexpr uintptr_t kTimeStepVA         = 0x00B7CB5Cu;
constexpr uintptr_t kGameMsVA           = 0x00B7CB84u;

// CPhysical offsets
constexpr size_t kMoveSpeedOff = 0x44;
constexpr size_t kTurnSpeedOff = 0x50;

// CAnimBlendAssociation offsets
constexpr size_t kAnimCurrentTimeOff = 0x20;
constexpr size_t kAnimSpeedOff       = 0x24;
constexpr size_t kAnimTimeStepOff    = 0x28;
constexpr size_t kAnimIdOff          = 0x2C;

struct Vec3 { float x, y, z; };

using LaunchBunnyHopFn = void (__cdecl*)(void* blendAssoc, void* data);
LaunchBunnyHopFn g_originalBunny = nullptr;

HANDLE g_log = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_logCs{};
LARGE_INTEGER g_qpcFreq{};
LARGE_INTEGER g_qpcStart{};
uintptr_t g_base = 0;

uintptr_t AtVA(uintptr_t preferredVA) {
    return g_base + (preferredVA - kPreferredBase);
}

void LogRaw(const char* text) {
    if (g_log == INVALID_HANDLE_VALUE || !text) return;
    EnterCriticalSection(&g_logCs);
    DWORD written = 0;
    WriteFile(g_log, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
    FlushFileBuffers(g_log);
    LeaveCriticalSection(&g_logCs);
}

void Logf(const char* fmt, ...) {
    char buf[4096]{};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    LogRaw(buf);
}

uint64_t QpcUs() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const LONGLONG d = now.QuadPart - g_qpcStart.QuadPart;
    if (g_qpcFreq.QuadPart <= 0) return 0;
    return static_cast<uint64_t>((d * 1000000LL) / g_qpcFreq.QuadPart);
}

template <typename T>
T ReadVA(uintptr_t va) {
    return *reinterpret_cast<volatile T*>(AtVA(va));
}

void BuildLogPath(char* out, size_t cap) {
    char exe[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char* slash = std::strrchr(exe, '\\');
    if (slash) *(slash + 1) = '\0';
    sprintf_s(out, cap, "%sGTAProbe.log", exe);
}

bool IsReadableAddress(const void* p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD prot = mbi.Protect & 0xFFu;
    if (prot == PAGE_NOACCESS || prot == PAGE_GUARD) return false;
    return true;
}

void __cdecl HookLaunchBunnyHop(void* blendAssoc, void* data) {
    const uint64_t tUs = QpcUs();

    uint32_t frame = 0, gameMs = 0;
    float fps = 0.0f, step = 0.0f, stepNc = 0.0f;

    Vec3 move0{}, turn0{}, move1{}, turn1{};
    float animCurrent = 0.0f, animSpeed = 0.0f, animStep = 0.0f;
    uint16_t animId = 0xFFFFu;
    bool snapOk = false;

    __try {
        frame  = ReadVA<uint32_t>(kFrameCounterVA);
        fps    = ReadVA<float>(kGameFpsVA);
        stepNc = ReadVA<float>(kTimeStepNcVA);
        step   = ReadVA<float>(kTimeStepVA);
        gameMs = ReadVA<uint32_t>(kGameMsVA);

        if (data) {
            move0 = *reinterpret_cast<Vec3*>(reinterpret_cast<uint8_t*>(data) + kMoveSpeedOff);
            turn0 = *reinterpret_cast<Vec3*>(reinterpret_cast<uint8_t*>(data) + kTurnSpeedOff);
        }

        if (blendAssoc) {
            animCurrent = *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(blendAssoc) + kAnimCurrentTimeOff);
            animSpeed   = *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(blendAssoc) + kAnimSpeedOff);
            animStep    = *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(blendAssoc) + kAnimTimeStepOff);
            animId      = *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(blendAssoc) + kAnimIdOff);
        }
        snapOk = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snapOk = false;
    }

    // Call GTA exactly once, with the untouched original arguments.
    g_originalBunny(blendAssoc, data);

    bool afterOk = false;
    __try {
        if (data) {
            move1 = *reinterpret_cast<Vec3*>(reinterpret_cast<uint8_t*>(data) + kMoveSpeedOff);
            turn1 = *reinterpret_cast<Vec3*>(reinterpret_cast<uint8_t*>(data) + kTurnSpeedOff);
            afterOk = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        afterOk = false;
    }

    Logf(
        "BUNNY,%llu,%u,%.6f,%.6f,%.6f,%u,0x%08X,0x%08X,%u,%.6f,%.6f,%.6f,"
        "%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,"
        "%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,"
        "%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%d,%d\r\n",
        static_cast<unsigned long long>(tUs),
        frame, fps, step, stepNc, gameMs,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(data)),
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(blendAssoc)),
        static_cast<unsigned>(animId),
        animCurrent, animSpeed, animStep,
        move0.x, move0.y, move0.z, turn0.x, turn0.y, turn0.z,
        move1.x, move1.y, move1.z, turn1.x, turn1.y, turn1.z,
        move1.x - move0.x, move1.y - move0.y, move1.z - move0.z,
        turn1.x - turn0.x, turn1.y - turn0.y, turn1.z - turn0.z,
        snapOk ? 1 : 0, afterOk ? 1 : 0
    );
}

DWORD WINAPI InitThread(LPVOID) {
    // Let the ASI loader finish and GTA complete its early DLL initialization.
    Sleep(1500);

    InitializeCriticalSection(&g_logCs);
    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_qpcStart);
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));

    char logPath[MAX_PATH]{};
    BuildLogPath(logPath, sizeof(logPath));
    g_log = CreateFileA(
        logPath,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (g_log == INVALID_HANDLE_VALUE) {
        return 0;
    }

    Logf("# GTAProbe v3\r\n");
    Logf("# Diagnostic only: does not unlock FPS, synthesize input, add bunny impulses, or change animations.\r\n");
    Logf("# EXE_BASE,0x%08X\r\n", static_cast<unsigned>(g_base));
    Logf("# LOG_PATH,%s\r\n", logPath);
    Logf("# Columns:\r\n");
    Logf("# type,qpc_us,frame,fps,timestep,timestep_nonclipped,game_ms,bmx_ptr,assoc_ptr,anim_id,anim_current,anim_speed,anim_step,"
         "move0_x,move0_y,move0_z,turn0_x,turn0_y,turn0_z,"
         "move1_x,move1_y,move1_z,turn1_x,turn1_y,turn1_z,"
         "dmove_x,dmove_y,dmove_z,dturn_x,dturn_y,dturn_z,snapshot_ok,after_ok\r\n");

    void* target = reinterpret_cast<void*>(AtVA(kLaunchBunnyHopVA));
    if (!IsReadableAddress(target)) {
        Logf("ERROR,target_not_readable,0x%08X\r\n", static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)));
        return 0;
    }

    unsigned char bytes[8]{};
    __try {
        std::memcpy(bytes, target, sizeof(bytes));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Logf("ERROR,could_not_read_target_bytes\r\n");
        return 0;
    }

    Logf("TARGET_BYTES,0x%08X,%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)),
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);

    if (bytes[0] == 0xE9 || bytes[0] == 0xE8) {
        Logf("ERROR,target_already_hooked_or_redirected,first_byte=%02X\r\n", bytes[0]);
        Logf("# Disable SelectiveFPS and any other mod that hooks 0x6C0390, then retry.\r\n");
        return 0;
    }

    const MH_STATUS initSt = MH_Initialize();
    if (initSt != MH_OK && initSt != MH_ERROR_ALREADY_INITIALIZED) {
        Logf("ERROR,MH_Initialize,%d\r\n", static_cast<int>(initSt));
        return 0;
    }

    const MH_STATUS createSt = MH_CreateHook(
        target,
        reinterpret_cast<LPVOID>(&HookLaunchBunnyHop),
        reinterpret_cast<LPVOID*>(&g_originalBunny)
    );
    if (createSt != MH_OK) {
        Logf("ERROR,MH_CreateHook,%d\r\n", static_cast<int>(createSt));
        return 0;
    }

    const MH_STATUS enableSt = MH_EnableHook(target);
    if (enableSt != MH_OK) {
        Logf("ERROR,MH_EnableHook,%d\r\n", static_cast<int>(enableSt));
        return 0;
    }

    Logf("HOOK_OK,LaunchBunnyHopCB,0x%08X\r\n", static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)));
    Logf("READY\r\n");
    return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE th = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (th) CloseHandle(th);
    }
    return TRUE;
}
