// dllmain.cpp : Majesty HD Runtime Localization v9.2 (修正偏移 + CJK 字体扫描 + 字体替换)
//
// === v9.2 修正诊断版 ===
// v9.1 结果: this+0xA0 是 CYDialogButtonItem (不是字体!), 偏移错误
//
// 修正 (IDA 反编译确认):
//   sub_66E7B0 中 v5 = *(this + 10) -> DWORD 索引10 = 字节偏移 0x28
//   sub_647140(&renderCtx, v5, this[97]) -> renderCtx[0] = v5 (字体对象)
//   sub_647420 中:
//     - renderCtx[21] -> 备选字体
//     - dword_7CA9C0[renderCtx[22]] -> 全局字体数组
//   dword_7CA9C0 数组条目是 sub_68A9C0(256) 创建的 glyph cache (0x41C 字节)
//     不是字体对象! vtable=0 是正常的
//
// 字体类 vtable (IDA 确认):
//   CYFont          vtable = 0x74C2C4
//   CYFontPixelmap  vtable = 0x75041C  (CJK 字体)
//   CYFontImage     vtable = 待确认
//
// v9.2 策略:
//   1. 修正字体对象偏移为 this+0x28
//   2. 诊断当前字体对象的 RTTI 类型
//   3. 在内存中扫描 CYFontPixelmap vtable (0x75041C)
//   4. 如果找到 CJK 字体, 在 hit 时替换 this[10]

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
static constexpr uintptr_t ADDR_7CA9C0  = 0x007CA9C0;  // dword_7CA9C0 (字体数组指针)
static constexpr uintptr_t ADDR_7BC518  = 0x007BC518;  // dword_7BC518 (字体数组大小)
static constexpr uintptr_t ADDR_7CA9E4  = 0x007CA9E4;  // dword_7CA9E4 (默认渲染设备)
static constexpr uintptr_t VTABLE_CYFONT         = 0x0074C2C4;  // CYFont 基类 vtable
static constexpr uintptr_t VTABLE_CYFONTPIXELMAP  = 0x0075041C;  // CYFontPixelmap vtable (CJK)

// ===================== 字符串对象 =====================
struct StrObj {
    void*    data;   // [0]
    uint32_t meta;   // [4] 低3字节=长度, bit24=wide标志
    uint32_t extra;  // [8]
};

// ===================== 字典 =====================
struct DictEntry {
    std::vector<uint8_t> cnUtf16LE;
    int cnWcharCount;
};
static std::unordered_map<std::string, DictEntry> g_dict;
static int g_dictCount = 0;

// ===================== 原始函数指针 =====================
typedef int (__fastcall *OrigDraw_t)(int ecx_this, int edx_unused, int a2, int a3, int a4);
static OrigDraw_t g_orig66E7B0 = nullptr;

// ===================== Debug 日志 =====================
static FILE* g_logFile = nullptr;
static int g_callCount = 0;
static int g_replacedCount = 0;
static int g_hitCount = 0;
static int g_missCount = 0;
static bool g_fontDiagDone = false;

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

        for (size_t i = 0; i + 1 < cnStr.size(); i++) {
            if (cnStr[i] == '\\' && cnStr[i+1] == 'n') {
                cnStr[i] = '\n';
                cnStr.erase(i+1, 1);
            }
        }

        int wlen = MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(), nullptr, 0);
        if (wlen <= 0) continue;

        DictEntry entry;
        entry.cnWcharCount = wlen;
        entry.cnUtf16LE.resize((wlen + 1) * 2);
        MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(),
            (wchar_t*)entry.cnUtf16LE.data(), wlen);
        *(wchar_t*)(entry.cnUtf16LE.data() + wlen * 2) = 0;

        g_dict[enStr] = std::move(entry);
        g_dictCount++;
    }

    return true;
}

