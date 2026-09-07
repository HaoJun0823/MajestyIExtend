// dllmain.cpp : Majesty HD XML Text Wide Converter v4
//
// === 问题根因 ===
// 游戏 XML 解析器用 strlen 处理 UTF-8 buffer (无 null 字节, 没问题)。
// 但 XML 文本经 sub_627A80 (narrow 构造) 创建 narrow 对象 (+7 bit0=0)。
// sub_628350 (字符串赋值) 按 narrow 逐字节拷贝, 不转 wide。
// 渲染器处理 narrow 字符串遇 UTF-8 多字节 (byte >= 0x80) → 崩溃/死循环。
//
// === 方案 (v4) ===
// hook sub_628350 (字符串赋值函数), 100+ 调用者。
// 调用约定: __thiscall(ecx=this/dest, [esp+4]=src), retn 4
//
// 字符串对象布局 (12 字节):
//   [0] void*  data     — 数据指针 (wide: malloc+2, 前2字节=0xFEFF)
//   [4] uint32 meta    — 低3字节=长度, bit24(0x1000000)=wide标志
//   [8] uint32 extra   — 长度/hash
//
// 检查源对象 byte7 (meta 的 MSB) bit0:
//   - wide (bit0=1): 直接调原始函数
//   - narrow (bit0=0): 检查是否含 UTF-8 多字节 (byte >= 0x80)
//     - 含中文: 栈上构造临时 wide 对象 → 调原始函数做 wide→wide 拷贝
//     - 纯 ASCII: 直接调原始函数 (零影响)
//
// === v4 稳定性改进 ===
// 1. 完全不用 malloc/free — 栈缓冲区, 消除跨 CRT 堆损坏
// 2. 无原子计数器 — 减少开销
// 3. 仅启动/关闭/错误日志
// 4. 最大转换长度 4096 wide chars (8KB 栈), 超长回退 narrow

#include "pch.h"
#include <psapi.h>
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
typedef void (__fastcall *OrigAssign_t)(StrObj* /*this_*/, void* /*edx*/, StrObj* /*src*/);
static OrigAssign_t g_origAssign = nullptr;

// ===================== Debug 日志 (仅启动/关闭/错误) =====================
static FILE* g_logFile = nullptr;

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
static inline bool HasUtf8Multibyte(const char* str, int len) {
    if (!str || len <= 0) return false;
    const unsigned char* p = (const unsigned char*)str;
    for (int i = 0; i < len; i++) {
        if (p[i] >= 0x80) return true;
    }
    return false;
}

// ===================== Hook 函数 =====================
// 栈缓冲区: 4096 wide chars = 8KB, 足够任何游戏文本
static constexpr int MAX_WIDE_CHARS = 4096;

void __fastcall Hooked_628350(StrObj* this_, void* /*edx*/, StrObj* src) {
    // 跳过 self-assignment
    if (src == this_) {
        g_origAssign(this_, nullptr, src);
        return;
    }

    // 递归防护 (thread_local)
    static thread_local int recursion = 0;
    if (recursion > 0) {
        g_origAssign(this_, nullptr, src);
        return;
    }
    recursion++;

    // 获取源对象信息
    bool srcIsWide = (*(uint8_t*)((char*)src + 7) & 1) != 0;

    if (srcIsWide) {
        // 源已经是 wide —— 直接调原始函数
        g_origAssign(this_, nullptr, src);
    } else {
        // 源是 narrow —— 检查是否含 UTF-8 多字节
        int srcLen = src->meta & 0xFFFFFF;
        const char* srcData = (const char*)src->data;

        if (srcData && srcLen > 0 && HasUtf8Multibyte(srcData, srcLen)) {
            // narrow + UTF-8 多字节 → 在栈上转换为 wide
            // 使用栈缓冲区, 不调用 malloc/free
            wchar_t stackBuf[MAX_WIDE_CHARS];
            
            // 限制转换长度, 防止栈溢出
            int convertLen = (srcLen > MAX_WIDE_CHARS - 1) ? (MAX_WIDE_CHARS - 1) : srcLen;
            
            int wlen = MultiByteToWideChar(CP_UTF8, 0, srcData, convertLen, stackBuf, MAX_WIDE_CHARS);
            
            if (wlen > 0) {
                // 添加 null terminator
                stackBuf[wlen] = 0;
                
                // 在栈上构造临时 wide StrObj
                // 原始函数会: free(this->data) → 用游戏自己的 malloc 分配新 wide 缓冲区 → 从我们的栈缓冲区 memcpy
                // 函数返回后 this->data 指向游戏分配的内存, 我们的栈缓冲区不再被引用
                StrObj tempWide;
                tempWide.data = stackBuf;
                tempWide.meta = (uint32_t)wlen | 0x1000000;  // 长度 + wide 标志 (bit24)
                tempWide.extra = (uint32_t)wlen;
                
                // 调用原始函数: 它会分配游戏自己的 wide 缓冲区并拷贝数据
                g_origAssign(this_, nullptr, &tempWide);
            } else {
                // 转换失败, 降级为原始 narrow 调用
                LogWrite("[ERROR] MultiByteToWideChar failed, srcLen=%d\n", srcLen);
                g_origAssign(this_, nullptr, src);
            }
        } else {
            // 纯 ASCII narrow —— 直接调原始函数 (零影响)
            g_origAssign(this_, nullptr, src);
        }
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
        fprintf(g_logFile, "[MajestyHD Text Wide Converter v4] DllMain ATTACH\n");
        fprintf(g_logFile, "  Hook target: sub_628350 @ 0x%08X (string assign)\n", (unsigned)ADDR_628350);
        fprintf(g_logFile, "  Exe path: %s\n", path);
        fprintf(g_logFile, "  Log path: %s\n", logPath);
        fprintf(g_logFile, "  Strategy: hook assign, stack buffer (no malloc/free)\n");
        fprintf(g_logFile, "  v4: stack buffer, no atomics, no CRT cross-heap\n\n");
            fflush(g_logFile);
        }

        // 安装 hook
        if (InstallHook()) {
            LogWrite("[Hook] installed at 0x%08X\n", (unsigned)ADDR_628350);
            LogWrite("  Hook function at %p\n", &Hooked_628350);
            LogWrite("  OrigFunc (trampoline) at %p\n", g_origAssign);
            LogWrite("  MinHook version: 1.3.3\n\n");
        } else {
            uint8_t* tgt = (uint8_t*)ADDR_628350;
            LogWrite("[Hook] FAILED\n");
            LogWrite("  Target address: 0x%08X\n", (unsigned)ADDR_628350);
            LogWrite("  Expected bytes: 56 57\n");
            LogWrite("  Actual bytes: %02X %02X %02X %02X %02X\n",
                tgt[0], tgt[1], tgt[2], tgt[3], tgt[4]);
        }
    } else if (dwReason == DLL_PROCESS_DETACH) {
        if (g_logFile) {
            fprintf(g_logFile, "\n[DllMain] DETACH\n");
            fclose(g_logFile);
            g_logFile = nullptr;
        }
    }
    return TRUE;
}
