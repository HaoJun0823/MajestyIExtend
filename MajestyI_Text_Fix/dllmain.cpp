// dllmain.cpp : Majesty HD XML Text Wide Converter v2
//
// === 问题根因 ===
// 游戏 XML 解析器 (sub_68DE30) 用 strlen 处理文本 buffer —— 对 UTF-8 没问题(无 null 字节)。
// 但 XML 文本以 UTF-8 读入后，经 sub_627A80 (narrow 字符串构造) 创建 narrow 对象 (+7 bit0=0)。
// sub_628350 (字符串赋值函数) 按 narrow 逐字节拷贝，不转 wide。
// 渲染器处理 narrow 字符串时按单字节处理，遇到 UTF-8 多字节字符 → 崩溃/死循环。
//
// === 上一版验证失败 ===
// hook sub_627A80: debug 日志显示 wide=0, narrow=16400+ —— 中文文本根本不走这个函数。
// sub_627A80 只处理纯 ASCII 属性名，中文文本走 sub_628350 路径。
//
// === 新方案 ===
// hook sub_628350 (字符串赋值函数)，100+ 调用者，所有字符串赋值都经过它。
// 调用约定: __thiscall(ecx=this/dest, [esp+4]=src), retn 4
//
// 字符串对象布局 (12 字节):
//   [0] void*  data     — 数据指针 (wide: malloc+2, 前2字节=0xFEFF)
//   [4] uint32 meta     — 低3字节=长度, bit24(0x1000000)=wide标志, +7 bit0 = wide flag
//   [8] uint32 extra   — 长度/hash
//
// 检查源对象 (+7 bit0):
//   - wide (bit0=1): 直接调原始函数 (wide→wide 拷贝)
//   - narrow (bit0=0): 检查是否含 UTF-8 多字节 (byte >= 0x80)
//     - 含中文: 将 UTF-8 解码为 UTF-16LE, 构造临时 wide 对象, 调原始函数做 wide→wide 拷贝
//     - 纯 ASCII: 直接调原始函数 (narrow→narrow, 零影响)

#include "pch.h"
#include <psapi.h>
#include <string>
#include <atomic>
#include "MinHook.h"

#pragma comment(lib, "psapi.lib")

// ===================== 地址常量 =====================
static constexpr uintptr_t ADDR_628350 = 0x00628350;

// ===================== 字符串对象 =====================
struct StrObj {
    void*    data;   // [0]
    uint32_t meta;   // [4] 低3字节=长度, bit24=wide
    uint32_t extra;  // [8]
};

// ===================== 原始函数指针 =====================
// sub_628350: __thiscall(ecx=this/dest, [esp+4]=src), retn 4
// 用 __fastcall 模拟 __thiscall: ecx=this, edx=unused, [esp+4]=arg
typedef void (__fastcall *OrigAssign_t)(StrObj* /*this_*/, void* /*edx*/, StrObj* /*src*/);
static OrigAssign_t g_origAssign = nullptr;

// ===================== Debug 日志 =====================
static FILE* g_logFile = nullptr;
static std::atomic<int> g_callCount{0};
static std::atomic<int> g_wideCount{0};
static std::atomic<int> g_narrowCount{0};
static std::atomic<int> g_convertedCount{0};

static void LogWrite(const char* fmt, ...) {
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fflush(g_logFile);
}

// ===================== 工具函数 =====================

// 检查 narrow 字符串是否含 UTF-8 多字节字符
static bool HasUtf8Multibyte(const char* str, int len) {
    if (!str || len <= 0) return false;
    for (int i = 0; i < len; i++) {
        if ((unsigned char)str[i] >= 0x80) return true;
    }
    return false;
}

// 从 StrObj 获取 narrow 数据指针和长度
static const char* GetNarrowData(StrObj* obj, int* outLen) {
    // 长度在 meta 低 3 字节
    int len = obj->meta & 0xFFFFFF;
    *outLen = len;
    return (const char*)obj->data;
}

