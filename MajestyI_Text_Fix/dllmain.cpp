// dllmain.cpp : Majesty HD Runtime Localization v9.0 (绘制函数 hook)
//
// === 方案 (v9.0) ===
// 外挂汉化: hook sub_66E7B0 (真正的文本绘制函数), 在入口替换 this 字符串为中文
//
// v8 分析结果:
//   sub_66CA70 是测量函数, 不是绘制函数 → v8 替换无效 (只改变测量, 不改变绘制)
//   sub_66E7B0 是真正的文本绘制函数 (0x717 bytes, 15 callers)
//     - 内部逐字符调用 sub_647420 (实际字符 blit, 调 renderContext->vtable[10])
//     - 调用 sub_66CA70 (测量) + sub_66C7D0 (推进) + sub_647020 (设位置)
//     - this 对象的 StrObj 布局与 sub_66CA70 完全相同
//   sub_647420: 实际字符绘制, 调 renderContext vtable[10](char, x, y, ...)
//
// v9.0 策略:
//   - hook sub_66E7B0 入口 (0x0066E7B0)
//   - 读取 this 字符串(英文原文)
//   - 查词典, 命中时:
//     构造临时 inline wide StrObj, 替换 this 的字符串指针
//     调原函数绘制中文 (测量+绘制均使用中文), 绘制后还原 this
//   - 未命中或读取失败: 直接调原函数
//   - 无 a1==0 过滤 (sub_66E7B0 不分片, 总是绘制完整字符串)
//
// sub_66E7B0 入口字节 (SEH prologue):
//   6A FF              push -1
//   68 E8 46 72 00     push offset SEH_66E7B0
//   64 A1 00 00 00 00  mov eax, fs:0
//   MinHook 需要 5+ 字节, 取前 7 字节 (2 条指令) 做 trampoline
//   push -1 和 push offset 均为立即数, 可安全重定位
//
// StrObj 布局 (sub_66E7B0 与 sub_66CA70 相同):
//   this[0] 非空 => StrObj*, [0]=data, [4]=meta(低24位长,bit24 wide), [7]&1=wide
//   this[0]==0   => this[4]=data, this[8]==1?wide:narrow (inline 形式)
//
// 替换策略:
//   构造 inline wide StrObj: 设 this[0]=0, this[4]=cnData(wchar*), this[8]=1
//   原函数走 inline wide 路径: cmp [edi+8],1 -> je wide -> mov cx,[ecx+esi*2]
//   临时数据在 DLL 全局 buffer, 生命周期覆盖绘制调用
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
static constexpr uintptr_t ADDR_66E7B0 = 0x0066E7B0;

// ===================== 字符串对象 =====================
struct StrObj {
    void*    data;   // [0]
    uint32_t meta;   // [4] 低3字节=长度, bit24=wide标志
    uint32_t extra;  // [8]
};

// ===================== 字典 =====================
// key: 英文(UTF-8), value: 中文 UTF-16LE 字节(可直接用于 wide 渲染)
struct DictEntry {
    std::vector<uint8_t> cnUtf16LE;  // UTF-16LE 字节 (含 null terminator)
    int cnWcharCount;                // wchar 数 (不含 null)
};
static std::unordered_map<std::string, DictEntry> g_dict;
static int g_dictCount = 0;

// ===================== 原始函数指针 =====================
// sub_66E7B0 是 __thiscall(this, a2, a3, a4)
// 用 __fastcall 捕获 ecx=this, edx 忽略
typedef int (__fastcall *OrigDraw_t)(int ecx_this, int edx_unused, int a2, int a3, int a4);
static OrigDraw_t g_orig66E7B0 = nullptr;

// ===================== Debug 日志 =====================
static FILE* g_logFile = nullptr;
static int g_callCount = 0;
static int g_replacedCount = 0;
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
            if (c2 >= 0x8B && c2 <= 0x8F) { read += 3; continue; }
        }
        if (read + 2 < len && c == 0xE2 &&
            (unsigned char)data[read+1] == 0x81) {
            unsigned char c2 = (unsigned char)data[read+2];
            if (c2 >= 0xA0 && c2 <= 0xA4) { read += 3; continue; }
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

        // 处理 \n 转义 -> 真换行
        for (size_t i = 0; i + 1 < cnStr.size(); i++) {
            if (cnStr[i] == '\\' && cnStr[i+1] == 'n') {
                cnStr[i] = '\n';
                cnStr.erase(i+1, 1);
            }
        }

        // 转 UTF-16LE
        int wlen = MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(), nullptr, 0);
        if (wlen <= 0) continue;

        DictEntry entry;
        entry.cnWcharCount = wlen;
        entry.cnUtf16LE.resize((wlen + 1) * 2);  // +1 for null terminator
        MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(),
            (wchar_t*)entry.cnUtf16LE.data(), wlen);
        // null terminator
        *(wchar_t*)(entry.cnUtf16LE.data() + wlen * 2) = 0;

        g_dict[enStr] = std::move(entry);
        g_dictCount++;
    }

    return true;
}

