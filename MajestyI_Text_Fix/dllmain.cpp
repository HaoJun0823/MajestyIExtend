// dllmain.cpp : Majesty HD Runtime Localization v7.0 (只读诊断版)
//
// === 方案 (v7.0) ===
// 外挂汉化: 不修改任何原文件, 运行时 hook 渲染核心 sub_66CA70
//
// v7.0 只读诊断:
//   本版不替换渲染, 只 hook sub_66CA70 入口,
//   读取 this 字符串 + 起始索引 a1 + 查词典 + 打日志,
//   验证:
//     1) this 字符串能否正确读出完整文本
//     2) 起始索引 a1 是否通常为 0 (决定能否整体替换)
//     3) 词典命中情况
//     4) 调用频率 (是否为渲染热路径)
//   全部直接调用原函数, 不修改任何数据, 游戏行为完全不变。
//
// sub_66CA70 反汇编关键事实:
//   __thiscall(this, a1=startCharIdx, a2, a3, a4, a5, a6)  ret 0x18(6栈参)
//   this(edi): 字符串字段在 this 的 [0]/[4]/[8]:
//     - this[0] 非空 => this[0]=StrObj*, 其 [0]=data, [7]&1=wide
//     - this[0]==0   => this[4]=data, this[8]==1?wide:narrow
//   esi(a1) = 起始字符索引, 渲染从 a1 开始的子串
//   逐字符读取 [eax+7]&1 判 wide/narrow
//
// 字典文件: scripts/dict.txt (UTF-8, 格式: 英文\t中文\n)

#include "pch.h"
#include <psapi.h>
#include <intrin.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <set>
#include "MinHook.h"

#pragma comment(lib, "psapi.lib")
#pragma intrinsic(_ReturnAddress)

// ===================== 地址常量 =====================
static constexpr uintptr_t ADDR_66CA70 = 0x0066CA70; // 渲染核心 (thiscall, 6栈参 ret 0x18)

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
// sub_66CA70: __thiscall(this, a1..a6) -> int
// 用 __fastcall 模拟 __thiscall: ecx=this, edx=unused, a1..a6 全走栈
//   栈布局与 __thiscall 完全一致 (this 在 ecx, a1..a6 在栈 [esp+4]起)
typedef int (__fastcall *OrigRender_t)(int ecx_this, int edx_unused, int a1, int a2, int a3, int a4, int a5, int a6);
static OrigRender_t g_orig66CA70 = nullptr;

// ===================== Debug 日志 =====================
static FILE* g_logFile = nullptr;
static int g_callCount = 0;
static int g_lookupCount = 0;
static int g_hitCount = 0;
static int g_missCount = 0;
static int g_zeroIndexCount = 0;
static int g_nonZeroIndexCount = 0;

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

// ===================== 从 this 控件读取完整字符串 =====================
// this(edi) 字符串布局:
//   this[0] 非空 => this = StrObj*, 其 data=[+0], meta=[+4](低24位长, bit24 wide), extra=[+8]
//   this[0]==0   => this[4]=data, this[8]==1? wide : narrow
// 返回: 完整字符串 (UTF-8)。len 为字符数。
static bool ReadFullString(int thisPtr, std::string& out, int* outCharLen = nullptr, bool* outWide = nullptr) {
    if (!thisPtr) return false;
    uint8_t* edi = (uint8_t*)thisPtr;

    uint8_t* data = nullptr;
    bool wide = false;
    int chLen = 0;      // 字符数 (wide: wchar数; narrow: 字节数)

    void* obj = *(void**)edi;  // this[0]
    if (obj) {
        // StrObj 指针形式
        uint8_t* s = (uint8_t*)obj;
        wide = (s[7] & 1) != 0;
        chLen = (*(uint32_t*)(s + 4)) & 0xFFFFFF;  // meta 低24位
        data = *(uint8_t**)s;                       // data
    } else {
        // 内联形式
        wide = (*(uint32_t*)(edi + 8)) == 1;
        data = *(uint8_t**)(edi + 4);
    }

    if (!data) return false;

    if (outWide) *outWide = wide;

    if (chLen > 0) {
        // 已知长度: 直接转换
        if (wide) {
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)data, chLen, nullptr, 0, nullptr, nullptr);
            if (utf8Len <= 0) return false;
            out.resize(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)data, chLen, &out[0], utf8Len, nullptr, nullptr);
            if (outCharLen) *outCharLen = chLen;
            return true;
        } else {
            int actualLen = strnlen((const char*)data, (size_t)chLen);
            if (actualLen <= 0) actualLen = chLen;
            out.assign((const char*)data, actualLen);
            if (outCharLen) *outCharLen = actualLen;
            return true;
        }
    }

    // 长度未知, 需自行扫描
    int maxScan = 0;
    if (wide) {
        // 用 null terminator 扫描 (上限保护, 避免越界)
        maxScan = 256;
        const wchar_t* ws = (const wchar_t*)data;
        int n = 0;
        while (n < maxScan && ws[n] != 0) n++;
        if (n >= maxScan) return false;
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, ws, n, nullptr, 0, nullptr, nullptr);
        if (utf8Len <= 0) return false;
        out.resize(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, ws, n, &out[0], utf8Len, nullptr, nullptr);
        if (outCharLen) *outCharLen = n;
        return true;
    } else {
        maxScan = 256;
        const char* cs = (const char*)data;
        int n = (int)strnlen(cs, maxScan);
        if (n >= maxScan) return false;
        out.assign(cs, n);
        if (outCharLen) *outCharLen = n;
        return true;
    }
}

