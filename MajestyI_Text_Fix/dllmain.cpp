// dllmain.cpp : Majesty HD XML Text Wide Converter
//
// 问题根因：
//   游戏 XML 解析器 (sub_68DE30) 用 strlen 处理文本，不支持 UTF-16LE 文件。
//   XML 文本以 UTF-8 读入，经 sub_627A80 (narrow 字符串构造) 创建 narrow 对象 (+7 bit0=0)。
//   narrow 对象渲染走 sub_62AF20 逐字节路径，UTF-8 多字节字符被拆散 → 死循环。
//
// 修复方案：用 MinHook inline hook sub_627A80 入口
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
#include "MinHook.h"

#pragma comment(lib, "psapi.lib")

// ===================== 地址常量 =====================
static constexpr uintptr_t ADDR_627A80 = 0x00627A80;

// ===================== 字符串对象 =====================
struct StrObj {
    void*    data;   // [0]
    uint32_t meta;   // [4] 低3字节=长度, bit24=wide
    uint32_t extra;  // [8]
};

// ===================== 原始函数指针 =====================
// sub_627A80 是 __thiscall(ecx, [esp+4]=text), retn 4
// MSVC x86 __fastcall: ecx=arg0, edx=arg1, [esp+4]=arg2, retn 4
// 用 __fastcall 声明可以正确匹配 thiscall 调用约定
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

void __fastcall Hooked_627A80(StrObj* this_, void* /*edx*/, const char* text) {
    if (text && HasUtf8Multibyte(text)) {
        // 含 UTF-8 多字节 → 构造 wide 对象
        ConstructWideStr(this_, text);
        return;
    }
    // 纯 ASCII 或 null → 调用原始函数 (MinHook trampoline)
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

    // 初始化 MinHook
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        return false;
    }

    // 创建 hook
    status = MH_CreateHook(
        (LPVOID)ADDR_627A80,
        (LPVOID)&Hooked_627A80,
        (LPVOID*)&g_origFunc
    );
    if (status != MH_OK) {
        return false;
    }

    // 启用 hook
    status = MH_EnableHook((LPVOID)ADDR_627A80);
    if (status != MH_OK) {
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
            return TRUE;  // 不是目标进程
        }

        // 日志路径
        char logPath[MAX_PATH];
        GetModuleFileNameA(hModule, logPath, MAX_PATH);
        char* p = strrchr(logPath, '\\');
        if (p) {
            strcpy(p + 1, "MajestyI_TextFix.log");
        } else {
            strcpy(logPath, "MajestyI_TextFix.log");
        }

        // 安装 hook
        if (InstallHook()) {
            FILE* f = fopen(logPath, "w");
            if (f) {
                fprintf(f, "[MajestyHD XML Wide Converter] Hook installed at 0x%08X\n", (unsigned)ADDR_627A80);
                fprintf(f, "  Hook function at %p\n", &Hooked_627A80);
                fprintf(f, "  OrigFunc (trampoline) at %p\n", g_origFunc);
                fprintf(f, "  MinHook version: 1.3.3\n");
                fclose(f);
            }
        } else {
            uint8_t* tgt = (uint8_t*)ADDR_627A80;
            FILE* f = fopen(logPath, "w");
            if (f) {
                fprintf(f, "[MajestyHD XML Wide Converter] Hook FAILED\n");
                fprintf(f, "  Target address: 0x%08X\n", (unsigned)ADDR_627A80);
                fprintf(f, "  Expected bytes: 53 8B 5C 24 08\n");
                fprintf(f, "  Actual bytes: %02X %02X %02X %02X %02X\n",
                    tgt[0], tgt[1], tgt[2], tgt[3], tgt[4]);
                fclose(f);
            }
        }
    }
    return TRUE;
}