// ===================== 从 this 读取完整字符串 =====================
static bool ReadFullString(int thisPtr, std::string& out, int* outCharLen = nullptr, bool* outWide = nullptr) {
    if (!thisPtr) return false;
    uint8_t* edi = (uint8_t*)thisPtr;

    uint8_t* data = nullptr;
    bool wide = false;
    int chLen = 0;

    void* obj = *(void**)edi;
    if (obj) {
        uint8_t* s = (uint8_t*)obj;
        wide = (s[7] & 1) != 0;
        chLen = (*(uint32_t*)(s + 4)) & 0xFFFFFF;
        data = *(uint8_t**)s;
    } else {
        wide = (*(uint32_t*)(edi + 8)) == 1;
        data = *(uint8_t**)(edi + 4);
        chLen = 0;
    }

    if (!data) return false;
    if (outWide) *outWide = wide;

    if (wide) {
        int n = chLen;
        if (n <= 0) {
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
            const char* cs = (const char*)data;
            n = (int)strnlen(cs, 256);
            if (n >= 256) return false;
        }
        out.assign((const char*)data, n);
        if (outCharLen) *outCharLen = n;
        return true;
    }
}

// ===================== RTTI 安全读取 =====================
// MSVC RTTI 链: vtable-4 -> COL -> COL+12 -> TypeDescriptor -> TD+8 -> name
static bool ReadRttiName(uint32_t objPtr, char* buf, int bufSize) {
    if (!objPtr || bufSize < 8) return false;
    buf[0] = 0;
    if (IsBadReadPtr((void*)objPtr, 4)) return false;
    uint32_t vtable = *(uint32_t*)objPtr;
    if (!vtable) return false;
    if (IsBadReadPtr((void*)(vtable - 4), 4)) return false;
    uint32_t colPtr = *(uint32_t*)(vtable - 4);  // Complete Object Locator
    if (!colPtr) return false;
    if (IsBadReadPtr((void*)(colPtr + 12), 4)) return false;
    uint32_t typeDescPtr = *(uint32_t*)(colPtr + 12);  // TypeDescriptor ptr
    if (!typeDescPtr) return false;
    // 尝试直接 VA (老 MSVC)
    const char* typeName = (const char*)(typeDescPtr + 8);
    if (!IsBadReadPtr((void*)typeName, bufSize)) {
        strncpy(buf, typeName, bufSize - 1);
        buf[bufSize - 1] = 0;
        return true;
    }
    // 尝试 RVA (新 MSVC, image base=0x400000)
    uint32_t rvaAddr = 0x400000 + typeDescPtr;
    const char* typeNameRva = (const char*)(rvaAddr + 8);
    if (!IsBadReadPtr((void*)typeNameRva, bufSize)) {
        strncpy(buf, typeNameRva, bufSize - 1);
        buf[bufSize - 1] = 0;
        return true;
    }
    return false;
}

// ===================== CJK 字体扫描 =====================
// 在堆内存中扫描 vtable=0x75041C (CYFontPixelmap) 的对象
static uint32_t g_cjkFontObj = 0;  // 找到的 CJK 字体对象

