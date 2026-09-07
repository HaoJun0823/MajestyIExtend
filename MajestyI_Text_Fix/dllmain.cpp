// dllmain.cpp : Majesty HD Runtime Localization v6.5
//
// === 方案 (v6.5) ===
// 外挂汉化: 不修改任何原文件, 运行时 hook 字符串赋值函数 sub_628350
//
// v6.5 核心改变: 在 v6.4 基础上加返回地址过滤, 只翻译 XML 文本加载
//
// v6.4 基础: hook sub_628350, 用栈上临时 wide StrObj 让游戏自己 malloc/free
// v6.5 过滤: 检查 [esp] 返回地址, 只在来自 sub_659820 (XML 文本提取)
//           的调用时翻译, 跳过 GPL 脚本字符串赋值
//
// sub_659820 内部有两处 call sub_628350:
//   0x659896 — XML 节点有文本时
//   0x6598C4 — XML 节点为空时
// 只有返回地址在这两个位置附近的才翻译
//
// 字典文件: scripts/dict.txt (UTF-8, 格式: 英文\t中文\n)

#include "pch.h"
#include <psapi.h>
#include <unordered_map>
#include <string>
#include <vector>
#include "MinHook.h"

#pragma comment(lib, "psapi.lib")

// ===================== 地址常量 =====================
static constexpr uintptr_t ADDR_628350 = 0x00628350; // StrObj assign (thiscall)

// ===================== 字符串对象 =====================
struct StrObj {
    void*    data;   // [0]
    uint32_t meta;   // [4] 低3字节=长度, bit24=wide标志
    uint32_t extra;  // [8]
};

// ===================== 字典 =====================
static std::unordered_map<std::string, std::wstring> g_dict;
static int g_dictCount = 0;

// ===================== 原始函数指针 =====================
// sub_628350: __thiscall(this, src) -> this
// 用 __fastcall 模拟 __thiscall (ecx=this, edx=unused, first stack arg=src)
typedef int (__fastcall *OrigStrAssign_t)(int ecx_this, int edx_unused, int src);
static OrigStrAssign_t g_orig628350 = nullptr;

// ===================== Debug 日志 =====================
static FILE* g_logFile = nullptr;
static int g_callCount = 0;
static int g_lookupCount = 0;
static int g_hitCount = 0;
static int g_missCount = 0;

static void LogWrite(const char* fmt, ...) {
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fflush(g_logFile);
}

// ===================== 字典加载 =====================

// 从整个文件数据中移除所有零宽字符序列 (UTF-8 编码)
static int RemoveZeroWidthChars(char* data, int len) {
    int write = 0;
    int read = 0;
    while (read < len) {
        unsigned char c = (unsigned char)data[read];
        if (read + 2 < len && c == 0xEF &&
            (unsigned char)data[read+1] == 0xBB && (unsigned char)data[read+2] == 0xBF) {
            read += 3;
            continue;
        }
        if (read + 2 < len && c == 0xE2 &&
            (unsigned char)data[read+1] == 0x80) {
            unsigned char c2 = (unsigned char)data[read+2];
            if (c2 >= 0x8B && c2 <= 0x8F) {
                read += 3;
                continue;
            }
        }
        if (read + 2 < len && c == 0xE2 &&
            (unsigned char)data[read+1] == 0x81) {
            unsigned char c2 = (unsigned char)data[read+2];
            if (c2 >= 0xA0 && c2 <= 0xA4) {
                read += 3;
                continue;
            }
        }
        data[write++] = data[read++];
    }
    return write;
}

