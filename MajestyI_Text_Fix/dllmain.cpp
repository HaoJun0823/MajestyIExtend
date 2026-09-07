// dllmain.cpp : Majesty HD Runtime Localization v5
//
// === 方案 (v5) ===
// 外挂汉化: 不修改任何原文件, 运行时 hook 文本检索函数,
// 查字典替换英文为中文.
//
// Hook 点:
// 1. sub_64D6B0 @ 0x64D6B0 — __stdcall(int key) — IDTXT_ 文本检索
//    调原函数拿英文 StrObj → 读英文文本 → 查字典 → 有翻译则构造 wide StrObj 返回
//
// 2. sub_664660 @ 0x664660 — __cdecl(filename, section_id, index) — STRT 文本检索
//    同理
//
// 3. sub_6646F0 @ 0x6646F0 — __cdecl(filename, section_id, key) — STRT 按 key 检索
//    同理
//
// 字符串对象布局 (12 字节):
//   [0] void*    data   — 数据指针 (wide: malloc+2, 前2字节=0xFEFF 哨兵)
//   [4] uint32_t meta   — 低3字节=长度, bit24(0x1000000)=wide标志, byte7 bit0=wide flag
//   [8] uint32_t extra  — 长度/hash
//
// 返回中文的方式:
//   用 VirtualAlloc 分配永久内存, 构造 wide StrObj
//   data 指向: [0xFF 0xFE] [wide string data] [0x00 0x00]
//   meta = wlen | 0x1000000  (wide 标志)
//   extra = wlen
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
static constexpr uintptr_t ADDR_64D6B0 = 0x0064D6B0;
static constexpr uintptr_t ADDR_664660 = 0x00664660;
static constexpr uintptr_t ADDR_6646F0 = 0x006646F0;
static constexpr uintptr_t ADDR_64D5F0 = 0x0064D5F0; // XML dict lookup (thiscall)
static constexpr uintptr_t ADDR_508480 = 0x00508480; // XML+CAM text search (cdecl)

// ===================== 字符串对象 =====================
struct StrObj {
    void*    data;   // [0]
    uint32_t meta;   // [4] 低3字节=长度, bit24=wide, byte7 bit0=wide flag
    uint32_t extra;  // [8]
};

// ===================== 字典 =====================
static std::unordered_map<std::string, std::wstring> g_dict;
static int g_dictCount = 0;

// ===================== 原始函数指针 =====================
// sub_64D6B0: __stdcall(int a1) -> int (StrObj*)
typedef int (__stdcall *OrigGetText_t)(int a1);
static OrigGetText_t g_orig64D6B0 = nullptr;

// sub_664660: __cdecl(int filename, int section_id, int index) -> int (StrObj*)
typedef int (__cdecl *OrigGetStrText_t)(int a1, int a2, int a3);
static OrigGetStrText_t g_orig664660 = nullptr;

// sub_6646F0: __cdecl(int filename, int section_id, int key) -> int (StrObj*)
static OrigGetStrText_t g_orig6646F0 = nullptr;

// sub_64D5F0: __thiscall(this, int hash_key) -> int (StrObj*)
// 使用 __fastcall 模拟 __thiscall (ecx=this, first arg on stack)
typedef int (__fastcall *OrigXmlLookup_t)(int ecx_this, int edx_unused, int hash_key);
static OrigXmlLookup_t g_orig64D5F0 = nullptr;

// sub_508480: __cdecl(int hash_key, int out_str_obj) -> bool
// XML+CAM text search: 先查 CAM STRT, 再查 XML 字典
typedef int (__cdecl *OrigXmlSearch_t)(int hash_key, int out_str_obj);
static OrigXmlSearch_t g_orig508480 = nullptr;

// ===================== Debug 日志 =====================
static FILE* g_logFile = nullptr;
static int g_lookupCount = 0;
static int g_hitCount = 0;
static int g_missCount = 0;

