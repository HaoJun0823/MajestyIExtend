// dllmain.cpp : Majesty HD XML Text Wide Converter
//
// 问题根因：
//   游戏 XML 解析器 (sub_68DE30) 用 strlen 处理文本，不支持 UTF-16LE 文件。
//   XML 文本以 UTF-8 读入，经 sub_627A80 (narrow 字符串构造) 创建 narrow 对象 (+7 bit0=0)。
//   narrow 对象渲染走 sub_62AF20 逐字节路径，UTF-8 多字节字符被拆散 → 死循环。
//
// 修复方案：inline hook sub_627A80 入口
//   - 检测输入文本是否含 UTF-8 多字节字符 (byte >= 0x80)
//   - 纯 ASCII: 调用原始函数 (零影响)
//   - 含中文: 解码 UTF-8→UTF-16LE, 构造 wide 对象 (+7 bit0=1, malloc 2*len+4, 哨兵 0xFEFF)
//
// 字符串对象布局 (12 字节):
//   [0] void*  data     — 数据指针 (wide: malloc+2, 前2字节=0xFEFF)
//   [4] uint32 meta     — 低3字节=长度, bit24(0x1000000)=wide标志
//   [8] uint32 extra    — 长度
//
// sub_627A80 调用约定: __thiscall(ecx=this, [esp+4]=text), retn 4
// 原始前5字节: 53 8B 5C 24 08 (push ebx; mov ebx,[esp+4+arg_0])

#include "pch.h"
#include <psapi.h>
#include <string>

#pragma comment(lib, "psapi.lib")

// ===================== 地址常量 =====================
static constexpr uintptr_t ADDR_627A80 = 0x00627A80;

// ===================== 字符串对象 =====================
struct StrObj {
    void*    data;   // [0]
    uint32_t meta;   // [4] 低3字节=长度, bit24=wide
    uint32_t extra;  // [8]
};

// ===================== Trampoline =====================
// trampoline 布局: [原始5字节][E9 xx xx xx xx]  →  jmp 0x627A85
// 用 VirtualAlloc 分配在 2GB 范围内（以便 rel32 跳转可达）
static uint8_t*  g_trampoline = nullptr;       // VirtualAlloc'd
static uint8_t   g_savedBytes[5];             // 原始前5字节
static bool      g_hooked = false;

// 原始函数指针 (通过 trampoline 调用)
// sub_627A80 是 __thiscall(ecx, [esp+4]=text), retn 4
// MSVC x86 __fastcall: ecx=arg0, edx=arg1, [esp+4]=arg2, retn 4
// 所以用 __fastcall 声明可以正确匹配
typedef void (__fastcall *OrigFunc_t)(StrObj* /*this_*/, void* /*edx*/, const char* /*text*/);
static OrigFunc_t g_origFunc = nullptr;

// ===================== 工具函数 =====================

static bool HasUtf8Multibyte(const char* str) {
    if (!str) return false;
    for (const unsigned char* p = (const unsigned char*)str; *p; ++p) {
        if (*p >= 0x80) return true;
    }
    return false;
}

// 构造 wide 字符串对象 (等价 sub_6279E0 的逻辑)
static void ConstructWideStr(StrObj* obj, const char* utf8) {
    // 解码 UTF-8 → UTF-16LE
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (wlen <= 0) {
        obj->data = nullptr;
        obj->meta = 0;
        obj->extra = 0;
        return;
    }

    // 分配 wide 数据: malloc(2*wlen + 4) + 2, 前2字节放 0xFEFF 哨兵
    int allocSize = 2 * wlen + 4;
    char* raw = (char*)malloc(allocSize);
    if (!raw) {
        obj->data = nullptr;
        obj->meta = 0;
        obj->extra = 0;
        return;
    }

    *(uint16_t*)(raw) = 0xFEFF;  // 哨兵 (sub_627890 wide 分支会检查)
    wchar_t* wdata = (wchar_t*)(raw + 2);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wdata, wlen);

    // wlen 含 null terminator, 实际字符数 = wlen - 1
    int actualLen = wlen - 1;

    obj->data = wdata;
    obj->meta = actualLen | 0x1000000;  // 长度 + wide 标志
    obj->extra = actualLen;
}

// ===================== Hook 函数 =====================
// sub_627A80 调用约定: __thiscall(ecx=this, [esp+4]=text), retn 4
// 用 __fastcall 声明: ecx=this_, edx=unused, text=[esp+4]
// MSVC __fastcall 自动处理 retn 4 (因为有一个栈参数)
// 当游戏调用 0x627A80 时: jmp 到本函数, 栈帧完全兼容

