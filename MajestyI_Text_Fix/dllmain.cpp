// dllmain.cpp : Majesty HD Runtime Localization v11.0 (Direct Pixel Buffer Rendering)
//
// === v11.0 直接像素缓冲区渲染 ===
// v10.1 结果: DirectDraw surface GetDC 方案失败 — dword_7CA9E8 是 CYOffportIMP
//   而非 CYDDOffport, 没有 IDirectDrawSurface 指针
//
// v11.0 核心发现:
//   dword_7CA9E4 = CYOffportIMP 渲染设备对象 (sub_648C70 设置)
//   CYOffportIMP 布局 (sub_6477B0 初始化):
//     this[4]  (off 16) = 像素缓冲区基地址 (malloc 分配, 系统内存)
//     this[5]  (off 20) = width
//     this[6]  (off 24) = height
//     this[7]  (off 28) = BPP (bits per pixel)
//     this[8]  (off 32) = stride (bytes per row, DWORD 对齐)
//     this[14] (off 56) = 颜色格式索引
//     this[18] (off 72) = clip left
//     this[19] (off 76) = clip top
//     this[20] (off 80) = clip right
//     this[21] (off 84) = clip bottom
//   sub_6479F0 (vtable[46], offset 184): 返回像素指针 = this[4] + y*stride + (x*bpp)>>3
//   游戏 sub_634140 直接写像素缓冲区, 不用 Lock/Unlock
//
// v11.0 策略:
//   hook sub_66E7B0 (文本绘制函数), 命中中文时:
//   1. 跳过原函数
//   2. 从 dword_7CA9E4 或 a4 获取 CYOffportIMP 渲染设备
//   3. 读取像素缓冲区信息 (base/bpp/stride/clip)
//   4. 用 GetGlyphOutlineW 获取中文字形位图 (1bpp monochrome)
//   5. 手动 blit 字形到像素缓冲区 (支持 8/16/32bpp)
//   6. 处理对齐/裁剪/描边
//   7. 非中文文本走原函数
//
// this 对象布局 (sub_66E7B0 的 this = UI 组件):
//   this[0]/[4]/[8]  - StrObj 字符串数据
//   this[2] (0x08)  - 字符串模式 (1=UTF-16, 其他=ANSI)
//   this[3] (0x0C)  - X 偏移
//   this[4] (0x10)  - Y 偏移
//   this[5] (0x14)  - 右边界 X
//   this[6] (0x18)  - 底部 Y
//   this[7] (0x1C)  - 渲染标志 (bit0=居中, bit1=右对齐, bit2=描边, bit3=垂直居中)
//   this[25](0x64)  - 前景色
//   this[26](0x68)  - 描边色
//
// sub_66E7B0(this, a2, a3, a4) 签名:
//   this = UI 组件对象
//   a2 = X 偏移, a3 = Y 偏移
//   a4 = 渲染设备 (0=用默认 dword_7CA9E4)

#include "pch.h"
#include <psapi.h>
#include <intrin.h>
#include <unordered_map>
#include <string>
#include <vector>
#include "MinHook.h"

#pragma comment(lib, "psapi.lib")
#pragma intrinsic(_ReturnAddress)

// ===================== 地址常量 =====================
static constexpr uintptr_t ADDR_66E7B0  = 0x0066E7B0;
static constexpr uintptr_t ADDR_7CA9E4   = 0x007CA9E4;  // dword_7CA9E4 (默认渲染设备 CYOffportIMP)
static constexpr uintptr_t EXPECTED_VT   = 0x00741CAC;  // CYOffportIMP vtable

// CYOffportIMP 字段偏移
static constexpr uint32_t OFF_PIXELBUF  = 16;   // this[4]  像素缓冲区
static constexpr uint32_t OFF_WIDTH     = 20;   // this[5]  宽度
static constexpr uint32_t OFF_HEIGHT    = 24;   // this[6]  高度
static constexpr uint32_t OFF_BPP        = 28;   // this[7]  BPP
static constexpr uint32_t OFF_STRIDE     = 32;   // this[8]  stride
static constexpr uint32_t OFF_CLIP_LEFT  = 72;   // this[18] 裁剪左
static constexpr uint32_t OFF_CLIP_TOP   = 76;   // this[19] 裁剪上
static constexpr uint32_t OFF_CLIP_RIGHT = 80;   // this[20] 裁剪右
static constexpr uint32_t OFF_CLIP_BOT   = 84;   // this[21] 裁剪下