// Per-hook call counters
static int g_call64D6B0 = 0;
static int g_call664660 = 0;
static int g_call6646F0 = 0;
static int g_call64D5F0 = 0;
static int g_call508480 = 0;

// Loop detection: track last call per hook
static int g_lastArg64D6B0 = -1;
static int g_lastArg64D6B0_repeatCount = 0;
static int g_lastArg664660_a3 = -1;
static int g_lastArg664660_repeatCount = 0;

static void LogWrite(const char* fmt, ...) {
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fflush(g_logFile);
}

// Dump StrObj contents for debugging
static void DumpStrObj(const char* label, StrObj* obj) {
    if (!obj) {
        LogWrite("  %s: NULL\n", label);
        return;
    }
    bool isWide = (*(uint8_t*)((char*)obj + 7) & 1) != 0;
    int len = obj->meta & 0xFFFFFF;
    LogWrite("  %s: data=%p meta=%08X extra=%08X isWide=%d len=%d\n",
        label, obj->data, obj->meta, obj->extra, isWide ? 1 : 0, len);
    if (obj->data && len > 0) {
        // Dump first 32 bytes of data
        uint8_t* d = (uint8_t*)obj->data;
        char hex[128] = {0};
        int dumpLen = len < 16 ? len : 16;
        for (int i = 0; i < dumpLen && i < 16; i++) {
            sprintf(hex + i * 3, "%02X ", d[i]);
        }
        LogWrite("    data[0..%d]: %s\n", dumpLen - 1, hex);
        // If narrow, try to print as string
        if (!isWide && len < 256) {
            char text[256] = {0};
            memcpy(text, d, len < 255 ? len : 255);
            LogWrite("    text: \"%s\"\n", text);
        } else if (isWide && len < 128) {
            // Wide: data 直接指向 UTF-16LE (无 BOM)
            const wchar_t* wstr = (const wchar_t*)d;
            char text[512] = {0};
            WideCharToMultiByte(CP_ACP, 0, wstr, len, text, 511, nullptr, nullptr);
            LogWrite("    wtext: \"%s\"\n", text);
        }
    }
}

// ===================== 字典加载 =====================