extern "C" void __fastcall Hooked_627A80(StrObj* this_, void* /*edx*/, const char* text) {
    if (text && HasUtf8Multibyte(text)) {
        // 含 UTF-8 多字节 → 构造 wide 对象
        ConstructWideStr(this_, text);
        return;
    }
    // 纯 ASCII 或 null → 调用原始函数 (通过 trampoline)
    g_origFunc(this_, nullptr, text);
}

// ===================== Hook 安装 =====================
static bool InstallHook() {
    uint8_t* target = (uint8_t*)ADDR_627A80;

    // 验证原始字节
    const uint8_t expected[5] = { 0x53, 0x8B, 0x5C, 0x24, 0x08 };
    for (int i = 0; i < 5; i++) {
        if (target[i] != expected[i]) {
            return false;  // 已被修改或版本不匹配
        }
    }

    // 保存原始字节
    memcpy(g_savedBytes, target, 5);

    // 分配 trampoline (尽量靠近 target 以保证 rel32 可达)
    SIZE_T trampSize = 16;
    g_trampoline = (uint8_t*)VirtualAlloc(
        (void*)(ADDR_627A80 - 0x20000000),  // 偏好地址在 target 以下 512MB
        trampSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE
    );
    // 如果偏好地址失败，让系统分配
    if (!g_trampoline) {
        g_trampoline = (uint8_t*)VirtualAlloc(
            nullptr, trampSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE
        );
    }
    if (!g_trampoline) return false;

    // 构建 trampoline: 原始5字节 + jmp 0x627A85
    memcpy(g_trampoline, g_savedBytes, 5);
    g_trampoline[5] = 0xE9;  // jmp rel32
    // 相对跳转: target = g_trampoline + 5 + 5 + rel = ADDR_627A80 + 5
    // rel = (ADDR_627A80 + 5) - (g_trampoline + 10)
    intptr_t jmpBack = (intptr_t)(ADDR_627A80 + 5) - (intptr_t)(g_trampoline + 10);
    memcpy(&g_trampoline[9], &jmpBack, 4);

    // 设置 g_origFunc 指向 trampoline
    g_origFunc = (OrigFunc_t)g_trampoline;

    // 写入 hook: 0x627A80 → jmp Hooked_627A80
    DWORD oldProtect;
    if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    target[0] = 0xE9;  // jmp rel32
    intptr_t rel = (intptr_t)&Hooked_627A80 - (intptr_t)(target + 5);
    memcpy(&target[1], &rel, 4);

    VirtualProtect(target, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), (void*)target, 5);

    g_hooked = true;
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
            return TRUE;  // 不是目标进程
        }

        // 安装 hook
        if (InstallHook()) {
            // 日志
            char logPath[MAX_PATH];
            GetModuleFileNameA(hModule, logPath, MAX_PATH);
            char* p = strrchr(logPath, '\\');
            if (p) {
                strcpy(p + 1, "MajestyI_TextFix.log");
            } else {
                strcpy(logPath, "MajestyI_TextFix.log");
            }

            FILE* f = fopen(logPath, "w");
            if (f) {
                fprintf(f, "[MajestyHD XML Wide Converter] Hook installed at 0x%08X\n", (unsigned)ADDR_627A80);
                fprintf(f, "  Trampoline at %p\n", g_trampoline);
                fprintf(f, "  Hook function at %p\n", &Hooked_627A80);
                fprintf(f, "  OrigFunc (trampoline) at %p\n", g_origFunc);
                fclose(f);
            }
        } else {
            // Hook 安装失败
            char logPath[MAX_PATH];
            GetModuleFileNameA(hModule, logPath, MAX_PATH);
            char* p = strrchr(logPath, '\\');
            if (p) {
                strcpy(p + 1, "MajestyI_TextFix.log");
            } else {
                strcpy(logPath, "MajestyI_TextFix.log");
            }

            uint8_t* tgt = (uint8_t*)ADDR_627A80;
            FILE* g = fopen(logPath, "w");
            if (g) {
                fprintf(g, "[MajestyHD XML Wide Converter] Hook FAILED\n");
                fprintf(g, "  Target address: 0x%08X\n", (unsigned)ADDR_627A80);
                fprintf(g, "  Expected bytes: 53 8B 5C 24 08\n");
                fprintf(g, "  Actual bytes: %02X %02X %02X %02X %02X\n",
                    tgt[0], tgt[1], tgt[2], tgt[3], tgt[4]);
                fclose(g);
            }
        }
    }
    return TRUE;
}