// ===================== 字符串对象 =====================
struct StrObj {
    void*    data;   // [0]
    uint32_t meta;   // [4]
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
static int g_blitCount = 0;
static int g_blitFailCount = 0;
static int g_devDumpCount = 0;

// ===================== IsBadReadPtr 替代 =====================
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
    int write = 0, read = 0;
    while (read < len) {
        unsigned char c = (unsigned char)data[read];
        if (read + 2 < len && c == 0xEF &&
            (unsigned char)data[read+1] == 0xBB && (unsigned char)data[read+2] == 0xBF) {
            read += 3; continue;
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
    int pos = 0, lineNum = 0;
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
        while (actualLen > 0 && (lineBuf[actualLen-1] == '\r' || lineBuf[actualLen-1] == '\n'))
            lineBuf[--actualLen] = '\0';
        if (actualLen == 0) continue;
        char* tab = (char*)memchr(lineBuf, '\t', actualLen);
        if (!tab) continue;
        int enLen = (int)(tab - (char*)lineBuf);
        if (enLen <= 0 || enLen >= 4096) continue;
        char enKey[4096];
        memcpy(enKey, lineBuf, enLen);
        enKey[enLen] = '\0';
        while (enLen > 0 && (enKey[enLen-1] == '\r' || enKey[enLen-1] == '\n'))
            enKey[--enLen] = '\0';
        if (enLen == 0) continue;
        char* cnStart = tab + 1;
        int cnLen = actualLen - enLen - 1;
        if (cnLen <= 0 || cnLen >= 4096) continue;
        while (cnLen > 0 && (cnStart[cnLen-1] == '\r' || cnStart[cnLen-1] == '\n'))
            cnStart[--cnLen] = '\0';
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

// ===================== GDI 字体 (用于 GetGlyphOutlineW) =====================
static HFONT g_cjkFont = nullptr;
static int g_fontSize = 14;

static void InitGdiFonts() {
    if (g_cjkFont) return;
    g_cjkFont = CreateFontW(
        -g_fontSize, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY,  // 关闭抗锯齿, 使用 1bpp monochrome
        DEFAULT_PITCH | FF_DONTCARE,
        L"SimSun"
    );
    if (g_cjkFont) {
        LogWrite("[Font] SimSun %dpt created (handle=0x%p)\n", g_fontSize, g_cjkFont);
    } else {
        LogWrite("[Font] ERROR: CreateFontW failed (err=%d)\n", GetLastError());
    }
}

// ===================== 渲染设备信息缓存 =====================
struct RenderDevInfo {
    uint32_t obj;       // CYOffportIMP 对象地址
    uint32_t pixelBuf;  // this[4] 像素缓冲区
    uint32_t width;     // this[5]
    uint32_t height;    // this[6]
    uint32_t bpp;       // this[7]
    uint32_t stride;    // this[8]
    uint32_t clipL;     // this[18]
    uint32_t clipT;     // this[19]
    uint32_t clipR;     // this[20]
    uint32_t clipB;     // this[21]
};

static bool GetRenderDevInfo(int a4, RenderDevInfo& info) {
    memset(&info, 0, sizeof(info));

    // 1. 获取渲染设备对象地址
    info.obj = 0;
    if (a4) {
        info.obj = (uint32_t)a4;
    } else {
        if (!SafeRead32(ADDR_7CA9E4, &info.obj) || !info.obj) {
            if (g_blitFailCount < 5)
                LogWrite("[Render] dword_7CA9E4 = 0 (no render device)\n");
            return false;
        }
    }

    // 2. 验证 vtable (可选, 用于诊断)
    uint32_t vt = 0;
    if (SafeRead32(info.obj, &vt)) {
        if (g_devDumpCount < 3) {
            LogWrite("[Render] obj=0x%X vtable=0x%X (expected=0x%X)\n",
                info.obj, vt, EXPECTED_VT);
            g_devDumpCount++;
        }
    } else {
        if (g_blitFailCount < 5)
            LogWrite("[Render] obj=0x%X vtable unreadable\n", info.obj);
        return false;
    }

    // 3. 读取像素缓冲区信息
    if (!SafeRead32(info.obj + OFF_PIXELBUF, &info.pixelBuf) || !info.pixelBuf) {
        if (g_blitFailCount < 5)
            LogWrite("[Render] pixelBuf = 0 (obj=0x%X)\n", info.obj);
        return false;
    }
    if (!SafeRead32(info.obj + OFF_WIDTH, &info.width) || !info.width) return false;
    if (!SafeRead32(info.obj + OFF_HEIGHT, &info.height) || !info.height) return false;
    if (!SafeRead32(info.obj + OFF_BPP, &info.bpp) || !info.bpp) return false;
    if (!SafeRead32(info.obj + OFF_STRIDE, &info.stride) || !info.stride) return false;

    // 4. 读取裁剪矩形
    SafeRead32(info.obj + OFF_CLIP_LEFT, &info.clipL);
    SafeRead32(info.obj + OFF_CLIP_TOP, &info.clipT);
    SafeRead32(info.obj + OFF_CLIP_RIGHT, &info.clipR);
    SafeRead32(info.obj + OFF_CLIP_BOT, &info.clipB);

    // 使用 surface 尺寸作为默认裁剪
    if (info.clipR == 0) info.clipR = info.width;
    if (info.clipB == 0) info.clipB = info.height;

    if (g_devDumpCount < 5) {
        LogWrite("[Render] pixelBuf=0x%X %ux%u bpp=%u stride=%u clip=[%u,%u,%u,%u]\n",
            info.pixelBuf, info.width, info.height, info.bpp, info.stride,
            info.clipL, info.clipT, info.clipR, info.clipB);
        g_devDumpCount++;
    }

    return true;
}

// ===================== 直接像素缓冲区渲染 =====================
static bool DirectBlitText(int thisPtr, int a2, int a3, int a4,
                           const wchar_t* wstr, int wlen) {
    if (!wstr || wlen <= 0) return false;

    // 1. 获取渲染设备信息
    RenderDevInfo rdi;
    if (!GetRenderDevInfo(a4, rdi)) {
        g_blitFailCount++;
        return false;
    }

    // 2. 读取文本布局
    uint32_t* dwordBase = (uint32_t*)thisPtr;
    int startX = (int)dwordBase[3] + a2;
    int endX   = (int)dwordBase[5] + a2;
    int startY = (int)dwordBase[4] + a3;
    int endY   = (int)dwordBase[6] + a3;
    uint8_t flags = (uint8_t)dwordBase[7];
    uint32_t fgColor = dwordBase[25];  // 前景色
    uint32_t bgColor = dwordBase[26];  // 描边色

    bool centered   = (flags & 0x01) != 0;
    bool rightAlign = (flags & 0x02) != 0;
    bool outlined   = (flags & 0x04) != 0;
    bool vCentered  = (flags & 0x08) != 0;

    // 3. 计算每像素字节数
    int bytesPerPixel = rdi.bpp / 8;
    if (bytesPerPixel < 1 || bytesPerPixel > 4) {
        if (g_blitFailCount < 5)
            LogWrite("[Blit] Unsupported BPP=%u\n", rdi.bpp);
        g_blitFailCount++;
        return false;
    }

    // 4. 创建内存 DC (用于 GetGlyphOutlineW)
    HDC mdc = CreateCompatibleDC(nullptr);
    if (!mdc) {
        g_blitFailCount++;
        return false;
    }
    HFONT oldFont = (HFONT)SelectObject(mdc, g_cjkFont);

    // 5. 测量文本总宽度
    int totalWidth = 0;
    int textHeight = g_fontSize;
    int maxLineWidth = 0;
    int curLineWidth = 0;
    for (int i = 0; i < wlen; i++) {
        if (wstr[i] == L'\n') {
            if (curLineWidth > maxLineWidth) maxLineWidth = curLineWidth;
            curLineWidth = 0;
            textHeight += g_fontSize;
            continue;
        }
        GLYPHMETRICS gm;
        MAT2 mat = {{0,1},{0,0},{0,0},{0,1}};
        DWORD ret = GetGlyphOutlineW(mdc, wstr[i], GGO_METRICS, &gm, 0, nullptr, &mat);
        if (ret != GDI_ERROR) {
            curLineWidth += gm.gmCellIncX;
        }
    }
    if (curLineWidth > maxLineWidth) maxLineWidth = curLineWidth;
    totalWidth = maxLineWidth;

    // 6. 计算绘制位置
    int drawX = startX;
    int drawY = startY;
    if (centered) {
        drawX = startX + (endX - startX - totalWidth) / 2;
    } else if (rightAlign) {
        drawX = endX - totalWidth;
    }
    if (vCentered) {
        drawY = startY + (endY - startY - textHeight) / 2;
    }

    // 7. 裁剪: 文本裁剪 + surface 裁剪
    int clipL = startX > (int)rdi.clipL ? startX : (int)rdi.clipL;
    int clipT = startY > (int)rdi.clipT ? startY : (int)rdi.clipT;
    int clipR = endX < (int)rdi.clipR ? endX : (int)rdi.clipR;
    int clipB = endY < (int)rdi.clipB ? endY : (int)rdi.clipB;
    if (clipR > (int)rdi.width) clipR = (int)rdi.width;
    if (clipB > (int)rdi.height) clipB = (int)rdi.height;

    if (g_blitCount < 5) {
        LogWrite("[Blit %d] pos=(%d,%d) clip=[%d,%d,%d,%d] surf=[%u,%u,%u,%u] flags=0x%02X fg=0x%X bg=0x%X bpp=%u\n",
            g_blitCount, drawX, drawY, clipL, clipT, clipR, clipB,
            rdi.clipL, rdi.clipT, rdi.clipR, rdi.clipB,
            flags, fgColor, bgColor, rdi.bpp);
    }

    // 8. 逐字符 blit
    int curX = drawX;
    int curY = drawY;

    for (int i = 0; i < wlen; i++) {
        if (wstr[i] == L'\n') {
            curX = drawX;
            curY += g_fontSize;
            continue;
        }
        if (wstr[i] == L'\t') {
            curX += g_fontSize * 4;
            continue;
        }

        // 获取字形位图
        GLYPHMETRICS gm;
        MAT2 mat = {{0,1},{0,0},{0,0},{0,1}};
        DWORD glyphSize = GetGlyphOutlineW(mdc, wstr[i], GGO_BITMAP, &gm, 0, nullptr, &mat);
        if (glyphSize == GDI_ERROR || glyphSize == 0) {
            // 无法获取字形, 用默认宽度前进
            GLYPHMETRICS gm2;
            DWORD ret = GetGlyphOutlineW(mdc, wstr[i], GGO_METRICS, &gm2, 0, nullptr, &mat);
            if (ret != GDI_ERROR) {
                curX += gm2.gmCellIncX;
            } else {
                curX += g_fontSize;  // fallback
            }
            continue;
        }

        std::vector<uint8_t> glyphBuf(glyphSize, 0);
        if (GetGlyphOutlineW(mdc, wstr[i], GGO_BITMAP, &gm, glyphSize, glyphBuf.data(), &mat) == GDI_ERROR) {
            curX += gm.gmCellIncX;
            continue;
        }

        int glyphW = gm.gmBlackBoxX;
        int glyphH = gm.gmBlackBoxY;
        int glyphStride = ((glyphW + 7) / 8 + 3) & ~3;  // DWORD 对齐
        int originX = gm.gmptGlyphOrigin.x;
        int originY = gm.gmptGlyphOrigin.y;

        // blit lambda: 把字形位图 blit 到像素缓冲区, 带偏移和颜色
        auto blitGlyph = [&](int offX, int offY, uint32_t color) {
            for (int py = 0; py < glyphH; py++) {
                int dstY = curY + originY + py + offY;
                if (dstY < clipT || dstY >= clipB) continue;
                if (dstY < 0 || dstY >= (int)rdi.height) continue;

                uint8_t* dstRow = (uint8_t*)rdi.pixelBuf + dstY * rdi.stride;
                uint8_t* srcRow = glyphBuf.data() + py * glyphStride;

                for (int px = 0; px < glyphW; px++) {
                    // 检查位图位
                    if (!(srcRow[px >> 3] & (0x80 >> (px & 7)))) continue;

                    int dstX = curX + originX + px + offX;
                    if (dstX < clipL || dstX >= clipR) continue;
                    if (dstX < 0 || dstX >= (int)rdi.width) continue;

                    // SEH 保护写入
                    __try {
                        switch (bytesPerPixel) {
                            case 1:
                                dstRow[dstX] = (uint8_t)color;
                                break;
                            case 2:
                                *(uint16_t*)(dstRow + dstX * 2) = (uint16_t)color;
                                break;
                            case 3:
                                dstRow[dstX * 3]     = (uint8_t)(color);
                                dstRow[dstX * 3 + 1] = (uint8_t)(color >> 8);
                                dstRow[dstX * 3 + 2] = (uint8_t)(color >> 16);
                                break;
                            case 4:
                                *(uint32_t*)(dstRow + dstX * 4) = color;
                                break;
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        // 写入失败, 跳过这个像素
                    }
                }
            }
        };

        // 描边 (先画, 被前景色覆盖)
        if (outlined) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    blitGlyph(dx, dy, bgColor);
                }
            }
        }

        // 前景色
        blitGlyph(0, 0, fgColor);

        // 前进
        curX += gm.gmCellIncX;
    }

    // 清理
    SelectObject(mdc, oldFont);
    DeleteDC(mdc);

    g_blitCount++;
    if (g_blitCount <= 20) {
        LogWrite("[Blit %d] done: text='%.*ls' pos=(%d,%d) chars=%d\n",
            g_blitCount, (wlen < 40 ? wlen : 40), wstr, drawX, drawY, wlen);
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

    // 直接像素缓冲区渲染, 跳过原函数
    const wchar_t* wstr = (const wchar_t*)entry.cnUtf16LE.data();
    int wlen = entry.cnWcharCount;

    bool drawn = DirectBlitText(ecx_this, a2, a3, a4, wstr, wlen);
    if (!drawn) {
        if (g_blitFailCount <= 5) {
            LogWrite("[Blit] Failed, falling back to original (will be blank)\n");
        }
        return g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);
    }

    g_replacedCount++;
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
        if (p) strcpy(p + 1, "MajestyI_TextFix.log");
        else strcpy(logPath, "MajestyI_TextFix.log");
        g_logFile = fopen(logPath, "w");
        if (g_logFile) {
            fprintf(g_logFile, "[MajestyHD Runtime Localization v11.0 Direct Pixel Buffer] DllMain ATTACH\n");
            fprintf(g_logFile, "  Exe path: %s\n", path);
            fprintf(g_logFile, "  Log path: %s\n", logPath);
            fprintf(g_logFile, "  Strategy: hook sub_66E7B0 + CYOffportIMP pixel buffer + GetGlyphOutlineW\n\n");
            fflush(g_logFile);
        }
        char dictPath[MAX_PATH];
        GetModuleFileNameA(hModule, dictPath, MAX_PATH);
        char* p2 = strrchr(dictPath, '\\');
        if (p2) strcpy(p2 + 1, "dict.txt");
        else strcpy(dictPath, "dict.txt");
        if (LoadDict(dictPath)) {
            LogWrite("[Dict] Loaded %d entries from %s\n", g_dictCount, dictPath);
        } else {
            LogWrite("[ERROR] Failed to load dict from %s\n", dictPath);
        }
        InitGdiFonts();
        if (InstallHooks()) {
            LogWrite("\n[Init] Hook installed successfully\n");
        } else {
            LogWrite("\n[ERROR] Hook installation failed\n");
        }
        fflush(g_logFile);
    } else if (dwReason == DLL_PROCESS_DETACH) {
        if (g_logFile) {
            fprintf(g_logFile, "\n[DllMain] DETACH (v11.0)\n");
            fprintf(g_logFile, "  Calls=%d Replaced=%d Hits=%d Misses=%d\n",
                g_callCount, g_replacedCount, g_hitCount, g_missCount);
            fprintf(g_logFile, "  Blits=%d BlitFails=%d\n",
                g_blitCount, g_blitFailCount);
            fclose(g_logFile);
            g_logFile = nullptr;
        }
        if (g_cjkFont) { DeleteObject(g_cjkFont); g_cjkFont = nullptr; }
    }
    return TRUE;
}