// 从整个文件数据中移除所有零宽字符序列 (UTF-8 编码)
// 零宽字符: U+200B~U+200F (E2 80 8B~8F), U+2060~U+2064 (E2 81 A0~A4), U+FEFF (EF BB BF)
// 同时移除 UTF-8 BOM (EF BB BF)
static int RemoveZeroWidthChars(char* data, int len) {
    int write = 0;
    int read = 0;
    while (read < len) {
        unsigned char c = (unsigned char)data[read];
        // UTF-8 BOM / U+FEFF (EF BB BF)
        if (read + 2 < len && c == 0xEF &&
            (unsigned char)data[read+1] == 0xBB && (unsigned char)data[read+2] == 0xBF) {
            read += 3;
            continue;
        }
        // U+200B~U+200F (E2 80 8B~8F)
        if (read + 2 < len && c == 0xE2 &&
            (unsigned char)data[read+1] == 0x80) {
            unsigned char c2 = (unsigned char)data[read+2];
            if (c2 >= 0x8B && c2 <= 0x8F) {
                read += 3;
                continue;
            }
        }
        // U+2060~U+2064 (E2 81 A0~A4)
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
    FILE* f = fopen(path, "rb");  // 用二进制模式打开, 避免文本模式转换问题
    if (!f) {
        return false;
    }

    // 读取整个文件到内存
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0) {
        fclose(f);
        return false;
    }

    // 限制最大 4MB
    if (fileSize > 4 * 1024 * 1024) {
        LogWrite("[Dict] File too large: %ld bytes, truncating to 4MB\n", fileSize);
        fileSize = 4 * 1024 * 1024;
    }

    std::vector<char> fileData(fileSize + 1, 0);
    fread(fileData.data(), 1, fileSize, f);
    fclose(f);

    // 全局移除零宽字符 (AIGC 水印可能在文件任意位置)
    int cleanLen = RemoveZeroWidthChars(fileData.data(), (int)fileSize);
    fileData[cleanLen] = '\0';
    LogWrite("[Dict] File size: %ld -> %d (after removing zero-width chars)\n", fileSize, cleanLen);

    // 逐行解析
    int pos = 0;
    int lineNum = 0;
    while (pos < cleanLen) {
        lineNum++;

        // 找行尾
        int lineStart = pos;
        while (pos < cleanLen && fileData[pos] != '\n') {
            pos++;
        }
        int lineLen = pos - lineStart;
        if (pos < cleanLen) pos++; // 跳过 \n

        // 跳过空行
        if (lineLen == 0) continue;
        if (lineLen == 1 && fileData[lineStart] == '\r') continue;

        // 复制到工作缓冲区
        if (lineLen >= 8192) {
            LogWrite("[Dict] Line %d too long (%d bytes), skipping\n", lineNum, lineLen);
            continue;
        }

        unsigned char lineBuf[8192];
        memcpy(lineBuf, fileData.data() + lineStart, lineLen);
        lineBuf[lineLen] = '\0';
        int actualLen = lineLen;

        // 去掉行尾 \r
        while (actualLen > 0 && (lineBuf[actualLen-1] == '\r' || lineBuf[actualLen-1] == '\n')) {
            lineBuf[--actualLen] = '\0';
        }
        if (actualLen == 0) continue;

        // 找 tab 分隔符
        char* tab = (char*)memchr(lineBuf, '\t', actualLen);
        if (!tab) continue;

        // 提取英文 key
        int enLen = (int)(tab - (char*)lineBuf);
        if (enLen <= 0 || enLen >= 4096) continue;

        char enKey[4096];
        memcpy(enKey, lineBuf, enLen);
        enKey[enLen] = '\0';

        // 去掉 key 末尾的 \r
        while (enLen > 0 && (enKey[enLen-1] == '\r' || enKey[enLen-1] == '\n')) {
            enKey[--enLen] = '\0';
        }
        if (enLen == 0) continue;

        // 提取中文 value
        char* cnStart = tab + 1;
        int cnLen = actualLen - enLen - 1;  // -1 for tab
        if (cnLen <= 0 || cnLen >= 4096) continue;

        // 去掉末尾的 \n \r
        while (cnLen > 0 && (cnStart[cnLen-1] == '\r' || cnStart[cnLen-1] == '\n')) {
            cnStart[--cnLen] = '\0';
        }
        if (cnLen == 0) continue;

        // 处理 \\n 转义为真实换行
        std::string enStr(enKey, enLen);
        std::string cnStr(cnStart, cnLen);

        // 替换 \\n 为 \n
        for (size_t i = 0; i + 1 < cnStr.size(); i++) {
            if (cnStr[i] == '\\' && cnStr[i+1] == 'n') {
                cnStr[i] = '\n';
                cnStr.erase(i+1, 1);
            }
        }

        // UTF-8 -> UTF-16
        int wlen = MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(), nullptr, 0);
        if (wlen <= 0) continue;

        std::wstring wcn(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(), &wcn[0], wlen);

        g_dict[enStr] = wcn;
        g_dictCount++;
    }

    return true;
}

// ===================== 内存管理 =====================
// 用 VirtualAlloc 分配永久内存, 避免跨 CRT 堆问题
// 分配的内存结构: [0xFF 0xFE] [wide string] [0x00 0x00]
// 返回指向 0xFF 位置的指针 (data 指针)
struct AllocatedStr {
    void* base;      // VirtualAlloc 基址
    void* data;      // data 指针 (base + 0 或 base + offset)
    int totalSize;
};

// 简单的内存池: 预分配一个大块, 按需切分
static constexpr int POOL_SIZE = 16 * 1024 * 1024; // 16MB 应该够用
static uint8_t* g_poolBase = nullptr;
static size_t g_poolOffset = 0;