static bool LoadDict(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0) { fclose(f); return false; }
    if (fileSize > 4 * 1024 * 1024) {
        LogWrite("[Dict] File too large: %ld bytes, truncating to 4MB\n", fileSize);
        fileSize = 4 * 1024 * 1024;
    }

    std::vector<char> fileData(fileSize + 1, 0);
    fread(fileData.data(), 1, fileSize, f);
    fclose(f);

    int cleanLen = RemoveZeroWidthChars(fileData.data(), (int)fileSize);
    fileData[cleanLen] = '\0';
    LogWrite("[Dict] File size: %ld -> %d (after removing zero-width chars)\n", fileSize, cleanLen);

    int pos = 0;
    int lineNum = 0;
    while (pos < cleanLen) {
        lineNum++;
        int lineStart = pos;
        while (pos < cleanLen && fileData[pos] != '\n') pos++;
        int lineLen = pos - lineStart;
        if (pos < cleanLen) pos++;
        if (lineLen == 0) continue;
        if (lineLen == 1 && fileData[lineStart] == '\r') continue;
        if (lineLen >= 8192) continue;

        unsigned char lineBuf[8192];
        memcpy(lineBuf, fileData.data() + lineStart, lineLen);
        lineBuf[lineLen] = '\0';
        int actualLen = lineLen;
        while (actualLen > 0 && (lineBuf[actualLen-1] == '\r' || lineBuf[actualLen-1] == '\n')) {
            lineBuf[--actualLen] = '\0';
        }
        if (actualLen == 0) continue;

        char* tab = (char*)memchr(lineBuf, '\t', actualLen);
        if (!tab) continue;

        int enLen = (int)(tab - (char*)lineBuf);
        if (enLen <= 0 || enLen >= 4096) continue;

        char enKey[4096];
        memcpy(enKey, lineBuf, enLen);
        enKey[enLen] = '\0';
        while (enLen > 0 && (enKey[enLen-1] == '\r' || enKey[enLen-1] == '\n')) {
            enKey[--enLen] = '\0';
        }
        if (enLen == 0) continue;

        char* cnStart = tab + 1;
        int cnLen = actualLen - enLen - 1;
        if (cnLen <= 0 || cnLen >= 4096) continue;
        while (cnLen > 0 && (cnStart[cnLen-1] == '\r' || cnStart[cnLen-1] == '\n')) {
            cnStart[--cnLen] = '\0';
        }
        if (cnLen == 0) continue;

        std::string enStr(enKey, enLen);
        std::string cnStr(cnStart, cnLen);

        for (size_t i = 0; i + 1 < cnStr.size(); i++) {
            if (cnStr[i] == '\\' && cnStr[i+1] == 'n') {
                cnStr[i] = '\n';
                cnStr.erase(i+1, 1);
            }
        }

        int wlen = MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(), nullptr, 0);
        if (wlen <= 0) continue;

        std::wstring wcn(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(), &wcn[0], wlen);

        g_dict[enStr] = wcn;
        g_dictCount++;
    }

    return true;
}

// ===================== 从 StrObj 提取英文文本 =====================
static bool ExtractTextFromStrObj(StrObj* obj, std::string& out) {
    if (!obj) return false;

    bool isWide = (*(uint8_t*)((char*)obj + 7) & 1) != 0;
    int len = obj->meta & 0xFFFFFF;

    if (len <= 0 || !obj->data) return false;

    if (isWide) {
        const wchar_t* wstr = (const wchar_t*)obj->data;
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wstr, len, nullptr, 0, nullptr, nullptr);
        if (utf8Len <= 0) return false;
        out.resize(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, wstr, len, &out[0], utf8Len, nullptr, nullptr);
        return true;
    } else {
        const char* str = (const char*)obj->data;
        int actualLen = strnlen(str, len);
        if (actualLen <= 0) {
            actualLen = obj->extra & 0xFFFFFF;
            if (actualLen <= 0 || actualLen > len) actualLen = len;
        }
        out.assign(str, actualLen);
        return true;
    }
}

// ===================== 返回地址过滤 =====================
// sub_659820 内部调用 sub_628350 的两个 call site:
//   0x659896 — call sub_628350 (XML 节点有文本)
//   0x6598C4 — call sub_628350 (XML 节点为空, 赋空 StrObj)
// 返回地址 = call 指令的下一条指令地址
//   从 0x659896 调用: 返回地址 = 0x65989B
//   从 0x6598C4 调用: 返回地址 = 0x6598C9
static constexpr uintptr_t XML_CALLSITE_1_RET = 0x65989B;
static constexpr uintptr_t XML_CALLSITE_2_RET = 0x6598C9;