// 构造临时 wide StrObj (等价 sub_6279E0 + sub_627890 wide 路径)
// 返回临时对象和 malloc 原始指针(调用者负责释放)
static void ConstructTempWide(StrObj* tempObj, const char* utf8, int utf8Len) {
    memset(tempObj, 0, sizeof(StrObj));

    // 解码 UTF-8 → UTF-16LE (不含 null terminator, 用精确长度)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8Len, nullptr, 0);
    if (wlen <= 0) {
        LogWrite("  [CONVERT] MultiByteToWideChar FAILED wlen=%d utf8Len=%d\n", wlen, utf8Len);
        return;
    }

    // 分配 wide 数据: malloc(2*wlen + 4) + 2, 前2字节放 0xFEFF 哨兵
    // 需要额外空间放 null terminator (2 bytes)
    int allocSize = 2 * wlen + 4 + 2;
    char* raw = (char*)malloc(allocSize);
    if (!raw) {
        LogWrite("  [CONVERT] malloc FAILED size=%d\n", allocSize);
        return;
    }

    // 哨兵
    *(uint16_t*)(raw) = 0xFEFF;
    wchar_t* wdata = (wchar_t*)(raw + 2);

    // 转换 (不含 null terminator)
    MultiByteToWideChar(CP_UTF8, 0, utf8, utf8Len, wdata, wlen);
    // 手动加 null terminator
    wdata[wlen] = 0;

    // 设置 StrObj
    tempObj->data = wdata;                    // 指向哨兵之后的数据
    tempObj->meta = wlen | 0x1000000;         // 长度 + wide 标志 (bit24)
    tempObj->extra = wlen;                    // 长度

    LogWrite("  [CONVERT] utf8Len=%d wlen=%d allocSize=%d tempObj={data=%p meta=0x%08X extra=%u}\n",
        utf8Len, wlen, allocSize, tempObj->data, tempObj->meta, tempObj->extra);
}

// ===================== Hook 函数 =====================

void __fastcall Hooked_628350(StrObj* this_, void* /*edx*/, StrObj* src) {
    int callNum = ++g_callCount;

    // 跳过 self-assignment (源==目标)
    if (src == this_) {
        // 直接调原始函数 (它会立即返回)
        g_origAssign(this_, nullptr, src);
        return;
    }

    // 获取源对象信息
    bool srcIsWide = (*(uint8_t*)((char*)src + 7) & 1) != 0;
    int srcLen = src->meta & 0xFFFFFF;
    const char* srcData = (const char*)src->data;

    // 详细日志 (前100次)
    bool detailedLog = (callNum <= 100);

    // 递归防护
    static thread_local int recursion = 0;
    if (recursion > 0) {
        // 递归调用，直接走原始函数
        recursion++;
        g_origAssign(this_, nullptr, src);
        recursion--;
        return;
    }

    recursion++;

    if (srcIsWide) {
        // 源已经是 wide —— 直接调原始函数
        g_wideCount++;
        if (detailedLog) {
            LogWrite("[%d] WIDE src len=%d data=%p\n", callNum, srcLen, srcData);
        }
        g_origAssign(this_, nullptr, src);
    } else {
        // 源是 narrow —— 检查是否含 UTF-8 多字节
        if (srcData && HasUtf8Multibyte(srcData, srcLen)) {
            // narrow + UTF-8 多字节 → 转换为临时 wide 对象
            g_convertedCount++;

            if (detailedLog) {
                // 记录前 60 字节的 hex
                LogWrite("[%d] CONVERT narrow+utf8 len=%d hex=", callNum, srcLen);
                int logLen = srcLen < 60 ? srcLen : 60;
                for (int i = 0; i < logLen; i++) {
                    LogWrite("%02X ", (unsigned char)srcData[i]);
                }
                LogWrite("\n");
            }

            // 构造临时 wide 对象
            StrObj tempWide;
            ConstructTempWide(&tempWide, srcData, srcLen);

            if (tempWide.data) {
                // 用临时 wide 对象作为源调用原始函数
                // 原始函数会: 检测 src is wide → 分配 wide 内存 → wide→wide 拷贝
                g_origAssign(this_, nullptr, &tempWide);

                // 释放临时 wide 数据
                // tempWide.data 指向 malloc+2, 需要 free 原始指针
                char* raw = (char*)tempWide.data - 2;
                free(raw);

                if (detailedLog) {
                    LogWrite("  [CONVERT] done dest={data=%p meta=0x%08X extra=%u wide=%s}\n",
                        this_->data, this_->meta, this_->extra,
                        (this_->meta & 0x1000000) ? "YES" : "NO");
                }
            } else {
                // 转换失败，降级为原始 narrow 调用
                LogWrite("  [CONVERT] FAILED, fallback to narrow\n");
                g_narrowCount++;
                g_origAssign(this_, nullptr, src);
            }
        } else {
            // 纯 ASCII narrow —— 直接调原始函数 (零影响)
            g_narrowCount++;
            if (detailedLog && callNum <= 20) {
                LogWrite("[%d] NARROW ascii len=%d\n", callNum, srcLen);
            }
            g_origAssign(this_, nullptr, src);
        }
    }

    // 定期摘要
    if (callNum % 500 == 0) {
        LogWrite("[%d] summary: wide=%d narrow=%d converted=%d\n", callNum,
            g_wideCount.load(), g_narrowCount.load(), g_convertedCount.load());
    }

    recursion--;
}