static void InitPool() {
    g_poolBase = (uint8_t*)VirtualAlloc(nullptr, POOL_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (g_poolBase) {
        // 填零
        memset(g_poolBase, 0, POOL_SIZE);
    }
}

// 从池中分配, 返回 data 指针
// wide: 分配 2*len + 2 (null term), 不写 BOM (BOM 在 data-2 处, 由游戏逻辑管理)
// 我们只分配纯 UTF-16LE 数据 + null terminator
static void* PoolAllocWide(int wlen) {
    if (!g_poolBase) return nullptr;
    // 需要 wideBytes + 2 字节 null
    int wideBytes = wlen * 2;
    int need = wideBytes + 2;
    // 4 字节对齐
    need = (need + 3) & ~3;
    if (g_poolOffset + need > POOL_SIZE) {
        LogWrite("[ERROR] Pool exhausted! offset=%zu need=%d\n", g_poolOffset, need);
        return nullptr;
    }
    uint8_t* p = g_poolBase + g_poolOffset;
    g_poolOffset += need;
    return p;
}

// 从池中分配 StrObj 结构
static StrObj* PoolAllocStrObj() {
    if (!g_poolBase) return nullptr;
    int need = sizeof(StrObj);
    need = (need + 3) & ~3;
    if (g_poolOffset + need > POOL_SIZE) {
        LogWrite("[ERROR] Pool exhausted (StrObj)! offset=%zu need=%d\n", g_poolOffset, need);
        return nullptr;
    }
    StrObj* p = (StrObj*)(g_poolBase + g_poolOffset);
    g_poolOffset += need;
    return p;
}

// ===================== 构造 wide StrObj =====================
// 从 wstring 构造 wide StrObj, data 指向池分配的永久内存
// data 不含 BOM, 直接是 UTF-16LE + null terminator
// StrObj 本身也从池分配, 避免 static 被覆盖
static StrObj* MakeWideStrObj(const wchar_t* wstr, int wlen) {
    if (!wstr || wlen <= 0) return nullptr;

    int wideBytes = wlen * 2;

    // 分配数据区
    void* dataPtr = PoolAllocWide(wlen);
    if (!dataPtr) return nullptr;

    // 复制 wide 字符串数据
    memcpy(dataPtr, wstr, wideBytes);

    // 写 null terminator
    *((wchar_t*)((uint8_t*)dataPtr + wideBytes)) = 0;

    // 分配 StrObj 结构本身
    StrObj* obj = PoolAllocStrObj();
    if (!obj) return nullptr;

    obj->data = dataPtr;
    obj->meta = (uint32_t)wlen | 0x1000000;  // 长度 + wide 标志 (bit24)
    // byte7 = (meta >> 24) & 0xFF = 0x01 (wide flag)
    obj->extra = (uint32_t)wlen;

    return obj;
}

// ===================== 从 StrObj 提取英文文本 =====================
// 读取 StrObj 的文本, 返回 narrow UTF-8 字符串
static bool ExtractTextFromStrObj(StrObj* obj, std::string& out) {
    if (!obj) return false;

    bool isWide = (*(uint8_t*)((char*)obj + 7) & 1) != 0;
    int len = obj->meta & 0xFFFFFF;

    if (len <= 0 || !obj->data) return false;

    if (isWide) {
        // wide: data 直接指向 UTF-16LE 字符串数据 (无 BOM)
        const wchar_t* wstr = (const wchar_t*)obj->data;
        // 转换为 UTF-8
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wstr, len, nullptr, 0, nullptr, nullptr);
        if (utf8Len <= 0) return false;
        out.resize(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, wstr, len, &out[0], utf8Len, nullptr, nullptr);
        return true;
    } else {
        // narrow: data 直接指向字符串
        // 用 strlen 限制长度, 防止 meta 中的 len 不准
        int actualLen = len;
        // 也可以用 strnlen 确认
        out.assign((const char*)obj->data, actualLen);
        return true;
    }
}