// ===================== Hook sub_628350 =====================
// __thiscall(this, src) — 游戏字符串赋值
// ecx = this (目标 StrObj), [esp+4] = src (源 StrObj*)
// 用 __fastcall 模拟: ecx=this, edx=unused, stack arg=src
//
// v6.5: 只在来自 sub_659820 的调用时翻译, 跳过 GPL 脚本字符串
int __fastcall Hooked_628350(int ecx_this, int edx_unused, int src) {
    g_callCount++;

    // v6.5: 检查返回地址, 只在来自 sub_659820 的调用时翻译
    // __fastcall: ecx=arg0, edx=unused, stack arg=src
    // 栈布局: [esp] = return addr, [esp+4] = src
    uintptr_t retAddr;
    __asm {
        mov eax, [esp]
        mov retAddr, eax
    }

    bool fromXml = (retAddr == XML_CALLSITE_1_RET || retAddr == XML_CALLSITE_2_RET);

    if (!fromXml) {
        // 不是来自 XML 文本提取, 直接调原函数
        return g_orig628350(ecx_this, edx_unused, src);
    }

    StrObj* srcObj = (StrObj*)src;

    // 如果 src 是 wide (已经是中文), pass-through
    if (srcObj && (*(uint8_t*)((char*)srcObj + 7) & 1) != 0) {
        return g_orig628350(ecx_this, edx_unused, src);
    }

    // 提取英文文本
    std::string enText;
    if (!srcObj || !ExtractTextFromStrObj(srcObj, enText)) {
        return g_orig628350(ecx_this, edx_unused, src);
    }

    g_lookupCount++;

    // 查字典
    auto it = g_dict.find(enText);
    if (it == g_dict.end()) {
        // MISS: 直接调原函数
        if (g_missCount < 50) {
            LogWrite("[MISS] \"%s\"\n", enText.substr(0, 80).c_str());
            g_missCount++;
        }
        return g_orig628350(ecx_this, edx_unused, src);
    }

    // HIT: 构造栈上临时 wide StrObj
    g_hitCount++;
    const std::wstring& wcn = it->second;
    int wlen = (int)wcn.size();

    // 栈上分配: BOM(2) + wide data + null term(2)
    int wideBytes = wlen * 2;
    int dataAllocSize = 2 + wideBytes + 2;
    uint8_t* dataBase = (uint8_t*)_alloca(dataAllocSize);

    // 写 BOM 哨兵 (0xFF 0xFE)
    dataBase[0] = 0xFF;
    dataBase[1] = 0xFE;

    // data 指针指向 BOM 之后
    uint8_t* dataPtr = dataBase + 2;

    // 复制 wide 字符串数据
    memcpy(dataPtr, wcn.c_str(), wideBytes);

    // 写 null terminator
    *((wchar_t*)(dataPtr + wideBytes)) = 0;

    // 构造栈上临时 StrObj
    StrObj tmpObj;
    tmpObj.data = dataPtr;
    tmpObj.meta = (uint32_t)wlen | 0x1000000;  // 长度 + wide 标志
    tmpObj.extra = (uint32_t)wlen;

    // 日志前 100 条
    if (g_hitCount <= 100) {
        LogWrite("[HIT %d] \"%s\" -> cn(wlen=%d)\n",
            g_hitCount, enText.substr(0, 80).c_str(), wlen);
    }

    // 调用原 sub_628350, 用栈上临时 StrObj 作为 src
    return g_orig628350(ecx_this, edx_unused, (int)&tmpObj);
}

// ===================== Hook 安装 =====================
static bool InstallHooks() {
    uint8_t* p628350 = (uint8_t*)ADDR_628350;

    LogWrite("[Verify] sub_628350 bytes: %02X %02X %02X %02X\n",
        p628350[0], p628350[1], p628350[2], p628350[3]);

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        LogWrite("[Hook] MH_Initialize failed: %s\n", MH_StatusToString(status));
        return false;
    }

    status = MH_CreateHook((LPVOID)ADDR_628350, (LPVOID)&Hooked_628350, (LPVOID*)&g_orig628350);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook(628350) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook((LPVOID)ADDR_628350);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook(628350) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    LogWrite("[Hook] sub_628350 hooked, trampoline=%p\n", g_orig628350);

    return true;
}

// ===================== 主入口 =====================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        HMODULE hExe = GetModuleHandleA(nullptr);
        if (!hExe) return TRUE;

        char path[MAX_PATH];
        GetModuleFileNameA(hExe, path, MAX_PATH);
        if (!strstr(path, "MajestyHD.exe") && !strstr(path, "majestyhd.exe")) {
            return TRUE;
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

        g_logFile = fopen(logPath, "w");
        if (g_logFile) {
            fprintf(g_logFile, "[MajestyHD Runtime Localization v6.5] DllMain ATTACH\n");
            fprintf(g_logFile, "  Exe path: %s\n", path);
            fprintf(g_logFile, "  Log path: %s\n", logPath);
            fprintf(g_logFile, "  Strategy: hook sub_628350 with return-addr filter (XML-only)\n");
            fprintf(g_logFile, "  All malloc/free by game CRT, no cross-CRT issues\n\n");
            fflush(g_logFile);
        }

        // 加载字典
        char dictPath[MAX_PATH];
        GetModuleFileNameA(hModule, dictPath, MAX_PATH);
        char* p2 = strrchr(dictPath, '\\');
        if (p2) {
            strcpy(p2 + 1, "dict.txt");
        } else {
            strcpy(dictPath, "dict.txt");
        }

        if (LoadDict(dictPath)) {
            LogWrite("[Dict] Loaded %d entries from %s\n", g_dictCount, dictPath);
        } else {
            LogWrite("[ERROR] Failed to load dict from %s\n", dictPath);
        }

        // 安装 hooks
        if (InstallHooks()) {
            LogWrite("\n[Init] Hook installed successfully\n");
        } else {
            LogWrite("\n[ERROR] Hook installation failed\n");
        }

        fflush(g_logFile);

    } else if (dwReason == DLL_PROCESS_DETACH) {
        if (g_logFile) {
            fprintf(g_logFile, "\n[DllMain] DETACH\n");
            fprintf(g_logFile, "  Calls: %d, Lookups: %d, Hits: %d, Misses: %d\n",
                g_callCount, g_lookupCount, g_hitCount, g_missCount);
            fclose(g_logFile);
            g_logFile = nullptr;
        }
    }
    return TRUE;
}
