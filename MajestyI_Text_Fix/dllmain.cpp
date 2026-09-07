// dllmain.cpp : Majesty HD Runtime Localization v10.1 (GDI on DirectDraw Surface)
//
// === v10.1 DirectDraw Surface GetDC 方案 ===
// v10.0 结果: GDI 画在窗口 DC 上被 DirectDraw Flip 覆盖 → 不可见
//
// v10.1 策略:
//   hook sub_66E7B0 (文本绘制函数), 命中中文时:
//   1. 跳过原函数 (不调用 g_orig66E7B0)
//   2. 从 this 对象读取布局信息 (位置/颜色/对齐)
//   3. 从 dword_7CA9EC (back buffer CYDDOffport) 获取 IDirectDrawSurface
//   4. 调用 IDirectDrawSurface::GetDC() 获取 DirectDraw surface 的 DC
//   5. 用 GDI ExtTextOutW 在 DirectDraw surface DC 上绘制中文
//   6. 调用 IDirectDrawSurface::ReleaseDC() 释放 DC
//   7. 非中文文本走原函数
//
// this 对象布局 (IDA 反编译确认):
//   this[0]/[4]/[8]  - StrObj 字符串数据 (inline/indirect)
//   this[2] (0x08)  - 字符串模式 (1=UTF-16, 其他=ANSI)
//   this[3] (0x0C)  - X 偏移 (加到 a2)
//   this[4] (0x10)  - Y 偏移 (加到 a3)
//   this[5] (0x14)  - 右边界 X (加到 a2)
//   this[6] (0x18)  - 底部 Y (加到 a3)
//   this[7] (0x1C)  - 渲染标志位 (bit0=居中, bit1=右对齐, bit2=描边, bit3=垂直居中, bit4=裁剪, bit5=右对齐2, bit6=颜色覆盖)
//   this[10](0x28)  - 字体对象 (CYFontImage)
//   this[12](0x30) - 描边颜色索引
//   this[13](0x34) - 前景颜色索引
//   this[20](0x50) - 颜色覆盖列表大小
//   this[21](0x54) - 颜色覆盖列表指针
//   this[23](0x5C) - 文本最大宽度
//   this[25](0x64) - 前景色 (调色板索引)
//   this[26](0x68) - 描边色 (调色板索引)
//   this+0x61      - 字体 ID
//   this[97]       - (同上, 字节偏移)
//
// DirectDraw surface 获取:
//   dword_7CA9E8 = primary surface CYDDOffport (this[33] = IDirectDrawSurface* offset 132)
//   dword_7CA9EC = back buffer CYDDOffport
//
// sub_66E7B0(this, a2, a3, a4) 签名:
//   this = UI 组件对象
//   a2 = X 偏移 (加到 this[3] 得到实际 X)
//   a3 = Y 偏移 (加到 this[6] 得到实际 Y)  
//   a4 = 渲染设备 (0=用默认 dword_7CA9E4)

#include "pch.h"
#include <psapi.h>
#include <intrin.h>
#include <ddraw.h>       // DirectDraw interfaces
#include <unordered_map>
#include <string>
#include <vector>
#include "MinHook.h"

#pragma comment(lib, "psapi.lib")
// 不链接 ddraw.lib: 只通过 vtable 指针调用 GetDC/ReleaseDC, 不需要导入符号
#pragma intrinsic(_ReturnAddress)

// ===================== 地址常量 =====================
static constexpr uintptr_t ADDR_66E7B0  = 0x0066E7B0;
static constexpr uintptr_t ADDR_7CA9E4   = 0x007CA9E4;  // dword_7CA9E4 (默认渲染设备)
static constexpr uintptr_t ADDR_7CA9BC  = 0x007CA9BC;  // dword_7CA9BC (调色板数组)
static constexpr uintptr_t ADDR_7CA9E8  = 0x007CA9E8;  // dword_7CA9E8 (primary surface CYDDOffport)
static constexpr uintptr_t ADDR_7CA9EC  = 0x007CA9EC;  // dword_7CA9EC (back buffer CYDDOffport)
static constexpr uintptr_t ADDR_7C89E8  = 0x007C89E8;  // dword_7C89E8 (primary IDirectDrawSurface*)
static constexpr uintptr_t ADDR_7C89EC  = 0x007C89EC;  // dword_7C89EC (back buffer IDirectDrawSurface*)
static constexpr uintptr_t ADDR_7C89D8  = 0x007C89D8;  // dword_7C89D8 (DirectDraw2 interface)
static constexpr uintptr_t ADDR_7C89E0  = 0x007C89E0;  // dword_7C89E0 (DirectDraw4 interface)
static constexpr uintptr_t ADDR_7C89DC  = 0x007C89DC;  // dword_7C89DC (DirectDraw7 interface)