static void ScanForCjkFont() {
    if (g_cjkFontObj) return;

    HANDLE hProcess = GetCurrentProcess();
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    uint32_t addr = (uint32_t)si.lpMinimumApplicationAddress;
    uint32_t endAddr = (uint32_t)si.lpMaximumApplicationAddress;
    uint32_t targetVtable = (uint32_t)VTABLE_CYFONTPIXELMAP;

    MEMORY_BASIC_INFORMATION mbi;
    LogWrite("[FontScan] Scanning for CYFontPixelmap (vtable=0x%08X)...\n", targetVtable);

    int found = 0;
    while (addr < endAddr) {
        if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE|PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & PAGE_READONLY) && mbi.RegionSize > 0 && mbi.RegionSize < 0x10000000) {

            // 扫描这个区域
            uint32_t regionStart = (uint32_t)mbi.BaseAddress;
            uint32_t regionSize = (uint32_t)mbi.RegionSize;

            // 安全限制: 最多扫描 4MB
            uint32_t scanSize = regionSize > 0x400000 ? 0x400000 : regionSize;

            // 分块读取
            uint8_t* buf = (uint8_t*)malloc(scanSize);
            if (buf) {
                SIZE_T bytesRead = 0;
                if (ReadProcessMemory(hProcess, (void*)regionStart, buf, scanSize, &bytesRead) && bytesRead > 4) {
                    // 搜索 vtable 指针值 (4字节对齐)
                    for (uint32_t off = 0; off + 4 <= (uint32_t)bytesRead; off += 4) {
                        uint32_t val = *(uint32_t*)(buf + off);
                        if (val == targetVtable) {
                            uint32_t objAddr = regionStart + off;
                            // 验证: CYFontPixelmap 的 this+4 应该是 1 (构造函数设置)
                            if (off + 8 <= scanSize) {
                                uint32_t field4 = *(uint32_t*)(buf + off + 4);
                                if (field4 == 1) {
                                    LogWrite("[FontScan] FOUND CYFontPixelmap at 0x%08X (field4=1)\n", objAddr);
                                    if (!g_cjkFontObj) g_cjkFontObj = objAddr;
                                    found++;
                                    if (found >= 10) break;  // 最多记 10 个
                                }
                            }
                        }
                    }
                }
                free(buf);
            }
            if (found >= 10) break;
        }
        addr = (uint32_t)mbi.BaseAddress + (uint32_t)mbi.RegionSize;
    }

    LogWrite("[FontScan] Found %d CYFontPixelmap object(s), g_cjkFontObj=0x%08X\n", found, g_cjkFontObj);
}

// ===================== 字体诊断 =====================
static void FontDiag(int thisPtr) {
    if (g_fontDiagDone) return;
    g_fontDiagDone = true;

    uint8_t* base = (uint8_t*)thisPtr;

    // 修正: 字体对象在 this+0x28 (DWORD 索引 10)
    uint32_t fontObj = *(uint32_t*)(base + 0x28);
    // 字体 ID 在 this+0x61 (字节偏移 97)
    uint8_t  fontId  = *(uint8_t*)(base + 0x61);

    LogWrite("\n[FontDiag] === FONT DIAGNOSTICS (v9.2) ===\n");
    LogWrite("[FontDiag] this=0x%08X\n", thisPtr);
    LogWrite("[FontDiag] this+0x28 (fontObj) = 0x%08X\n", fontObj);
    LogWrite("[FontDiag] this+0x61 (fontId)  = %d\n", fontId);

    // 全局字体数组
    uint32_t* fontArray = *(uint32_t**)ADDR_7CA9C0;
    uint32_t arraySize = *(uint32_t*)ADDR_7BC518;
    LogWrite("[FontDiag] dword_7CA9C0 (fontArrayPtr) = 0x%08X\n", (uint32_t)(uintptr_t)fontArray);
    LogWrite("[FontDiag] dword_7BC518 (arraySize)    = %d\n", arraySize);

    if (fontArray && arraySize > 0 && arraySize < 100) {
        for (uint32_t i = 0; i < arraySize; i++) {
            uint32_t entry = fontArray[i];
            LogWrite("[FontDiag] fontArray[%d] = 0x%08X\n", i, entry);
            if (entry) {
                uint32_t vtable = *(uint32_t*)entry;
                LogWrite("[FontDiag]   vtable = 0x%08X\n", vtable);
                // 注意: 这些是 glyph cache (sub_68A9C0), 不是字体对象, vtable=0 正常
            }
        }
    }

    // fontObj 详细信息
    if (fontObj) {
        uint32_t vtable = *(uint32_t*)fontObj;
        LogWrite("[FontDiag] fontObj vtable = 0x%08X\n", vtable);
        char nameBuf[65] = {0};
        if (ReadRttiName(fontObj, nameBuf, sizeof(nameBuf))) {
            LogWrite("[FontDiag] fontObj RTTI name = %s\n", nameBuf);
        } else {
            LogWrite("[FontDiag] (fontObj RTTI read failed)\n");
        }
        // 检查是否是已知的字体 vtable
        if (vtable == VTABLE_CYFONT) {
            LogWrite("[FontDiag] -> CYFont (base class, 英文字体)\n");
        } else if (vtable == VTABLE_CYFONTPIXELMAP) {
            LogWrite("[FontDiag] -> CYFontPixelmap (CJK 字体!)\n");
        }
        // 前 64 字节 hex dump
        LogWrite("[FontDiag] fontObj bytes: ");
        for (int i = 0; i < 64; i++) {
            fprintf(g_logFile, "%02X ", ((uint8_t*)fontObj)[i]);
        }
        fprintf(g_logFile, "\n");
    }

    // 扫描 CJK 字体
    ScanForCjkFont();

    LogWrite("[FontDiag] === END FONT DIAGNOSTICS ===\n\n");
    fflush(g_logFile);
}