// ===================== 查字典 =====================
static StrObj* LookupDict(StrObj* origObj, const char* hookName) {
    if (!origObj) return nullptr;

    // 如果原 StrObj 已经是 wide (中文), 直接 pass-through 不翻译
    // SMNU section 的文本本身就是中文 UTF-16LE, 不需要查字典
    bool isWide = (*(uint8_t*)((char*)origObj + 7) & 1) != 0;
    if (isWide) {
        return nullptr;  // pass-through, 让游戏使用原始 wide StrObj
    }

    std::string enText;
    if (!ExtractTextFromStrObj(origObj, enText)) {
        // Log extraction failures for first 20
        if (g_missCount < 20) {
            LogWrite("[%s] ExtractText FAILED: ", hookName);
            DumpStrObj("origObj", origObj);
            g_missCount++;
        }
        return nullptr;
    }

    g_lookupCount++;

    auto it = g_dict.find(enText);
    if (it == g_dict.end()) {
        // Log first 30 misses
        if (g_missCount < 30) {
            LogWrite("[%s] MISS(%d): \"%s\"\n", hookName, g_missCount, enText.substr(0, 80).c_str());
            g_missCount++;
        }
        return nullptr;
    }

    g_hitCount++;

    // 构造 wide StrObj
    StrObj* result = MakeWideStrObj(it->second.c_str(), (int)it->second.size());
    if (result) {
        // 日志前 50 条
        if (g_hitCount <= 50) {
            LogWrite("[%s] HIT %d: \"%s\" -> cn(wlen=%d)\n",
                hookName, g_hitCount,
                enText.substr(0, 80).c_str(),
                (int)it->second.size());
            DumpStrObj("translated", result);
        }
    } else {
        LogWrite("[%s] MakeWideStrObj FAILED for \"%s\"\n", hookName, enText.substr(0, 80).c_str());
    }
    return result;
}

// ===================== Hook 函数 =====================

// Hook sub_64D6B0: __stdcall(int a1) -> int (StrObj*)
// 接受 IDTXT_ key C 字符串, 返回文本对象指针
int __stdcall Hooked_64D6B0(int a1) {
    g_call64D6B0++;

    // Loop detection: same arg repeated >100 times
    if (a1 == g_lastArg64D6B0) {
        g_lastArg64D6B0_repeatCount++;
        if (g_lastArg64D6B0_repeatCount == 100) {
            LogWrite("[LOOP DETECTED] sub_64D6B0 called %d times with a1=0x%X!\n",
                g_lastArg64D6B0_repeatCount, a1);
        } else if (g_lastArg64D6B0_repeatCount > 100 &&
                   g_lastArg64D6B0_repeatCount % 1000 == 0) {
            LogWrite("[LOOP] sub_64D6B0 still looping: %d calls with a1=0x%X\n",
                g_lastArg64D6B0_repeatCount, a1);
        }
    } else {
        g_lastArg64D6B0 = a1;
        g_lastArg64D6B0_repeatCount = 0;
    }

    // 调原函数获取英文 StrObj
    int origResult = g_orig64D6B0(a1);

    if (!origResult) {
        // Log first 5 null returns
        if (g_call64D6B0 <= 5) {
            LogWrite("[64D6B0] call #%d a1=0x%X -> NULL\n", g_call64D6B0, a1);
        }
        return origResult;
    }

    StrObj* origObj = (StrObj*)origResult;

    // Log first 10 calls with full detail
    if (g_call64D6B0 <= 10) {
        LogWrite("[64D6B0] call #%d a1=0x%X\n", g_call64D6B0, a1);
        DumpStrObj("orig", origObj);
    }

    // 查字典
    StrObj* translated = LookupDict(origObj, "64D6B0");
    if (translated) {
        if (g_hitCount <= 10) {
            LogWrite("[64D6B0] returning translated\n");
        }
        return (int)translated;
    }

    return origResult;
}