// ===================== DirectDraw 函数指针类型 =====================
// IDirectDrawSurface vtable: GetDC=slot19, ReleaseDC=slot28
// (QueryInterface=0, AddRef=1, Release=2, AddAttachedSurface=3,
//  AddOverlayDirtyRect=4, Blt=5, BltBatch=6, BltFast=7,
//  DeleteAttachedSurface=8, EnumAttachedZBuffers=9, Flip=10,
//  GetAttachedSurface=11, GetBltStatus=12, GetCaps=13, GetClipper=14,
//  GetColorKey=15, GetDC=16, GetFlipStatus=17, GetOverlayPosition=18,
//  GetPalette=19, GetPixelFormat=20, GetSurfaceDesc=21, Initialize=22,
//  IsLost=23, Lock=24, ReleaseDC=25, Restore=26, SetClipper=27,
//  SetColorKey=28, SetOverlayPosition=29, SetPalette=30, Unlock=31,
//  UpdateOverlay=32, UpdateOverlayDisplay=33, UpdateOverlayZBuffer=34)
typedef HRESULT (__stdcall *DDSURFACE_GETDC)(void*, HDC*);
typedef HRESULT (__stdcall *DDSURFACE_RELEASEDC)(void*, HDC);

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
static int g_gdiDrawCount = 0;
static int g_gdiFailCount = 0;
static int g_surfaceFailCount = 0;

// ===================== surface 缓存 =====================
static void* g_cachedBackSurface = nullptr;
static int g_surfaceCacheMissCount = 0;
static int g_vtableLogCount = 0;