// ===================== Hook sub_66E7B0 =====================
int __fastcall Hooked_66E7B0(int ecx_this, int edx_unused, int a2, int a3, int a4) {
    g_callCount++;

    // 字体诊断 (仅首次)
    FontDiag(ecx_this);

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
        g_missCount++;
        if (g_missCount <= 60) {
            LogWrite("[MISS] \"%s\" (wide=%d chLen=%d)\n",
                enText.substr(0, 80).c_str(), (int)wide, chLen);
        }
        return g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);
    }

    // 命中!
    g_hitCount++;
    const DictEntry& entry = it->second;

    if (g_hitCount <= 80) {
        LogWrite("[HIT %d] \"%s\" -> cn(wchars=%d)\n",
            g_hitCount, enText.substr(0, 60).c_str(), entry.cnWcharCount);
    }

    // 构造临时 inline wide StrObj
    uint8_t* edi = (uint8_t*)ecx_this;

    uint32_t origThis0 = *(uint32_t*)edi;
    uint32_t origThis4 = *(uint32_t*)(edi + 4);
    uint32_t origThis8 = *(uint32_t*)(edi + 8);

    // 如果找到 CJK 字体, 同时替换字体对象 (this+0x28)
    uint32_t origFontObj = 0;
    bool fontReplaced = false;
    if (g_cjkFontObj) {
        origFontObj = *(uint32_t*)(edi + 0x28);
        if (origFontObj != g_cjkFontObj) {
            *(uint32_t*)(edi + 0x28) = g_cjkFontObj;
            fontReplaced = true;
            if (g_hitCount <= 10) {
                LogWrite("  [FontRepl] 0x%08X -> 0x%08X\n", origFontObj, g_cjkFontObj);
            }
        }
    }

    *(uint32_t*)edi = 0;
    *(uint32_t*)(edi + 4) = (uint32_t)(uintptr_t)entry.cnUtf16LE.data();
    *(uint32_t*)(edi + 8) = 1;

    g_replacedCount++;

    int result = g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);

    // 还原 this
    *(uint32_t*)edi = origThis0;
    *(uint32_t*)(edi + 4) = origThis4;
    *(uint32_t*)(edi + 8) = origThis8;
    if (fontReplaced) {
        *(uint32_t*)(edi + 0x28) = origFontObj;
    }

    return result;
}

// ===================== Hook 安装 =====================
static bool InstallHooks() {
    uint8_t* p66E7B0 = (uint8_t*)ADDR_66E7B0;

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
            fprintf(g_logFile, "[MajestyHD Runtime Localization v9.2 CJK字体扫描] DllMain ATTACH\n");
            fprintf(g_logFile, "  Exe path: %s\n", path);
            fprintf(g_logFile, "  Log path: %s\n", logPath);
            fprintf(g_logFile, "  Strategy: hook sub_66E7B0 + font scan + font replace\n\n");
            fflush(g_logFile);
        }

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

        if (InstallHooks()) {
            LogWrite("\n[Init] Hook installed successfully\n");
        } else {
            LogWrite("\n[ERROR] Hook installation failed\n");
        }

        fflush(g_logFile);

    } else if (dwReason == DLL_PROCESS_DETACH) {
        if (g_logFile) {
            fprintf(g_logFile, "\n[DllMain] DETACH (v9.2)\n");
            fprintf(g_logFile, "  Calls=%d Replaced=%d Hits=%d Misses=%d\n",
                g_callCount, g_replacedCount, g_hitCount, g_missCount);
            fclose(g_logFile);
            g_logFile = nullptr;
        }
    }
    return TRUE;
}