// ===================== Hook 安装 =====================
static bool InstallHook() {
    uint8_t* target = (uint8_t*)ADDR_628350;

    // 验证原始字节: sub_628350 开头应为 push esi; push edi
    // 56 57 (push esi; push edi)
    const uint8_t expected[2] = { 0x56, 0x57 };
    for (int i = 0; i < 2; i++) {
        if (target[i] != expected[i]) {
            LogWrite("[Hook] byte mismatch at offset %d: expected %02X got %02X\n",
                i, expected[i], target[i]);
            return false;
        }
    }

    // 初始化 MinHook
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        LogWrite("[Hook] MH_Initialize failed: %s\n", MH_StatusToString(status));
        return false;
    }

    // 创建 hook
    status = MH_CreateHook(
        (LPVOID)ADDR_628350,
        (LPVOID)&Hooked_628350,
        (LPVOID*)&g_origAssign
    );
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook failed: %s\n", MH_StatusToString(status));
        return false;
    }

    // 启用 hook
    status = MH_EnableHook((LPVOID)ADDR_628350);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook failed: %s\n", MH_StatusToString(status));
        return false;
    }

    return true;
}

// ===================== 主入口 =====================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        HMODULE hExe = GetModuleHandleA(nullptr);
        if (!hExe) return TRUE;

        // 验证主模块
        char path[MAX_PATH];
        GetModuleFileNameA(hExe, path, MAX_PATH);
        if (!strstr(path, "MajestyHD.exe") && !strstr(path, "majestyhd.exe")) {
            return TRUE;
        }

        // 日志路径 —— 放在 DLL 同目录
        char logPath[MAX_PATH];
        GetModuleFileNameA(hModule, logPath, MAX_PATH);
        char* p = strrchr(logPath, '\\');
        if (p) {
            strcpy(p + 1, "MajestyI_TextFix.log");
        } else {
            strcpy(logPath, "MajestyI_TextFix.log");
        }

        // 打开日志文件
        g_logFile = fopen(logPath, "w");
        if (g_logFile) {
            fprintf(g_logFile, "[MajestyHD Text Wide Converter v2] DllMain ATTACH\n");
            fprintf(g_logFile, "  Hook target: sub_628350 @ 0x%08X (string assign)\n", (unsigned)ADDR_628350);
            fprintf(g_logFile, "  Exe path: %s\n", path);
            fprintf(g_logFile, "  Log path: %s\n", logPath);
            fprintf(g_logFile, "  Strategy: hook assign function, convert narrow+UTF8 to wide\n");
            fprintf(g_logFile, "  Detailed log: first 100 calls, summary every 500\n\n");
            fflush(g_logFile);
        }

        // 安装 hook
        if (InstallHook()) {
            LogWrite("[Hook] installed at 0x%08X\n", (unsigned)ADDR_628350);
            LogWrite("  Hook function at %p\n", &Hooked_628350);
            LogWrite("  OrigFunc (trampoline) at %p\n", g_origAssign);
            LogWrite("  MinHook version: 1.3.3\n\n");
            fflush(g_logFile);
        } else {
            uint8_t* tgt = (uint8_t*)ADDR_628350;
            LogWrite("[Hook] FAILED\n");
            LogWrite("  Target address: 0x%08X\n", (unsigned)ADDR_628350);
            LogWrite("  Expected bytes: 56 57\n");
            LogWrite("  Actual bytes: %02X %02X %02X %02X %02X\n",
                tgt[0], tgt[1], tgt[2], tgt[3], tgt[4]);
            fflush(g_logFile);
        }
    } else if (dwReason == DLL_PROCESS_DETACH) {
        if (g_logFile) {
            LogWrite("\n[DllMain] DETACH\n");
            LogWrite("  Total calls: %d (wide=%d narrow=%d converted=%d)\n",
                g_callCount.load(), g_wideCount.load(),
                g_narrowCount.load(), g_convertedCount.load());
            fclose(g_logFile);
            g_logFile = nullptr;
        }
    }
    return TRUE;
}