// ===================== 从 this 读取完整字符串 (UTF-8) =====================
static bool ReadFullString(int thisPtr, std::string& out, int* outCharLen = nullptr, bool* outWide = nullptr) {
    if (!thisPtr) return false;
    uint8_t* edi = (uint8_t*)thisPtr;

    uint8_t* data = nullptr;
    bool wide = false;
    int chLen = 0;

    void* obj = *(void**)edi;  // this[0]
    if (obj) {
        uint8_t* s = (uint8_t*)obj;
        wide = (s[7] & 1) != 0;
        chLen = (*(uint32_t*)(s + 4)) & 0xFFFFFF;
        data = *(uint8_t**)s;
    } else {
        wide = (*(uint32_t*)(edi + 8)) == 1;
        data = *(uint8_t**)(edi + 4);
        chLen = 0; // inline 形式没有显式长度
    }

    if (!data) return false;
    if (outWide) *outWide = wide;

    if (wide) {
        // UTF-16 -> UTF-8
        int n = chLen;
        if (n <= 0) {
            // 扫描 null terminator
            const wchar_t* ws = (const wchar_t*)data;
            n = 0;
            while (n < 256 && ws[n] != 0) n++;
            if (n >= 256) return false;
        }
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)data, n, nullptr, 0, nullptr, nullptr);
        if (utf8Len <= 0) return false;
        out.resize(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)data, n, &out[0], utf8Len, nullptr, nullptr);
        if (outCharLen) *outCharLen = n;
        return true;
    } else {
        int n = chLen;
        if (n <= 0) {
            // 扫描 null terminator
            const char* cs = (const char*)data;
            n = (int)strnlen(cs, 256);
            if (n >= 256) return false;
        }
        out.assign((const char*)data, n);
        if (outCharLen) *outCharLen = n;
        return true;
    }
}

// ===================== Hook sub_66E7B0 (绘制函数, 翻译替换) =====================
int __fastcall Hooked_66E7B0(int ecx_this, int edx_unused, int a2, int a3, int a4) {
    g_callCount++;

    // 读取完整字符串
    std::string enText;
    bool wide = false;
    int chLen = 0;
    bool readOk = ReadFullString(ecx_this, enText, &chLen, &wide);

    if (!readOk || enText.empty()) {
        return g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);
    }

    // 查词典
    auto it = g_dict.find(enText);
    if (it == g_dict.end()) {
        // 未命中
        g_missCount++;
        if (g_missCount <= 60) {
            LogWrite("[MISS] \"%s\" (wide=%d chLen=%d)\n",
                enText.substr(0, 80).c_str(), (int)wide, chLen);
        }
        return g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);
    }

    // 命中! 替换为中文
    g_hitCount++;
    const DictEntry& entry = it->second;

    if (g_hitCount <= 80) {
        LogWrite("[HIT %d] \"%s\" -> cn(wchars=%d)\n",
            g_hitCount, enText.substr(0, 60).c_str(), entry.cnWcharCount);
    }

    // 构造临时 inline wide StrObj
    // this[0]=0, this[4]=cnData(wchar*), this[8]=1(wide)
    uint8_t* edi = (uint8_t*)ecx_this;

    // 保存原始值
    uint32_t origThis0 = *(uint32_t*)edi;
    uint32_t origThis4 = *(uint32_t*)(edi + 4);
    uint32_t origThis8 = *(uint32_t*)(edi + 8);

    // 替换为 inline wide
    *(uint32_t*)edi = 0;                                    // this[0] = 0 (inline)
    *(uint32_t*)(edi + 4) = (uint32_t)(uintptr_t)entry.cnUtf16LE.data(); // this[4] = data ptr
    *(uint32_t*)(edi + 8) = 1;                             // this[8] = 1 (wide)

    g_replacedCount++;

    // 调原函数绘制中文
    int result = g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);

    // 还原 this
    *(uint32_t*)edi = origThis0;
    *(uint32_t*)(edi + 4) = origThis4;
    *(uint32_t*)(edi + 8) = origThis8;

    return result;
}

// ===================== Hook 安装 =====================
static bool InstallHooks() {
    uint8_t* p66E7B0 = (uint8_t*)ADDR_66E7B0;

    // 验证入口字节: 应为 6A FF (push -1, SEH prologue)
    LogWrite("[Verify] sub_66E7B0 bytes: %02X %02X %02X %02X %02X %02X %02X\n",
        p66E7B0[0], p66E7B0[1], p66E7B0[2], p66E7B0[3], p66E7B0[4], p66E7B0[5], p66E7B0[6]);

    if (p66E7B0[0] != 0x6A || p66E7B0[1] != 0xFF) {
        LogWrite("[ERROR] sub_66E7B0 byte mismatch: expected 6A FF, got %02X %02X\n",
            p66E7B0[0], p66E7B0[1]);
        return false;
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        LogWrite("[Hook] MH_Initialize failed: %s\n", MH_StatusToString(status));
        return false;
    }

    status = MH_CreateHook((LPVOID)ADDR_66E7B0, (LPVOID)&Hooked_66E7B0, (LPVOID*)&g_orig66E7B0);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook(66E7B0) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook((LPVOID)ADDR_66E7B0);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook(66E7B0) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    LogWrite("[Hook] sub_66E7B0 hooked, trampoline=%p\n", g_orig66E7B0);

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
            fprintf(g_logFile, "[MajestyHD Runtime Localization v9.0 绘制函数Hook] DllMain ATTACH\n");
            fprintf(g_logFile, "  Exe path: %s\n", path);
            fprintf(g_logFile, "  Log path: %s\n", logPath);
            fprintf(g_logFile, "  Strategy: hook sub_66E7B0 (real draw func), replace this string with CN\n\n");
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
            fprintf(g_logFile, "  Calls=%d Replaced=%d Hits=%d Misses=%d\n",
                g_callCount, g_replacedCount, g_hitCount, g_missCount);
            fclose(g_logFile);
            g_logFile = nullptr;
        }
    }
    return TRUE;
}