// ===================== IsBadReadPtr 替代: 安全读取 4 字节 =====================
static bool SafeRead32(uint32_t addr, uint32_t* out) {
    if (!addr) return false;
    __try {
        *out = *(uint32_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

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

    void* obj = *(void**)edi;
    bool wide = false;
    int chLen = 0;
    uint8_t* data = nullptr;

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

// ===================== GDI 字体管理 =====================
static HFONT g_cjkFont = nullptr;
static HFONT g_cjkFontBold = nullptr;
static int g_fontSize = 14;

static void InitGdiFonts() {
    if (g_cjkFont) return;
    
    g_cjkFont = CreateFontW(
        -g_fontSize,              // 高度 (负值=字符高度)
        0,                        // 宽度 (0=自动)
        0, 0,                     // 倾斜/方向
        FW_NORMAL,                // 粗细
        FALSE, FALSE, FALSE,      // 斜体/下划线/删除线
        DEFAULT_CHARSET,          // 字符集
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,      // 抗锯齿
        DEFAULT_PITCH | FF_DONTCARE,
        L"SimSun"
    );
    
    g_cjkFontBold = CreateFontW(
        -g_fontSize,
        0, 0, 0,
        FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"SimSun"
    );
    
    if (g_cjkFont) {
        LogWrite("[GDI] Font created: SimSun %dpt (handle=0x%p)\n", g_fontSize, g_cjkFont);
    } else {
        LogWrite("[GDI] ERROR: Failed to create font (err=%d)\n", GetLastError());
    }
}

// ===================== 调色板索引 → RGB 颜色 =====================
static COLORREF PalIndexToRgb(uint32_t palIndex) {
    if (palIndex == 0) return RGB(0, 0, 0);
    if (palIndex == 255) return RGB(255, 255, 255);
    if (palIndex == 254) return RGB(255, 255, 255);
    uint8_t gray = (uint8_t)(palIndex & 0xFF);
    return RGB(gray, gray, gray);
}

// ===================== 获取 DirectDraw surface =====================
// 策略: 从多个全局变量获取 surface 指针, 用 SEH 保护
static void* GetBackBufferSurface() {
    // 优先使用缓存
    if (g_cachedBackSurface) {
        uint32_t vt_val = 0;
        if (SafeRead32((uint32_t)g_cachedBackSurface, &vt_val) && vt_val) {
            return g_cachedBackSurface;
        }
        g_cachedBackSurface = nullptr;
    }

    // 按优先级尝试多个来源
    uint32_t candidates[] = {
        ADDR_7C89EC,   // 原始 back buffer surface
        ADDR_7C89E8,   // 原始 primary surface
    };

    uint32_t surface = 0;
    for (int i = 0; i < 2 && !surface; i++) {
        uint32_t addr = candidates[i];
        if (SafeRead32(addr, &surface) && surface) {
            // 验证 surface 的 vtable 指针
            uint32_t vt_val = 0;
            if (SafeRead32(surface, &vt_val) && vt_val) {
                if (g_vtableLogCount < 3) {
                    LogWrite("[Surface] Source 0x%X -> surface=0x%X vtable=0x%X\n", addr, surface, vt_val);
                    g_vtableLogCount++;
                }
            } else {
                surface = 0; // vtable 无效, 继续尝试
            }
        }
    }

    // 如果直接来源都失败, 尝试 CYDDOffport
    if (!surface) {
        uint32_t cyOff[] = { ADDR_7CA9EC, ADDR_7CA9E8 };
        for (int i = 0; i < 2 && !surface; i++) {
            uint32_t cyAddr = 0;
            if (SafeRead32(cyOff[i], &cyAddr) && cyAddr) {
                // CYDDOffport[33] (DWORD index 33 = byte offset 132)
                if (SafeRead32(cyAddr + 132, &surface) && surface) {
                    uint32_t vt_val = 0;
                    if (SafeRead32(surface, &vt_val) && vt_val) {
                        if (g_vtableLogCount < 3) {
                            LogWrite("[Surface] CYDDOffport[0x%X]+132 -> surface=0x%X vtable=0x%X\n", cyAddr, surface, vt_val);
                            g_vtableLogCount++;
                        }
                    } else {
                        surface = 0;
                    }
                }
            }
        }
    }

    if (!surface) {
        if (g_surfaceFailCount < 10) {
            uint32_t v1=0, v2=0, v3=0, v4=0;
            SafeRead32(ADDR_7C89EC, &v1);
            SafeRead32(ADDR_7CA9EC, &v2);
            SafeRead32(ADDR_7C89E8, &v3);
            SafeRead32(ADDR_7CA9E8, &v4);
            LogWrite("[Surface] All sources failed (7C89EC=0x%X 7CA9EC=0x%X 7C89E8=0x%X 7CA9E8=0x%X)\n", v1, v2, v3, v4);
        }
        g_surfaceFailCount++;
        return nullptr;
    }

    g_cachedBackSurface = (void*)surface;
    return g_cachedBackSurface;
}

// ===================== GDI 文本绘制 (DirectDraw surface) =====================
static bool GdiDrawText(int thisPtr, int a2, int a3, const wchar_t* wstr, int wlen) {
    if (!wstr || wlen <= 0) return false;

    // 获取 DirectDraw surface
    void* surface = GetBackBufferSurface();
    if (!surface) {
        g_gdiFailCount++;
        return false;
    }

    // IDirectDrawSurface vtable: GetDC=slot16, ReleaseDC=slot25
    // 用 SafeRead32 安全读取 vtable 指针和 slot
    uint32_t vt_addr = 0;
    if (!SafeRead32((uint32_t)surface, &vt_addr) || !vt_addr) {
        if (g_gdiFailCount < 10) {
            LogWrite("[GDI] Surface vtable unreadable (surface=0x%X)\n", (uint32_t)(uintptr_t)surface);
        }
        g_cachedBackSurface = nullptr;
        g_gdiFailCount++;
        return false;
    }
    
    uint32_t getDC_addr = 0, releaseDC_addr = 0;
    // vtable slot 16 = byte offset 64, slot 25 = byte offset 100
    if (!SafeRead32(vt_addr + 64, &getDC_addr) || !getDC_addr) {
        if (g_gdiFailCount < 10) {
            LogWrite("[GDI] vtable[16] (GetDC) unreadable at 0x%X\n", vt_addr);
        }
        g_cachedBackSurface = nullptr;
        g_gdiFailCount++;
        return false;
    }
    if (!SafeRead32(vt_addr + 100, &releaseDC_addr) || !releaseDC_addr) {
        if (g_gdiFailCount < 10) {
            LogWrite("[GDI] vtable[25] (ReleaseDC) unreadable at 0x%X\n", vt_addr);
        }
        g_cachedBackSurface = nullptr;
        g_gdiFailCount++;
        return false;
    }
    
    if (g_vtableLogCount <= 3) {
        LogWrite("[GDI] vtable=0x%X GetDC=0x%X ReleaseDC=0x%X\n", vt_addr, getDC_addr, releaseDC_addr);
    }
    
    DDSURFACE_GETDC pGetDC = (DDSURFACE_GETDC)getDC_addr;
    DDSURFACE_RELEASEDC pReleaseDC = (DDSURFACE_RELEASEDC)releaseDC_addr;

    // SEH 保护: DirectDraw surface 可能在调用时失效
    HDC hdc = nullptr;
    HRESULT hr = 0; // DD_OK
    __try {
        hr = pGetDC(surface, &hdc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_gdiFailCount < 10) {
            LogWrite("[GDI] GetDC SEH exception: 0x%08X\n", GetExceptionCode());
        }
        // 失效缓存, 下次重新获取
        g_cachedBackSurface = nullptr;
        g_gdiFailCount++;
        return false;
    }
    if (FAILED(hr) || !hdc) {
        if (g_gdiFailCount < 10) {
            LogWrite("[GDI] Surface GetDC failed: hr=0x%08X\n", (unsigned)hr);
        }
        g_gdiFailCount++;
        return false;
    }

    // 保存 DC 状态
    int savedState = SaveDC(hdc);

    // === 坐标系说明 ===
    // DirectDraw surface DC 的坐标系是 surface 像素坐标 (0,0 = 左上角)
    // 游戏的绘制坐标是客户区坐标, 应该与 surface 坐标一致 (全屏模式)
    // 但如果窗口模式, surface 坐标可能需要偏移

    // 从 this 对象读取位置信息
    uint8_t* base = (uint8_t*)thisPtr;
    uint32_t* dwordBase = (uint32_t*)base;

    // sub_66E7B0 中的位置计算:
    // v7 = this[3] + a2  -> 起始 X
    // v8 = this[5] + a2  -> 结束 X  
    // v10 = this[4] + a3 -> Y 上
    // v65 = this[6] + a3 -> Y 下
    int startX = (int)dwordBase[3] + a2;
    int endX   = (int)dwordBase[5] + a2;
    int startY = (int)dwordBase[4] + a3;
    int endY   = (int)dwordBase[6] + a3;

    // 渲染标志
    uint8_t flags = (uint8_t)dwordBase[7];
    bool centered  = (flags & 0x01) != 0;
    bool rightAlign = (flags & 0x02) != 0;
    bool outlined  = (flags & 0x04) != 0;
    bool vCentered = (flags & 0x08) != 0;

    // 颜色
    uint32_t fgColorIdx = dwordBase[25];  // 前景色 (调色板索引)
    uint32_t bgColorIdx = dwordBase[26];  // 描边色 (调色板索引)

    COLORREF fgColor = PalIndexToRgb(fgColorIdx);
    COLORREF bgColor = PalIndexToRgb(bgColorIdx);

    // 映射
    SetMapMode(hdc, MM_TEXT);
    SetWindowOrgEx(hdc, 0, 0, nullptr);
    SetViewportOrgEx(hdc, 0, 0, nullptr);

    // 选择字体
    HFONT oldFont = (HFONT)SelectObject(hdc, g_cjkFont);

    // 透明背景
    SetBkMode(hdc, TRANSPARENT);

    // 计算文本尺寸
    SIZE textSize = {0, 0};
    GetTextExtentPoint32W(hdc, wstr, wlen, &textSize);

    // 计算绘制位置
    int drawX = startX;
    int drawY = startY;

    // 水平对齐
    if (centered) {
        drawX += (endX - startX - textSize.cx) / 2;
    } else if (rightAlign) {
        drawX = endX - textSize.cx;
    }

    // 垂直对齐
    if (vCentered) {
        drawY += (endY - startY - textSize.cy) / 2;
    }

    // 矩形裁剪
    RECT clipRect;
    clipRect.left = startX;
    clipRect.top = startY;
    clipRect.right = endX;
    clipRect.bottom = endY;

    // 如果描边, 先画黑色描边
    if (outlined) {
        SetTextColor(hdc, bgColor);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                ExtTextOutW(hdc, drawX + dx, drawY + dy, ETO_CLIPPED, &clipRect, wstr, wlen, nullptr);
            }
        }
    }

    // 前景色绘制
    SetTextColor(hdc, fgColor);
    ExtTextOutW(hdc, drawX, drawY, ETO_CLIPPED, &clipRect, wstr, wlen, nullptr);

    // 恢复 DC 状态
    SelectObject(hdc, oldFont);
    RestoreDC(hdc, savedState);

    // 释放 DC (加 SEH 保护)
    __try {
        pReleaseDC(surface, hdc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_gdiFailCount < 10) {
            LogWrite("[GDI] ReleaseDC SEH exception: 0x%08X\n", GetExceptionCode());
        }
        g_cachedBackSurface = nullptr;
    }

    g_gdiDrawCount++;
    if (g_gdiDrawCount <= 20) {
        LogWrite("[GDI Draw %d] pos=(%d,%d) clip=[%d,%d,%d,%d] flags=0x%02X fg=%d bg=%d text='%.*ls'\n",
            g_gdiDrawCount, drawX, drawY,
            clipRect.left, clipRect.top, clipRect.right, clipRect.bottom,
            flags, fgColorIdx, bgColorIdx,
            (wlen < 30 ? wlen : 30), wstr);
    }
    
    return true;
}

// ===================== Hook sub_66E7B0 =====================
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

    // 用 GDI 在 DirectDraw surface 上绘制中文, 跳过原函数
    const wchar_t* wstr = (const wchar_t*)entry.cnUtf16LE.data();
    int wlen = entry.cnWcharCount;
    
    bool drawn = GdiDrawText(ecx_this, a2, a3, wstr, wlen);
    if (!drawn) {
        // GDI 绘制失败, 回退到原函数 (会显示空白, 但不会崩溃)
        if (g_gdiFailCount <= 5) {
            LogWrite("[GDI] Draw failed, falling back to original (will be blank)\n");
        }
        return g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);
    }

    g_replacedCount++;
    
    // 返回字符数 (原函数返回 v15 = 字符索引)
    return wlen;
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
            fprintf(g_logFile, "[MajestyHD Runtime Localization v10.1 DirectDraw Surface GetDC] DllMain ATTACH\n");
            fprintf(g_logFile, "  Exe path: %s\n", path);
            fprintf(g_logFile, "  Log path: %s\n", logPath);
            fprintf(g_logFile, "  Strategy: hook sub_66E7B0 + IDirectDrawSurface::GetDC + GDI ExtTextOutW\n\n");
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

        // 初始化 GDI 字体
        InitGdiFonts();

        if (InstallHooks()) {
            LogWrite("\n[Init] Hook installed successfully\n");
        } else {
            LogWrite("\n[ERROR] Hook installation failed\n");
        }

        fflush(g_logFile);

    } else if (dwReason == DLL_PROCESS_DETACH) {
        if (g_logFile) {
            fprintf(g_logFile, "\n[DllMain] DETACH (v10.1)\n");
            fprintf(g_logFile, "  Calls=%d Replaced=%d Hits=%d Misses=%d\n",
                g_callCount, g_replacedCount, g_hitCount, g_missCount);
            fprintf(g_logFile, "  GDI Draws=%d GDI Fails=%d Surface Fails=%d\n",
                g_gdiDrawCount, g_gdiFailCount, g_surfaceFailCount);
            fclose(g_logFile);
            g_logFile = nullptr;
        }
        if (g_cjkFont) { DeleteObject(g_cjkFont); g_cjkFont = nullptr; }
        if (g_cjkFontBold) { DeleteObject(g_cjkFontBold); g_cjkFontBold = nullptr; }
    }
    return TRUE;
}