// ===================== Hook sub_66CA70 (只读诊断) =====================
// __thiscall(this, a1=startIdx, a2..a6) — 文本渲染核心
// 用 __fastcall 模拟: ecx=this, edx=unused, a1..a6 走栈
//
// v7 只读: 读取字符串 + 记录 a1 + 查词典 + 打日志, 不修改任何数据,
//          直接调用原函数继续渲染, 游戏行为不变。
int __fastcall Hooked_66CA70(int ecx_this, int edx_unused, int a1, int a2, int a3, int a4, int a5, int a6) {
    g_callCount++;

    // 起始索引统计
    if (a1 == 0) g_zeroIndexCount++;
    else g_nonZeroIndexCount++;

    // 读取完整字符串
    std::string enText;
    int chLen = 0;
    bool wide = false;
    bool readOk = ReadFullString((int)ecx_this, enText, &chLen, &wide);

    // 记录前 30 条调用 (含 a1, 字符串)
    static int s_debugCount = 0;
    if (s_debugCount < 30) {
        s_debugCount++;
        LogWrite("[CALL %d] this=%08X a1(startIdx)=%d chLen=%d wide=%d text=\"%s\"\n",
            s_debugCount, (unsigned int)ecx_this, a1, chLen, (int)wide,
            enText.substr(0, 80).c_str());
    }

    if (!readOk || enText.empty()) {
        // 无法读取或空串, 直接渲染
        return g_orig66CA70(ecx_this, edx_unused, a1, a2, a3, a4, a5, a6);
    }

    g_lookupCount++;

    // 查词典
    auto it = g_dict.find(enText);
    if (it != g_dict.end()) {
        // HIT: 记录
        g_hitCount++;
        if (g_hitCount <= 100) {
            LogWrite("[HIT %d] a1=%d \"%s\" -> cn(wlen=%d)\n",
                g_hitCount, a1, enText.substr(0, 80).c_str(), (int)it->second.size());
        }
    } else {
        // MISS: 记录有限条
        if (g_missCount < 60) {
            LogWrite("[MISS] a1=%d \"%s\"\n", a1, enText.substr(0, 80).c_str());
            g_missCount++;
        }
    }

    // 只读诊断: 不修改, 直接调原函数渲染
    return g_orig66CA70(ecx_this, edx_unused, a1, a2, a3, a4, a5, a6);
}

// ===================== Hook 安装 =====================
static bool InstallHooks() {
    uint8_t* p66CA70 = (uint8_t*)ADDR_66CA70;

    LogWrite("[Verify] sub_66CA70 bytes: %02X %02X %02X %02X %02X\n",
        p66CA70[0], p66CA70[1], p66CA70[2], p66CA70[3], p66CA70[4]);

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        LogWrite("[Hook] MH_Initialize failed: %s\n", MH_StatusToString(status));
        return false;
    }

    status = MH_CreateHook((LPVOID)ADDR_66CA70, (LPVOID)&Hooked_66CA70, (LPVOID*)&g_orig66CA70);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook(66CA70) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook((LPVOID)ADDR_66CA70);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook(66CA70) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    LogWrite("[Hook] sub_66CA70 hooked, trampoline=%p\n", g_orig66CA70);

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
            fprintf(g_logFile, "[MajestyHD Runtime Localization v7.0 只读诊断] DllMain ATTACH\n");
            fprintf(g_logFile, "  Exe path: %s\n", path);
            fprintf(g_logFile, "  Log path: %s\n", logPath);
            fprintf(g_logFile, "  Strategy: hook sub_66CA70 (render core), read-only diagnostic\n");
            fprintf(g_logFile, "  NOT modifying render, game behavior unchanged\n\n");
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
            fprintf(g_logFile, "  Calls=%d zeroIdx=%d nonZeroIdx=%d Lookups=%d Hits=%d Misses=%d\n",
                g_callCount, g_zeroIndexCount, g_nonZeroIndexCount, g_lookupCount, g_hitCount, g_missCount);
            fclose(g_logFile);
            g_logFile = nullptr;
        }
    }
    return TRUE;
}