// Hook sub_64D5F0: __thiscall(this, hash_key) -> int (StrObj*)
// XML 字典查找: 接收 hash 值, 返回 StrObj 指针
// 用 __fastcall 模拟 __thiscall (ecx=this, 第一个参数在栈上)
int __fastcall Hooked_64D5F0(int ecx_this, int edx_unused, int hash_key) {
    g_call64D5F0++;

    // 调原函数获取英文 StrObj
    int origResult = g_orig64D5F0(ecx_this, edx_unused, hash_key);

    if (!origResult) {
        if (g_call64D5F0 <= 5) {
            LogWrite("[64D5F0] call #%d hash=0x%X -> NULL\n", g_call64D5F0, hash_key);
        }
        return origResult;
    }

    StrObj* origObj = (StrObj*)origResult;

    if (g_call64D5F0 <= 10) {
        LogWrite("[64D5F0] call #%d hash=0x%X\n", g_call64D5F0, hash_key);
        DumpStrObj("orig", origObj);
    }

    // 查字典
    StrObj* translated = LookupDict(origObj, "64D5F0");
    if (translated) {
        if (g_hitCount <= 10) {
            LogWrite("[64D5F0] returning translated\n");
        }
        return (int)translated;
    }

    return origResult;
}

// Hook sub_508480: __cdecl(hash_key, out_str_obj) -> bool
// XML+CAM 文本检索: 先查 CAM STRT, 再查 XML 字典
// 调原函数后, 从 out 参数读取 StrObj, 查字典替换
int __cdecl Hooked_508480(int hash_key, int out_str_obj) {
    g_call508480++;

    // 调原函数
    int result = g_orig508480(hash_key, out_str_obj);

    if (!result || !out_str_obj) {
        if (g_call508480 <= 5) {
            LogWrite("[508480] call #%d hash=0x%X out=0x%X -> result=%d\n",
                g_call508480, hash_key, out_str_obj, result);
        }
        return result;
    }

    // out_str_obj 指向一个 StrObj (12 字节)
    StrObj* outObj = (StrObj*)out_str_obj;

    if (g_call508480 <= 10) {
        LogWrite("[508480] call #%d hash=0x%X\n", g_call508480, hash_key);
        DumpStrObj("out", outObj);
    }

    // 查字典 (如果 out 中是 narrow 英文)
    StrObj* translated = LookupDict(outObj, "508480");
    if (translated) {
        // 将翻译后的 wide StrObj 数据覆盖到 out 中
        // sub_628350 是 StrObj 的赋值函数, 但我们不能直接调它
        // 直接修改 out 的 data/meta/extra
        // 注意: out 中的 data 可能指向游戏堆内存, 需要先释放
        // 安全做法: 直接覆盖 out 的三个字段
        // 但需要确保 translated 的 data 在永久内存中 (VirtualAlloc 池)
        outObj->data = translated->data;
        outObj->meta = translated->meta;
        outObj->extra = translated->extra;

        if (g_hitCount <= 10) {
            LogWrite("[508480] replaced out with translated\n");
        }
    }

    return result;
}

// Hook sub_664660: __cdecl(filename, section_id, index) -> int (StrObj*)
int __cdecl Hooked_664660(int a1, int a2, int a3) {
    g_call664660++;

    // Loop detection on a3 (index)
    if (a3 == g_lastArg664660_a3) {
        g_lastArg664660_repeatCount++;
        if (g_lastArg664660_repeatCount == 100) {
            LogWrite("[LOOP DETECTED] sub_664660 called %d times with a3=0x%X!\n",
                g_lastArg664660_repeatCount, a3);
        } else if (g_lastArg664660_repeatCount > 100 &&
                   g_lastArg664660_repeatCount % 1000 == 0) {
            LogWrite("[LOOP] sub_664660 still looping: %d calls with a3=0x%X\n",
                g_lastArg664660_repeatCount, a3);
        }
    } else {
        g_lastArg664660_a3 = a3;
        g_lastArg664660_repeatCount = 0;
    }

    int origResult = g_orig664660(a1, a2, a3);

    if (!origResult) {
        if (g_call664660 <= 5) {
            LogWrite("[664660] call #%d a1=0x%X a2=0x%X a3=0x%X -> NULL\n",
                g_call664660, a1, a2, a3);
        }
        return origResult;
    }

    StrObj* origObj = (StrObj*)origResult;

    if (g_call664660 <= 10) {
        LogWrite("[664660] call #%d a1=0x%X a2=0x%X a3=0x%X\n",
            g_call664660, a1, a2, a3);
        DumpStrObj("orig", origObj);
    }

    StrObj* translated = LookupDict(origObj, "664660");
    if (translated) {
        if (g_hitCount <= 10) {
            LogWrite("[664660] returning translated\n");
        }
        return (int)translated;
    }

    return origResult;
}

// Hook sub_6646F0: __cdecl(filename, section_id, key) -> int (StrObj*)
int __cdecl Hooked_6646F0(int a1, int a2, int a3) {
    g_call6646F0++;

    // Log first 10 calls
    if (g_call6646F0 <= 10) {
        LogWrite("[6646F0] call #%d a1=0x%X a2=0x%X a3=0x%X\n",
            g_call6646F0, a1, a2, a3);
    }

    int origResult = g_orig6646F0(a1, a2, a3);

    if (!origResult) {
        if (g_call6646F0 <= 5) {
            LogWrite("[6646F0] call #%d -> NULL\n", g_call6646F0);
        }
        return origResult;
    }

    StrObj* origObj = (StrObj*)origResult;

    if (g_call6646F0 <= 10) {
        DumpStrObj("orig", origObj);
    }

    StrObj* translated = LookupDict(origObj, "6646F0");
    if (translated) {
        if (g_hitCount <= 10) {
            LogWrite("[6646F0] returning translated\n");
        }
        return (int)translated;
    }

    return origResult;
}

// ===================== 字节验证 =====================
static bool VerifyBytes(uintptr_t addr, const uint8_t* expected, int count, const char* name) {
    uint8_t* target = (uint8_t*)addr;
    for (int i = 0; i < count; i++) {
        if (target[i] != expected[i]) {
            LogWrite("[Hook] %s byte mismatch at offset %d: expected %02X got %02X\n",
                name, i, expected[i], target[i]);
            return false;
        }
    }
    return true;
}

// ===================== Hook 安装 =====================
static bool InstallHooks() {
    // 验证原始字节
    // sub_64D6B0: 应以 push ebp; mov ebp,esp 开头 (55 8B EC) 或类似
    // sub_664660: call sub_626AA0 (E8 xx xx xx xx)
    // sub_6646F0: call sub_626AA0 (E8 xx xx xx xx)

    uint8_t* p64D6B0 = (uint8_t*)ADDR_64D6B0;
    uint8_t* p664660 = (uint8_t*)ADDR_664660;
    uint8_t* p6646F0 = (uint8_t*)ADDR_6646F0;
    uint8_t* p64D5F0 = (uint8_t*)ADDR_64D5F0;
    uint8_t* p508480 = (uint8_t*)ADDR_508480;

    LogWrite("[Verify] sub_64D6B0 bytes: %02X %02X %02X %02X\n",
        p64D6B0[0], p64D6B0[1], p64D6B0[2], p64D6B0[3]);
    LogWrite("[Verify] sub_664660 bytes: %02X %02X %02X %02X\n",
        p664660[0], p664660[1], p664660[2], p664660[3]);
    LogWrite("[Verify] sub_6646F0 bytes: %02X %02X %02X %02X\n",
        p6646F0[0], p6646F0[1], p6646F0[2], p6646F0[3]);
    LogWrite("[Verify] sub_64D5F0 bytes: %02X %02X %02X %02X\n",
        p64D5F0[0], p64D5F0[1], p64D5F0[2], p64D5F0[3]);
    LogWrite("[Verify] sub_508480 bytes: %02X %02X %02X %02X\n",
        p508480[0], p508480[1], p508480[2], p508480[3]);

    // 初始化 MinHook
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        LogWrite("[Hook] MH_Initialize failed: %s\n", MH_StatusToString(status));
        return false;
    }

    // Hook sub_64D6B0
    status = MH_CreateHook((LPVOID)ADDR_64D6B0, (LPVOID)&Hooked_64D6B0, (LPVOID*)&g_orig64D6B0);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook(64D6B0) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook((LPVOID)ADDR_64D6B0);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook(64D6B0) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    LogWrite("[Hook] sub_64D6B0 hooked, trampoline=%p\n", g_orig64D6B0);

    // Hook sub_664660
    status = MH_CreateHook((LPVOID)ADDR_664660, (LPVOID)&Hooked_664660, (LPVOID*)&g_orig664660);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook(664660) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook((LPVOID)ADDR_664660);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook(664660) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    LogWrite("[Hook] sub_664660 hooked, trampoline=%p\n", g_orig664660);

    // Hook sub_6646F0
    status = MH_CreateHook((LPVOID)ADDR_6646F0, (LPVOID)&Hooked_6646F0, (LPVOID*)&g_orig6646F0);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook(6646F0) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook((LPVOID)ADDR_6646F0);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook(6646F0) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    LogWrite("[Hook] sub_6646F0 hooked, trampoline=%p\n", g_orig6646F0);

    // Hook sub_64D5F0 (XML dict lookup, __thiscall)
    // 用 __fastcall 模拟 __thiscall
    status = MH_CreateHook((LPVOID)ADDR_64D5F0, (LPVOID)&Hooked_64D5F0, (LPVOID*)&g_orig64D5F0);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook(64D5F0) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook((LPVOID)ADDR_64D5F0);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook(64D5F0) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    LogWrite("[Hook] sub_64D5F0 hooked, trampoline=%p\n", g_orig64D5F0);

    // Hook sub_508480 (XML+CAM text search, __cdecl)
    status = MH_CreateHook((LPVOID)ADDR_508480, (LPVOID)&Hooked_508480, (LPVOID*)&g_orig508480);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook(508480) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook((LPVOID)ADDR_508480);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook(508480) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    LogWrite("[Hook] sub_508480 hooked, trampoline=%p\n", g_orig508480);

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
            fprintf(g_logFile, "[MajestyHD Runtime Localization v5] DllMain ATTACH\n");
            fprintf(g_logFile, "  Exe path: %s\n", path);
            fprintf(g_logFile, "  Log path: %s\n", logPath);
            fprintf(g_logFile, "  Strategy: runtime dict lookup, no file modification\n");
            fprintf(g_logFile, "  Hooks: sub_64D6B0 (IDTXT), sub_664660 (STRT idx), sub_6646F0 (STRT key), sub_64D5F0 (XML dict), sub_508480 (XML+CAM search)\n\n");
            fflush(g_logFile);
        }

        // 初始化内存池
        InitPool();
        if (g_poolBase) {
            LogWrite("[Pool] VirtualAlloc %d bytes at %p\n", POOL_SIZE, g_poolBase);
        } else {
            LogWrite("[ERROR] VirtualAlloc failed!\n");
        }

        // 加载字典
        // 字典路径: DLL 同目录下的 dict.txt
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
            LogWrite("\n[Init] All hooks installed successfully\n");
        } else {
            LogWrite("\n[ERROR] Hook installation failed\n");
        }

        fflush(g_logFile);

    } else if (dwReason == DLL_PROCESS_DETACH) {
        if (g_logFile) {
            fprintf(g_logFile, "\n[DllMain] DETACH\n");
            fprintf(g_logFile, "  Call counts: 64D6B0=%d, 664660=%d, 6646F0=%d, 64D5F0=%d, 508480=%d\n",
                g_call64D6B0, g_call664660, g_call6646F0, g_call64D5F0, g_call508480);
            fprintf(g_logFile, "  Lookups: %d, Hits: %d, Misses: %d\n",
                g_lookupCount, g_hitCount, g_missCount);
            fclose(g_logFile);
            g_logFile = nullptr;
        }
    }
    return TRUE;
}
