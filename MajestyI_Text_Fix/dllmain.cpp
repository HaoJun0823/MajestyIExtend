// dllmain.cpp : Majesty HD Runtime Localization v16.0 (Motion Sampling)
//
// v14.1 诊断定案: 全屏脏矩形思路走不通。
//   日志铁证: submitZero=94%(提交几乎全被合并丢弃) + compose 105/s 但 DirectBlit
//   的字不在引擎重绘源里 → 让引擎"全屏重绘来擦旧字"在原理上不可能成功。
//   用户截图: 文字随单位/相机移动时, 旧位置像素残留 = 拖影; 等距栅格下呈斜向。
//
// v15.0 方案 C(治标证实思路): DirectBlit 内部闭环擦除, 完全绕开引擎。
//   画字前: 将 clip 区域背景 memcpy 缓存到堆(key=精确位置)
//           → 若同位置画过(上一帧), 先把缓存背景写回 pixelBuf(擦掉旧字)
//           → 再把当前(已擦)背景重新缓存, 然后才画新字
//   移走的位置: LRU 过期(约 2 帧未更新)后, 在后续 DirectBlit 时把缓存背景写回
//            → 补擦残留拖影
//   实测结果(v15.0 DETACH): hits=11768(同位置擦除大量执行) new=447(位置变化极少)
//   + 用户观察"单位移动标签留原地"+"动过才消失(静态帧缓存)"
//   → 推论: 拖影产生于引擎不调用文字绘制的期间(滚动/增量刷新移动了 DirectBlit
//     像素), v15 的擦除钩子(DirectBlit 内)在整个拖影产生期间从未被触发。
//
// v16.0 本版: 采集 Motion 数据验证上述推论 —— 全量记录每次 DirectBlit 的
//   (文本, clip 位置), 输出 scripts/motion.log。
//   用户跑 60 秒(含相机平移 + 单位移动), 分析:
//     (a) 平移/移动期间 DirectBlit 是否被调用? 位置连续 or 突变/静止?
//     (b) 同一文本的位置变化模式 → 决定修复 hook 点(帧末重画 / 滚动补偿 /
//         引擎文字重画时机)。
//   保留 v15.0 擦除代码(BG-LRU)作为对照, 行为不变。

#include "pch.h"
#include <psapi.h>
#include <intrin.h>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <algorithm>
#include "MinHook.h"

#pragma comment(lib, "psapi.lib")
#pragma intrinsic(_ReturnAddress)

#ifndef GGO_GRAY8
#define GGO_GRAY8 0x0005
#endif

// ===================== 地址常量 =====================
static constexpr uintptr_t ADDR_66E7B0  = 0x0066E7B0;
static constexpr uintptr_t ADDR_7CA9E4  = 0x007CA9E4;
static constexpr uintptr_t EXPECTED_VT  = 0x00741CAC;

// 脏矩形系统地址
static constexpr uintptr_t ADDR_DIRTY_MGR    = 0x007C12FC;  // dword_7C12FC: 脏矩形管理器
static constexpr uintptr_t ADDR_LAYER_IDX    = 0x007C5228;  // dword_7C5228: 当前图层索引
static constexpr uintptr_t ADDR_SUBMIT_DIRTY = 0x00673FB0;  // sub_673FB0: 提交脏矩形(带可见性检查+坐标转换)

// CYOffportIMP 字段偏移
static constexpr uint32_t OFF_PIXELBUF  = 16;
static constexpr uint32_t OFF_WIDTH     = 20;
static constexpr uint32_t OFF_HEIGHT    = 24;
static constexpr uint32_t OFF_BPP       = 28;
static constexpr uint32_t OFF_STRIDE    = 32;
static constexpr uint32_t OFF_CLIP_LEFT  = 72;
static constexpr uint32_t OFF_CLIP_TOP   = 76;
static constexpr uint32_t OFF_CLIP_RIGHT = 80;
static constexpr uint32_t OFF_CLIP_BOT   = 84;

// ===================== INI 配置 =====================
struct Config {
    // 字体
    char     fontFile[260];   // TTF 文件名 (同目录), 空=用系统字体
    char     fontName[128];   // 字体名称 (如 SimSun, Source Han Sans)
    int      fontSize;         // 字体大小 (像素)
    int      fontWeight;      // FW_NORMAL=400, FW_BOLD=700

    // 渲染模式
    int      renderMode;       // 0=GGO_BITMAP (1bpp), 1=GGO_GRAY8 (抗锯齿)
    int      blendMode;        // 0=直接写入, 1=alpha混合
    int      quality;           // 0=NONANTIALIASED, 1=ANTIALIASED, 2=CLEARTYPE

    // 颜色 (RGB565 格式, 16bpp)
    uint32_t fgColor;          // 前景色 (0=自动: 16bpp=0xFFFF)
    uint32_t bgColor;          // 描边色
    bool     overrideColor;    // true=忽略游戏颜色用配置值

    // 位置微调
    int      yOffset;          // Y 偏移微调 (像素)
    int      xOffset;          // X 偏移微调 (像素)
    int      lineSpacing;      // 额外行间距 (像素, 加在 tmHeight 上)
    int      wrapWidth;        // 自动换行宽度 (0=自动用 endX-startX, >0=固定像素宽度)
    int      wrapWidthAdjust;  // auto 模式下对自动宽度的微调 (+/- 像素)

    // 描边
    bool     enableOutline;    // 强制开启描边
    int      outlineWidth;     // 描边宽度 (像素)
};
static Config g_cfg;

// ===================== 字符串对象 =====================
struct StrObj {
    void*    data;
    uint32_t meta;
    uint32_t extra;
};

// ===================== 字典 =====================
struct DictEntry {
    std::vector<uint8_t> cnUtf16LE;
    int cnWcharCount;
};
static std::unordered_map<std::string, DictEntry> g_dict;
static int g_dictCount = 0;

// ===================== Rollback 词汇表 =====================
// 存储 英文片段→中文片段 的替换对，用于 MISS 后的子串替换
static std::vector<std::pair<std::string, std::string>> g_rollback;
static int g_rollbackCount = 0;

// ===================== 小写转换辅助 =====================
static std::string AsciiToLower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) { if (c >= 'A' && c <= 'Z') c += 32; }
    return r;
}

// ===================== 原始函数指针 =====================
typedef int (__fastcall *OrigDraw_t)(int ecx_this, int edx_unused, int a2, int a3, int a4);
static OrigDraw_t g_orig66E7B0 = nullptr;

// ===================== Debug 日志 =====================
static FILE* g_logFile = nullptr;
static FILE* g_missLogFile = nullptr;
static FILE* g_hitLogFile = nullptr;
static FILE* g_rollbackLogFile = nullptr;
static int g_callCount = 0;
static int g_replacedCount = 0;
static int g_hitCount = 0;
static int g_missCount = 0;
static int g_rollbackHitCount = 0;
static int g_blitCount = 0;
static int g_blitFailCount = 0;
static int g_devDumpCount = 0;

// MISS/HIT/Rollback 去重
static std::unordered_set<std::string> g_missSeen;
static std::unordered_set<std::string> g_hitSeen;
static std::unordered_set<std::string> g_rollbackSeen;

// ===================== SafeRead =====================
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

// ===================== v16.0: Motion 采样 =====================
// 目的: 采集 DirectBlitText 每次调用的 (文本, clip 位置) —— 判断拖影文字
//       在相机平移/单位移动时是否被引擎重画(位置是否连续变化)。
//   若平移期间 DirectBlit 几乎不被调用 / 位置突变 → 拖影 = 引擎滚动静态像素,
//       DirectBlit 内部擦除(v15)永远没有触发机会 → 需换 hook 点。
//   若平移期间位置连续平滑变化 → DirectBlit 在跟随画字, v15 擦除应有效但没效
//       → 需查 v15 内部缺陷(WriteBack 目标/时序)。
// 文件: scripts/motion.log (与主日志同目录)。全量记录, 512 行 flush 一次。
static FILE* g_motionFile = nullptr;
static int   g_motionLines = 0;

static void MotionLog(int thisPtr, int blitNo, const wchar_t* wstr, int wlen,
                      int l, int t, int r, int b, uint32_t fg, uint8_t flags) {
    if (!g_motionFile || !wstr || wlen <= 0) return;
    // 文本摘要: 前 10 个码元 → UTF-8 (防超长文本刷爆日志)
    char txt[64] = {0};
    int take = (wlen < 10) ? wlen : 10;
    WideCharToMultiByte(CP_UTF8, 0, wstr, take, txt, sizeof(txt) - 1, nullptr, nullptr);
    // 控制字符转义, 防止换行破坏行结构
    for (char* p = txt; *p; p++) {
        if ((uint8_t)*p < 0x20) *p = '?';
    }
    fprintf(g_motionFile, "[M] b=%d this=0x%X t='%s' c=(%d,%d,%d,%d) f=0x%X fl=0x%02X\n",
        blitNo, thisPtr, txt, l, t, r, b, fg, flags);
    if ((++g_motionLines & 511) == 0) fflush(g_motionFile);
}

// HIT 日志（去重，输出到 hit.log，含英文→中文映射）
static void LogHit(const char* enText, const char* cnUtf8, int wchars) {
    g_hitCount++;
    if (!g_hitLogFile) return;
    if (g_hitSeen.count(enText)) return;
    g_hitSeen.insert(enText);
    fprintf(g_hitLogFile, "[HIT] \"%s\" -> \"%s\" (wchars=%d)\n", enText, cnUtf8, wchars);
    fflush(g_hitLogFile);
}

// Rollback 日志（去重，输出到 rollback.log，含原文→替换后文本）
static void LogRollback(const char* enText, const char* replacedUtf8, int wchars) {
    g_rollbackHitCount++;
    if (!g_rollbackLogFile) return;
    if (g_rollbackSeen.count(enText)) return;
    g_rollbackSeen.insert(enText);
    fprintf(g_rollbackLogFile, "[ROLLBACK] \"%s\" -> \"%s\" (wchars=%d)\n", enText, replacedUtf8, wchars);
    fflush(g_rollbackLogFile);
}

// MISS 日志（去重，输出到 miss.log）— 仅在 rollback 也未命中时调用
static void LogMiss(const char* enText, bool wide, int chLen) {
    g_missCount++;
    if (!g_missLogFile) return;
    if (g_missSeen.count(enText)) return;
    g_missSeen.insert(enText);
    fprintf(g_missLogFile, "[MISS] \"%s\" (wide=%d chLen=%d)\n", enText, (int)wide, chLen);
    fflush(g_missLogFile);
}

// ===================== INI 读取 =====================
static std::string GetDllDir() {
    char path[MAX_PATH];
    HMODULE hMod;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&g_logFile, &hMod);
    GetModuleFileNameA(hMod, path, MAX_PATH);
    char* p = strrchr(path, '\\');
    if (p) *(p + 1) = '\0';
    return std::string(path);
}

static void LoadConfig(const char* iniPath) {
    // 默认值
    memset(&g_cfg, 0, sizeof(g_cfg));
    strncpy_s(g_cfg.fontFile, "", 0);
    strncpy_s(g_cfg.fontName, "SimSun", 6);
    g_cfg.fontSize = 12;
    g_cfg.fontWeight = FW_NORMAL;
    g_cfg.renderMode = 0;       // GGO_BITMAP
    g_cfg.blendMode = 0;        // 直接写入
    g_cfg.quality = 0;          // NONANTIALIASED
    g_cfg.fgColor = 0xFFFF;     // 白色 RGB565
    g_cfg.bgColor = 0x0000;     // 黑色
    g_cfg.overrideColor = true;
    g_cfg.yOffset = 0;
    g_cfg.xOffset = 0;
    g_cfg.lineSpacing = 0;
    g_cfg.wrapWidth = 0;
    g_cfg.wrapWidthAdjust = 0;
    g_cfg.enableOutline = false;
    g_cfg.outlineWidth = 1;

    char buf[260];

    // [Font]
    GetPrivateProfileStringA("Font", "FontFile", "", buf, 260, iniPath);
    if (buf[0]) strncpy_s(g_cfg.fontFile, buf, 259);

    GetPrivateProfileStringA("Font", "FontName", "SimSun", buf, 128, iniPath);
    strncpy_s(g_cfg.fontName, buf, 127);

    g_cfg.fontSize = GetPrivateProfileIntA("Font", "FontSize", 12, iniPath);
    g_cfg.fontWeight = GetPrivateProfileIntA("Font", "FontWeight", FW_NORMAL, iniPath);

    // [Render]
    g_cfg.renderMode = GetPrivateProfileIntA("Render", "RenderMode", 0, iniPath);
    g_cfg.blendMode = GetPrivateProfileIntA("Render", "BlendMode", 0, iniPath);
    g_cfg.quality = GetPrivateProfileIntA("Render", "Quality", 0, iniPath);

    // [Color]
    g_cfg.fgColor = (uint32_t)GetPrivateProfileIntA("Color", "FgColor", 0xFFFF, iniPath);
    g_cfg.bgColor = (uint32_t)GetPrivateProfileIntA("Color", "BgColor", 0x0000, iniPath);
    g_cfg.overrideColor = GetPrivateProfileIntA("Color", "OverrideColor", 1, iniPath) != 0;

    // [Position]
    g_cfg.yOffset = GetPrivateProfileIntA("Position", "YOffset", 0, iniPath);
    g_cfg.xOffset = GetPrivateProfileIntA("Position", "XOffset", 0, iniPath);
    g_cfg.lineSpacing = GetPrivateProfileIntA("Position", "LineSpacing", 0, iniPath);
    g_cfg.wrapWidth = GetPrivateProfileIntA("Position", "WrapWidth", 0, iniPath);
    g_cfg.wrapWidthAdjust = GetPrivateProfileIntA("Position", "WrapWidthAdjust", 0, iniPath);

    // [Outline]
    g_cfg.enableOutline = GetPrivateProfileIntA("Outline", "Enable", 0, iniPath) != 0;
    g_cfg.outlineWidth = GetPrivateProfileIntA("Outline", "Width", 1, iniPath);

    LogWrite("[Config] FontFile=%s FontName=%s FontSize=%d FontWeight=%d\n",
        g_cfg.fontFile[0] ? g_cfg.fontFile : "(none)", g_cfg.fontName, g_cfg.fontSize, g_cfg.fontWeight);
    LogWrite("[Config] RenderMode=%d BlendMode=%d Quality=%d\n",
        g_cfg.renderMode, g_cfg.blendMode, g_cfg.quality);
    LogWrite("[Config] FgColor=0x%X BgColor=0x%X Override=%d\n",
        g_cfg.fgColor, g_cfg.bgColor, (int)g_cfg.overrideColor);
    LogWrite("[Config] YOffset=%d XOffset=%d LineSpacing=%d WrapWidth=%d WrapWidthAdjust=%d\n",
        g_cfg.yOffset, g_cfg.xOffset, g_cfg.lineSpacing, g_cfg.wrapWidth, g_cfg.wrapWidthAdjust);
    LogWrite("[Config] Outline=%d OutlineWidth=%d\n",
        (int)g_cfg.enableOutline, g_cfg.outlineWidth);
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

// ---- JSON 解析器 (极简手写，只处理 {"key":"value",...} 结构) ----
// JSON 字符串转义解码
static int JsonDecodeString(const char* src, int srcLen, char* dst, int dstMax) {
    int w = 0;
    for (int i = 0; i < srcLen && w < dstMax - 1; i++) {
        if (src[i] == '\\' && i + 1 < srcLen) {
            char c = src[i+1];
            switch (c) {
                case 'n': dst[w++] = '\n'; i++; break;
                case 't': dst[w++] = '\t'; i++; break;
                case 'r': dst[w++] = '\r'; i++; break;
                case '"': dst[w++] = '"'; i++; break;
                case '\\': dst[w++] = '\\'; i++; break;
                case '/': dst[w++] = '/'; i++; break;
                case 'b': dst[w++] = '\b'; i++; break;
                case 'f': dst[w++] = '\f'; i++; break;
                case 'u': {
                    if (i + 5 < srcLen) {
                        char hex[5] = { src[i+2], src[i+3], src[i+4], src[i+5], 0 };
                        unsigned cp = (unsigned)strtoul(hex, nullptr, 16);
                        if (cp < 0x80) {
                            dst[w++] = (char)cp;
                        } else if (cp < 0x800) {
                            dst[w++] = (char)(0xC0 | (cp >> 6));
                            dst[w++] = (char)(0x80 | (cp & 0x3F));
                        } else {
                            dst[w++] = (char)(0xE0 | (cp >> 12));
                            dst[w++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            dst[w++] = (char)(0x80 | (cp & 0x3F));
                        }
                        i += 5;
                    }
                    break;
                }
                default: dst[w++] = c; i++; break;
            }
        } else {
            dst[w++] = src[i];
        }
    }
    dst[w] = '\0';
    return w;
}

// 跳过空白
static int SkipWhitespace(const char* s, int pos, int len) {
    while (pos < len) {
        char c = s[pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        pos++;
    }
    return pos;
}

// 解析 JSON 字符串 (从 pos 开始的 "..."), 返回字符串结束后的位置
static int ParseJsonString(const char* s, int pos, int len, char* out, int outMax) {
    if (pos >= len || s[pos] != '"') return -1;
    pos++; // skip opening quote
    int start = pos;
    // find closing quote (handle escapes)
    while (pos < len) {
        if (s[pos] == '\\' && pos + 1 < len) { pos += 2; continue; }
        if (s[pos] == '"') break;
        pos++;
    }
    if (pos >= len) return -1;
    int strLen = pos - start;
    JsonDecodeString(s + start, strLen, out, outMax);
    pos++; // skip closing quote
    return pos;
}

// ===================== [newline] -> \n 替换 =====================
// dict/rollback 的 value 中 [newline] 是字面量（9字符），需替换为实际换行符 \n
static void ReplaceNewlineLiteral(std::string& s) {
    const char* tag = "[newline]";
    size_t tagLen = 9;
    size_t pos = 0;
    while ((pos = s.find(tag, pos)) != std::string::npos) {
        s.replace(pos, tagLen, "\n");
        pos += 1; // \n 只有1字节，避免重复匹配
    }
}

static bool LoadDictJson(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize <= 0) { fclose(f); return false; }
    if (fileSize > 16 * 1024 * 1024) {
        LogWrite("[Dict] JSON file too large: %ld bytes\n", fileSize);
        fclose(f); return false;
    }
    std::vector<char> buf(fileSize + 1, 0);
    fread(buf.data(), 1, fileSize, f);
    fclose(f);
    int len = (int)fileSize;
    LogWrite("[Dict] JSON file size: %ld bytes\n", fileSize);

    int pos = SkipWhitespace(buf.data(), 0, len);
    if (pos >= len || buf[pos] != '{') {
        LogWrite("[Dict] JSON: expected '{' at pos %d\n", pos);
        return false;
    }
    pos++; // skip '{'
    pos = SkipWhitespace(buf.data(), pos, len);

    char enKey[8192];
    char cnValue[8192];

    while (pos < len) {
        // expect string key or '}'
        pos = SkipWhitespace(buf.data(), pos, len);
        if (pos >= len) break;
        if (buf[pos] == '}') { pos++; break; }
        if (buf[pos] == ',') { pos++; continue; }

        // parse key
        int newPos = ParseJsonString(buf.data(), pos, len, enKey, sizeof(enKey));
        if (newPos < 0) {
            LogWrite("[Dict] JSON: parse key failed at pos %d (char 0x%02X)\n", pos, (unsigned char)buf[pos]);
            break;
        }
        pos = newPos;

        // skip whitespace + colon
        pos = SkipWhitespace(buf.data(), pos, len);
        if (pos >= len || buf[pos] != ':') {
            LogWrite("[Dict] JSON: expected ':' at pos %d\n", pos);
            break;
        }
        pos++; // skip ':'
        pos = SkipWhitespace(buf.data(), pos, len);

        // parse value
        newPos = ParseJsonString(buf.data(), pos, len, cnValue, sizeof(cnValue));
        if (newPos < 0) {
            LogWrite("[Dict] JSON: parse value failed for key '%.80s' at pos %d\n", enKey, pos);
            break;
        }
        pos = newPos;

        // store entry
        std::string enStr(enKey);
        std::string cnStr(cnValue);
        ReplaceNewlineLiteral(cnStr);
        int wlen = MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(), nullptr, 0);
        if (wlen > 0) {
            DictEntry entry;
            entry.cnWcharCount = wlen;
            entry.cnUtf16LE.resize((wlen + 1) * 2);
            MultiByteToWideChar(CP_UTF8, 0, cnStr.c_str(), (int)cnStr.size(),
                (wchar_t*)entry.cnUtf16LE.data(), wlen);
            *(wchar_t*)(entry.cnUtf16LE.data() + wlen * 2) = 0;
            g_dict[enStr] = std::move(entry);
            g_dictCount++;
        }
    }
    return g_dictCount > 0;
}

// ===================== Rollback 词汇表加载 =====================
static bool LoadRollbackJson(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize <= 0) { fclose(f); return false; }
    if (fileSize > 4 * 1024 * 1024) {
        LogWrite("[Rollback] JSON file too large: %ld bytes\n", fileSize);
        fclose(f); return false;
    }
    std::vector<char> buf(fileSize + 1, 0);
    fread(buf.data(), 1, fileSize, f);
    fclose(f);
    int len = (int)fileSize;
    LogWrite("[Rollback] JSON file size: %ld bytes\n", fileSize);

    int pos = SkipWhitespace(buf.data(), 0, len);
    if (pos >= len || buf[pos] != '{') {
        LogWrite("[Rollback] JSON: expected '{' at pos %d\n", pos);
        return false;
    }
    pos++;
    pos = SkipWhitespace(buf.data(), pos, len);

    char enKey[8192];
    char cnValue[8192];

    while (pos < len) {
        pos = SkipWhitespace(buf.data(), pos, len);
        if (pos >= len) break;
        if (buf[pos] == '}') { pos++; break; }
        if (buf[pos] == ',') { pos++; continue; }

        int newPos = ParseJsonString(buf.data(), pos, len, enKey, sizeof(enKey));
        if (newPos < 0) {
            LogWrite("[Rollback] JSON: parse key failed at pos %d\n", pos);
            break;
        }
        pos = newPos;

        pos = SkipWhitespace(buf.data(), pos, len);
        if (pos >= len || buf[pos] != ':') {
            LogWrite("[Rollback] JSON: expected ':' at pos %d\n", pos);
            break;
        }
        pos++;
        pos = SkipWhitespace(buf.data(), pos, len);

        newPos = ParseJsonString(buf.data(), pos, len, cnValue, sizeof(cnValue));
        if (newPos < 0) {
            LogWrite("[Rollback] JSON: parse value failed for key '%.80s'\n", enKey);
            break;
        }
        pos = newPos;

        std::string en(enKey);
        std::string cn(cnValue);
        ReplaceNewlineLiteral(cn);
        if (!en.empty() && !cn.empty()) {
            g_rollback.push_back({en, cn});
            g_rollbackCount++;
        }
    }
    return g_rollbackCount > 0;
}

// ===================== Rollback 排序比较 =====================
// 按 key 长度降序排列，长短语优先匹配，避免短词破坏长短语内部
static bool RollbackSortByLengthDesc(const std::pair<std::string,std::string>& a,
                                      const std::pair<std::string,std::string>& b) {
    return a.first.size() > b.first.size();
}

// ===================== Rollback 词汇替换 =====================
// 对 UTF-8 英文文本做大小写不敏感的子串替换，返回替换后的 UTF-8 文本
// g_rollback 按 key 长度降序排列，长短语优先匹配
static bool RollbackReplace(const std::string& enText, std::string& outUtf8) {
    outUtf8 = enText;
    bool anyReplaced = false;
    std::string lowerText = AsciiToLower(enText);
    for (const auto& kv : g_rollback) {
        const std::string& from = kv.first;
        const std::string& to = kv.second;
        if (from.empty()) continue;
        std::string lowerFrom = AsciiToLower(from);
        size_t pos = 0;
        while ((pos = lowerText.find(lowerFrom, pos)) != std::string::npos) {
            outUtf8.replace(pos, from.size(), to);
            lowerText.replace(pos, lowerFrom.size(), std::string(to.size(), ' '));
            pos += to.size();
            anyReplaced = true;
        }
    }
    return anyReplaced;
}

// 旧格式兼容: tab 分隔文本
static bool LoadDictTxt(const char* path) {
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
        ReplaceNewlineLiteral(cnStr);
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

// ===================== 文本归一化 =====================
// 将游戏传入的实际控制字符归一化为 dict key 中的字面量格式
// \n (0x0A) -> [newline]
// \r (0x0D) -> 删除
// 其他字符（含 \x01 颜色码、%s、%d 等）保持不变
static std::string NormalizeText(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\n') {
            out += "[newline]";
        } else if (text[i] == '\r') {
            // 跳过 \r
        } else {
            out += text[i];
        }
    }
    return out;
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

// ===================== GDI 字体 =====================
static HFONT g_cjkFont = nullptr;
static int g_tmHeight = 16;
static int g_tmAscent = 13;
static int g_tmDescent = 3;

static void InitGdiFonts() {
    if (g_cjkFont) return;

    // 加载自定义 TTF
    if (g_cfg.fontFile[0]) {
        std::string dllDir = GetDllDir();
        std::string fontPath = dllDir + g_cfg.fontFile;
        if (AddFontResourceExA(fontPath.c_str(), FR_PRIVATE, nullptr)) {
            LogWrite("[Font] Loaded TTF: %s\n", fontPath.c_str());
        } else {
            LogWrite("[Font] Failed to load TTF: %s (err=%d)\n", fontPath.c_str(), GetLastError());
        }
    }

    int quality = NONANTIALIASED_QUALITY;
    if (g_cfg.quality == 1) quality = ANTIALIASED_QUALITY;
    else if (g_cfg.quality == 2) quality = CLEARTYPE_QUALITY;

    wchar_t wfontName[128];
    MultiByteToWideChar(CP_ACP, 0, g_cfg.fontName, -1, wfontName, 128);

    g_cjkFont = CreateFontW(
        -g_cfg.fontSize, 0, 0, 0, g_cfg.fontWeight,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        quality,
        DEFAULT_PITCH | FF_DONTCARE,
        wfontName
    );

    if (g_cjkFont) {
        HDC tdc = GetDC(nullptr);
        HFONT oldF = (HFONT)SelectObject(tdc, g_cjkFont);
        TEXTMETRICW tm;
        if (GetTextMetricsW(tdc, &tm)) {
            g_tmHeight = tm.tmHeight;
            g_tmAscent = tm.tmAscent;
            g_tmDescent = tm.tmDescent;
            LogWrite("[Font] %s %dpt: tmHeight=%d tmAscent=%d tmDescent=%d tmInternalLeading=%d\n",
                g_cfg.fontName, g_cfg.fontSize, tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmInternalLeading);
        }

        // 验证 GetGlyphOutlineW 是否对该字体可用（可变字体可能不兼容）
        GLYPHMETRICS testGm;
        MAT2 testMat = {{0,1},{0,0},{0,0},{0,1}};
        DWORD testRet = GetGlyphOutlineW(tdc, 0x4E2D /* 中 */, GGO_BITMAP, &testGm, 0, nullptr, &testMat);
        bool ggoOk = (testRet != GDI_ERROR);
        if (!ggoOk) {
            LogWrite("[Font] WARNING: GetGlyphOutlineW failed for custom font (variable font?), falling back to SimSun\n");
            SelectObject(tdc, oldF);
            DeleteObject(g_cjkFont);
            g_cjkFont = nullptr;
        } else {
            LogWrite("[Font] GetGlyphOutlineW test OK (glyphSize=%u for U+4E2D)\n", testRet);
        }

        if (g_cjkFont) {
            SelectObject(tdc, oldF);
            ReleaseDC(nullptr, tdc);
            LogWrite("[Font] %s %dpt created (handle=0x%p) tmHeight=%d tmAscent=%d\n",
                g_cfg.fontName, g_cfg.fontSize, g_cjkFont, g_tmHeight, g_tmAscent);
        }
    }

    // 回退到 SimSun
    if (!g_cjkFont) {
        wchar_t simsun[] = L"SimSun";
        g_cjkFont = CreateFontW(
            -g_cfg.fontSize, 0, 0, 0, g_cfg.fontWeight,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, simsun);
        if (g_cjkFont) {
            HDC tdc = GetDC(nullptr);
            HFONT oldF = (HFONT)SelectObject(tdc, g_cjkFont);
            TEXTMETRICW tm;
            if (GetTextMetricsW(tdc, &tm)) {
                g_tmHeight = tm.tmHeight;
                g_tmAscent = tm.tmAscent;
                g_tmDescent = tm.tmDescent;
            }
            SelectObject(tdc, oldF);
            ReleaseDC(nullptr, tdc);
            LogWrite("[Font] Fallback SimSun %dpt (handle=0x%p) tmHeight=%d tmAscent=%d\n",
                g_cfg.fontSize, g_cjkFont, g_tmHeight, g_tmAscent);
        } else {
            LogWrite("[Font] ERROR: SimSun fallback also failed (err=%d)\n", GetLastError());
        }
    }
}

// ===================== 渲染设备信息 =====================
struct RenderDevInfo {
    uint32_t obj;
    uint32_t pixelBuf;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t stride;
    uint32_t clipL;
    uint32_t clipT;
    uint32_t clipR;
    uint32_t clipB;
};

static bool GetRenderDevInfo(int a4, RenderDevInfo& info) {
    memset(&info, 0, sizeof(info));
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
    if (!SafeRead32(info.obj + OFF_PIXELBUF, &info.pixelBuf) || !info.pixelBuf) {
        if (g_blitFailCount < 5)
            LogWrite("[Render] pixelBuf = 0 (obj=0x%X)\n", info.obj);
        return false;
    }
    if (!SafeRead32(info.obj + OFF_WIDTH, &info.width) || !info.width) return false;
    if (!SafeRead32(info.obj + OFF_HEIGHT, &info.height) || !info.height) return false;
    if (!SafeRead32(info.obj + OFF_BPP, &info.bpp) || !info.bpp) return false;
    if (!SafeRead32(info.obj + OFF_STRIDE, &info.stride) || !info.stride) return false;
    SafeRead32(info.obj + OFF_CLIP_LEFT, &info.clipL);
    SafeRead32(info.obj + OFF_CLIP_TOP, &info.clipT);
    SafeRead32(info.obj + OFF_CLIP_RIGHT, &info.clipR);
    SafeRead32(info.obj + OFF_CLIP_BOT, &info.clipB);
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

// ===================== v15.0: 旧字背景 LRU 擦除 =====================
// 原理: DirectBlit 直写 front surface, 引擎(合成/脏矩形)不会重绘该区域 → 旧字残留。
// 做法: 画字前把整块 clip 区域背景缓存到堆(key=精确位置); 同位置再画时
//       先写回缓存背景(擦旧字) 再重存当前(干净)背景 然后画新字;
//       移走的位置由过期 slot 恢复补擦(LRU, 约 2 帧未更新即恢复释放)。
#define V15_SLOTS 1024
#define V15_MAX_BUF (192 * 1024)
#define V15_EXPIRE_TICKS 48   // ~2 帧(每帧 ~20 blit)。移走后 2 帧内补擦
#define V15_SCAN_INTERVAL 8   // 每 8 次 blit 扫一轮过期

struct V15Slot {
    int l, t, r, b;       // 缓存矩形(已按 clip 归一)
    int rowBytes;         // 一行字节数 = w*bppB
    int bppB;             // bytes per pixel
    int lastTick;
    uint8_t* buf;
};
static V15Slot g_v15[V15_SLOTS];
static int  g_v15Tick = 0;
static int  g_v15Scan = 0;
static int  g_v15Hits = 0;     // 恢复命中(擦旧字)
static int  g_v15New = 0;      // 新建缓存
static int  g_v15Clean = 0;    // 过期补擦
static int  g_v15Skip = 0;     // 超尺寸/无内存跳过
static int  g_v15Full = 0;     // slot 满
static int  g_v15OOR = 0;      // 区域越界 clamp 计数

// 把 slot 缓存背景写回 pixelBuf 的 clip 可见部分(擦旧字)。行级 clamp 防越界。
static void V15WriteBack(const V15Slot& s, const RenderDevInfo& rdi) {
    uint8_t* px = (uint8_t*)rdi.pixelBuf;
    int h = s.b - s.t;
    for (int yy = 0; yy < h; yy++) {
        int dy = s.t + yy;
        if (dy < 0 || dy >= (int)rdi.height) continue;
        // 仅恢复设备可见行
        if (dy < (int)rdi.clipT || dy >= (int)rdi.clipB) continue;
        int x0 = s.l, x1 = s.r;
        if (x0 < (int)rdi.clipL) x0 = rdi.clipL;
        if (x1 > (int)rdi.clipR) x1 = rdi.clipR;
        if (x0 < 0) x0 = 0;
        if (x1 > (int)rdi.width) x1 = rdi.width;
        if (x1 <= x0) continue;
        memcpy(px + dy * rdi.stride + x0 * s.bppB,
               s.buf + (size_t)yy * s.rowBytes + (size_t)(x0 - s.l) * s.bppB,
               (size_t)(x1 - x0) * s.bppB);
    }
}

// 把 pixelBuf 的 clip 区域背景保存进 slot.buf(全矩形宽, 含不可见列)
static bool V15Capture(V15Slot& s, const RenderDevInfo& rdi) {
    uint8_t* px = (uint8_t*)rdi.pixelBuf;
    int h = s.b - s.t, w = s.r - s.l;
    s.rowBytes = w * s.bppB;
    for (int yy = 0; yy < h; yy++) {
        int dy = s.t + yy;
        uint8_t* dstRow = s.buf + (size_t)yy * s.rowBytes;
        if (dy < 0 || dy >= (int)rdi.height) { memset(dstRow, 0, s.rowBytes); continue; }
        // 行内 x 区间裁剪(避免越界), 区间外填 0
        int x0 = s.l, x1 = s.r;
        if (x0 < 0) x0 = 0;
        if (x1 > (int)rdi.width) x1 = rdi.width;
        if (x0 < s.l) memset(dstRow, 0, (size_t)(x0 - s.l) * s.bppB);
        if (x1 < s.r) memset(dstRow + (size_t)(x1 - s.l) * s.bppB, 0, (size_t)(s.r - x1) * s.bppB);
        if (x1 > x0) {
            memcpy(dstRow + (size_t)(x0 - s.l) * s.bppB,
                   px + dy * rdi.stride + (size_t)x0 * s.bppB,
                   (size_t)(x1 - x0) * s.bppB);
        }
    }
    return true;
}

// 过期扫描: 把超过 V15_EXPIRE_TICKS 未更新的 slot 恢复(擦掉移走残留)并释放
static void V15Expire(const RenderDevInfo& rdi) {
    for (int i = 0; i < V15_SLOTS; i++) {
        V15Slot& s = g_v15[i];
        if (!s.buf) continue;
        if (g_v15Tick - s.lastTick <= V15_EXPIRE_TICKS) continue;
        __try {
            V15WriteBack(s, rdi);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        free(s.buf);
        s.buf = nullptr;
        g_v15Clean++;
    }
}

// 主入口: restore(命中→擦旧字) + capture(重存干净背景)。必须在画字前调用。
static void V15EraseOldText(int l, int t, int r, int b, const RenderDevInfo& rdi) {
    if (r <= l || b <= t) return;
    if (!rdi.pixelBuf || !rdi.stride) return;
    int bppB = (int)(rdi.bpp / 8);
    if (bppB < 1 || bppB > 4) return;
    int w = r - l, h = b - t;
    if (w <= 0 || h <= 0) return;
    if ((size_t)w * h * bppB > V15_MAX_BUF) { g_v15Skip++; return; }

    g_v15Tick++;
    g_v15Scan++;
    if ((g_v15Scan & (V15_SCAN_INTERVAL - 1)) == 0) {
        __try { V15Expire(rdi); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // 1) 查找精确匹配位置 → 命中则先写回背景(擦旧字), 再重存干净背景
    for (int i = 0; i < V15_SLOTS; i++) {
        V15Slot& s = g_v15[i];
        if (!s.buf) continue;
        if (s.l != l || s.t != t || s.r != r || s.b != b) continue;
        __try {
            V15WriteBack(s, rdi);          // 擦旧字
            V15Capture(s, rdi);            // 重存当前(干净)背景
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        s.lastTick = g_v15Tick;
        g_v15Hits++;
        return;
    }

    // 2) 未命中 → 找空 slot 新建缓存(保存当前背景)
    int freeIdx = -1;
    for (int i = 0; i < V15_SLOTS; i++) {
        if (!g_v15[i].buf) { freeIdx = i; break; }
    }
    if (freeIdx < 0) { g_v15Full++; return; }
    V15Slot& s = g_v15[freeIdx];
    memset(&s, 0, sizeof(s));
    s.buf = (uint8_t*)malloc((size_t)w * h * bppB);
    if (!s.buf) { g_v15Skip++; return; }
    s.l = l; s.t = t; s.r = r; s.b = b;
    s.bppB = bppB;
    s.rowBytes = w * bppB;
    s.lastTick = g_v15Tick;
    bool ok = false;
    __try { ok = V15Capture(s, rdi); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) {
        free(s.buf);
        s.buf = nullptr;
        g_v15Skip++;
        return;
    }
    g_v15New++;
}

// ===================== RGB565 颜色编码 =====================
static uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// ===================== 脏矩形系统 =====================
// sub_673FB0: __thiscall char sub_673FB0(int *this, int *rect, int layerIdx)
// this = 脏矩形管理器 (dword_7C12FC)
// rect = 指向 {left, top, right, bottom} 的指针 (屏幕坐标)
// layerIdx = 图层索引 (dword_7C5228)
// 内部自动做: 可见性检查(与视图边界相交) -> 屏幕->世界坐标转换 -> sub_673680 提交
// per-layer 队列布局: [mgr+layerIdx*4+0x34] = count, [mgr+layerIdx*4+0x3c] = rect 数组
typedef char (__thiscall *SubmitDirtyRect_t)(int thisPtr, int* rect, int layerIdx);
static SubmitDirtyRect_t g_submitDirty = (SubmitDirtyRect_t)ADDR_SUBMIT_DIRTY;

// sub_5D7720: 引擎合成/重绘上屏函数。cdecl 5 栈参（arg1=设备, arg2/3=rect*,
//   arg4/5=0）。调用者 add esp,0x14 清理。
typedef int (__cdecl *Orig5D7720_t)(int a1, void* a2, void* a3, int a4, int a5);
static Orig5D7720_t g_orig5D7720 = nullptr;

// 全屏脏矩形提交计数（用于日志统计，沿用 v14.0 字段名）
static int g_dirtyRectCount = 0;

// ===== v14.1 诊断数据 =====
// (a) 提交 delta 统计
static int g_dirtyDeltaOK    = 0;   // delta==1，提交入队
static int g_dirtyDeltaZero  = 0;   // delta==0，提交被丢弃
static int g_dirtyDeltaWeird = 0;   // delta 异常(<0 或 >1)
static int g_dirtyMgrNonNull = 0;   // [0x7C12FC] 脏矩形管理器非空
static int g_dirtyMgrNull    = 0;   // [0x7C12FC] 为 0
static int g_dirtySampleLogged = 0; // 抽样日志计数器（每 200 个 blit 1 行）
// (b) 引擎消费 (sub_5D7720) 统计
static int g_composeCallsTotal = 0;  // 累计 sub_5D7720 调用次数
static int g_composeSampleLogged = 0;
// (c) 设备一致性
static int g_composeDevMatch  = 0;  // arg1 == [0x7CA9E8]
static int g_composeDevDiffer = 0;  // arg1 != [0x7CA9E8]
// 每秒报告
static LARGE_INTEGER s_qpcFreq = {0};
static int64_t s_qpcLast = 0;
static int s_blitsLast = 0;
static int s_composeLast = 0;
static int s_deltaOKLast = 0;
static int s_deltaZeroLast = 0;
static int s_devMatchLast = 0;
static int s_devDifferLast = 0;
static int s_mgrNullLast = 0;

// 独立 C 函数包装 __try（避免 C++ 对象展开冲突）+ 提交前后读 [mgr+layer*4+0x34] delta
static void SafeSubmitFullscreenDirty(uint32_t dirtyMgr, int width, int height, int layerIdx) {
    int rect[4] = { 0, 0, width, height };  // 整个可见区域 (屏幕坐标)
    int cntBefore = -1, cntAfter = -1;
    int ofs = layerIdx * 4 + 0x34;
    SafeRead32(dirtyMgr + (uint32_t)ofs, (uint32_t*)&cntBefore);
    __try {
        g_submitDirty((int)dirtyMgr, rect, layerIdx);
        g_dirtyRectCount++;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    SafeRead32(dirtyMgr + (uint32_t)ofs, (uint32_t*)&cntAfter);
    int delta = (cntBefore >= 0 && cntAfter >= 0) ? (cntAfter - cntBefore) : -999;
    if (delta == 1) g_dirtyDeltaOK++;
    else if (delta == 0) g_dirtyDeltaZero++;
    else g_dirtyDeltaWeird++;

    // 抽样日志：前 20 次 + 每 200 次 blit 1 行
    g_dirtySampleLogged++;
    if (g_dirtySampleLogged <= 20 || (g_blitCount > 0 && (g_blitCount % 200) == 0)) {
        LogWrite("[SubmitDiag] blit=%d layer=%d mgr=0x%X cntBefore=%d cntAfter=%d delta=%d\n",
            g_blitCount, layerIdx, dirtyMgr, cntBefore, cntAfter, delta);
    }
}

// ===================== v14.1: 每秒汇总报告 =====================
static void MaybeReportPerSecond() {
    if (s_qpcFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&s_qpcFreq);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (s_qpcLast == 0) {
        s_qpcLast = now.QuadPart;
        s_blitsLast = g_blitCount;
        s_composeLast = g_composeCallsTotal;
        s_deltaOKLast = g_dirtyDeltaOK;
        s_deltaZeroLast = g_dirtyDeltaZero;
        s_devMatchLast = g_composeDevMatch;
        s_devDifferLast = g_composeDevDiffer;
        s_mgrNullLast = g_dirtyMgrNull;
        return;
    }
    int64_t elapsed = now.QuadPart - s_qpcLast;
    if (elapsed < s_qpcFreq.QuadPart) return;  // 1 秒内不报

    double seconds = (double)elapsed / (double)s_qpcFreq.QuadPart;
    int dBlit    = g_blitCount - s_blitsLast;
    int dCompose = g_composeCallsTotal - s_composeLast;
    int dDeltaOK = g_dirtyDeltaOK - s_deltaOKLast;
    int dDeltaZ  = g_dirtyDeltaZero - s_deltaZeroLast;
    int dDevM    = g_composeDevMatch - s_devMatchLast;
    int dDevD    = g_composeDevDiffer - s_devDifferLast;
    int dMgrNull = g_dirtyMgrNull - s_mgrNullLast;

    LogWrite("[PerSec] t=%.2fs blits=%d (%.0f/s) compose=%d (%.0f/s) submitOK=%d submitZero=%d submitWeird=%d devMatch=%d devDiffer=%d mgrNull=%d\n",
        seconds, dBlit, dBlit/seconds, dCompose, dCompose/seconds,
        dDeltaOK, dDeltaZ, g_dirtyDeltaWeird,
        dDevM, dDevD, dMgrNull);

    s_qpcLast = now.QuadPart;
    s_blitsLast = g_blitCount;
    s_composeLast = g_composeCallsTotal;
    s_deltaOKLast = g_dirtyDeltaOK;
    s_deltaZeroLast = g_dirtyDeltaZero;
    s_devMatchLast = g_composeDevMatch;
    s_devDifferLast = g_composeDevDiffer;
    s_mgrNullLast = g_dirtyMgrNull;
}

// ===================== v14.1: hook sub_5D7720 (cdecl 5 参) =====================
int __cdecl Hooked_5D7720(int a1, void* a2, void* a3, int a4, int a5) {
    g_composeCallsTotal++;

    uint32_t devBlit = 0;
    SafeRead32(ADDR_7CA9E4, &devBlit);
    bool devMatch = (devBlit == (uint32_t)a1);
    if (devMatch) g_composeDevMatch++; else g_composeDevDiffer++;

    // 抽样日志：前 50 次 + 每次设备不匹配都记
    g_composeSampleLogged++;
    if (g_composeSampleLogged <= 50 || !devMatch) {
        int L = 0, T = 0, R = 0, B = 0;
        __try {
            if (a2) {
                int* r = (int*)a2;
                L = r[0]; T = r[1]; R = r[2]; B = r[3];
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            L = T = R = B = -1;
        }
        LogWrite("[Compose %d] dev=0x%X [0x7CA9E4]=0x%X match=%d rect=[%d,%d,%d,%d] a4=%d a5=%d\n",
            g_composeCallsTotal, a1, devBlit, (int)devMatch, L, T, R, B, a4, a5);
    }
    return g_orig5D7720(a1, a2, a3, a4, a5);
}

// ===================== 直接像素缓冲区渲染 =====================
static bool DirectBlitText(int thisPtr, int a2, int a3, int a4,
                           const wchar_t* wstr, int wlen) {
    if (!wstr || wlen <= 0) return false;

    RenderDevInfo rdi;
    if (!GetRenderDevInfo(a4, rdi)) {
        g_blitFailCount++;
        return false;
    }

    // 读取文本布局
    uint32_t* dwordBase = (uint32_t*)thisPtr;
    int startX = (int)dwordBase[3] + a2;
    int endX   = (int)dwordBase[5] + a2;
    int startY = (int)dwordBase[4] + a3;
    int endY   = (int)dwordBase[6] + a3;
    uint8_t flags = (uint8_t)dwordBase[7];

    bool centered   = (flags & 0x01) != 0;
    bool rightAlign = (flags & 0x02) != 0;
    bool outlined   = (flags & 0x04) != 0 || g_cfg.enableOutline;
    bool vCentered  = (flags & 0x08) != 0;

    // 颜色
    uint32_t fgColor, bgColor;
    if (g_cfg.overrideColor) {
        fgColor = g_cfg.fgColor;
        bgColor = g_cfg.bgColor;
    } else {
        fgColor = dwordBase[25];
        bgColor = dwordBase[26];
        if (fgColor == 0) {
            switch (rdi.bpp) {
                case 16: fgColor = 0xFFFF; break;
                case 24: case 32: fgColor = 0xFFFFFF; break;
                default: fgColor = 0xFF; break;
            }
        }
    }

    // 偏移微调
    startX += g_cfg.xOffset;
    endX   += g_cfg.xOffset;
    startY += g_cfg.yOffset;
    endY   += g_cfg.yOffset;

    int bytesPerPixel = rdi.bpp / 8;
    if (bytesPerPixel < 1 || bytesPerPixel > 4) {
        if (g_blitFailCount < 5)
            LogWrite("[Blit] Unsupported BPP=%u\n", rdi.bpp);
        g_blitFailCount++;
        return false;
    }

    HDC mdc = CreateCompatibleDC(nullptr);
    if (!mdc) { g_blitFailCount++; return false; }
    HFONT oldFont = (HFONT)SelectObject(mdc, g_cjkFont);

    DWORD glyphFormat = (g_cfg.renderMode == 1) ? GGO_GRAY8 : GGO_BITMAP;
    bool useAlpha = (g_cfg.blendMode == 1) && (g_cfg.renderMode == 1);
    int alphaMax = 64;  // GGO_GRAY8 的最大灰度值

    // 测量文本总宽度 + 自动换行
    int totalWidth = 0;
    int textHeight = g_tmHeight;
    int maxLineWidth = 0;
    int curLineWidth = 0;
    int availWidth = (g_cfg.wrapWidth > 0) ? g_cfg.wrapWidth : (endX - startX + g_cfg.wrapWidthAdjust);
    if (availWidth <= 0) availWidth = rdi.width - startX;

    // 字符宽度缓存
    std::vector<int> charWidths(wlen, 0);
    for (int i = 0; i < wlen; i++) {
        if (wstr[i] == L'\n') {
            if (curLineWidth > maxLineWidth) maxLineWidth = curLineWidth;
            curLineWidth = 0;
            textHeight += g_tmHeight + g_cfg.lineSpacing;
            continue;
        }
        if (wstr[i] == L'\r') continue;
        GLYPHMETRICS gm;
        MAT2 mat = {{0,1},{0,0},{0,0},{0,1}};
        DWORD ret = GetGlyphOutlineW(mdc, wstr[i], GGO_METRICS, &gm, 0, nullptr, &mat);
        if (ret != GDI_ERROR) {
            charWidths[i] = gm.gmCellIncX;
            curLineWidth += gm.gmCellIncX;
        }
    }
    if (curLineWidth > maxLineWidth) maxLineWidth = curLineWidth;
    totalWidth = maxLineWidth;

    // ★ 自动换行：如果文本超出可用宽度且没有显式换行符，按词断行
    bool needWrap = false;
    std::vector<int> wrapPositions; // 每行结束位置（不包含该字符）
    {
        int lineStart = 0;
        int lineWidth = 0;
        int lastBreakPos = -1; // 上一个可断行位置
        for (int i = 0; i < wlen; i++) {
            if (wstr[i] == L'\n') {
                wrapPositions.push_back(i);
                lineStart = i + 1;
                lineWidth = 0;
                lastBreakPos = -1;
                continue;
            }
            if (wstr[i] == L'\r') continue;
            lineWidth += charWidths[i];
            // CJK 字符可以在任意位置断行
            if (wstr[i] >= 0x4E00 && wstr[i] <= 0x9FFF) {
                lastBreakPos = i + 1;
            }
            // 空格也可以断行
            if (wstr[i] == L' ' || wstr[i] == L'\t') {
                lastBreakPos = i + 1;
            }
            // 超出宽度则断行
            if (lineWidth > availWidth && i > lineStart) {
                if (lastBreakPos > lineStart) {
                    wrapPositions.push_back(lastBreakPos);
                    lineStart = lastBreakPos;
                    // 重新计算当前行宽度
                    lineWidth = 0;
                    for (int j = lineStart; j <= i; j++) {
                        if (wstr[j] != L'\n' && wstr[j] != L'\r')
                            lineWidth += charWidths[j];
                    }
                } else {
                    // 没有合适的断行点，强制在当前位置断行
                    wrapPositions.push_back(i);
                    lineStart = i;
                    lineWidth = charWidths[i];
                }
                needWrap = true;
            }
        }
        wrapPositions.push_back(wlen); // 最后一行
    }

    if (needWrap) {
        textHeight = (int)wrapPositions.size() * (g_tmHeight + g_cfg.lineSpacing);
        // 重新计算最大行宽
        maxLineWidth = 0;
        int lineStart = 0;
        for (size_t wi = 0; wi < wrapPositions.size(); wi++) {
            int lineEnd = wrapPositions[wi];
            int lw = 0;
            for (int j = lineStart; j < lineEnd; j++) {
                if (wstr[j] != L'\n' && wstr[j] != L'\r')
                    lw += charWidths[j];
            }
            if (lw > maxLineWidth) maxLineWidth = lw;
            lineStart = lineEnd;
            // 跳过换行符本身
            if (lineStart < wlen && wstr[lineStart] == L'\n') lineStart++;
        }
        totalWidth = maxLineWidth;
    }

    // 计算绘制位置
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

    // 裁剪
    int clipL = startX > (int)rdi.clipL ? startX : (int)rdi.clipL;
    int clipT = startY > (int)rdi.clipT ? startY : (int)rdi.clipT;
    int clipR = endX < (int)rdi.clipR ? endX : (int)rdi.clipR;
    // ★ 修复：扩展 clipB 以适应 CJK 字体实际高度
    // 原版 endY 基于小字体行高，CJK 字体更高，多行文本会超出 endY 被裁剪
    int effectiveEndY = endY;
    if (textHeight > (endY - startY)) {
        effectiveEndY = startY + textHeight;
    }
    int clipB = effectiveEndY < (int)rdi.clipB ? effectiveEndY : (int)rdi.clipB;
    if (clipR > (int)rdi.width) clipR = (int)rdi.width;
    if (clipB > (int)rdi.height) clipB = (int)rdi.height;

    // v16.0: motion 采样 — 记录每次绘制的文本与位置(分析移动模式)
    //        g_blitCount 此时是本次 blit 的序号(0 起), 与 pos 日志一致
    MotionLog(thisPtr, g_blitCount, wstr, wlen, clipL, clipT, clipR, clipB, fgColor, flags);

    if (g_blitCount < 20) {
        LogWrite("[Blit %d] pos=(%d,%d) clip=[%d,%d,%d,%d] flags=0x%02X fg=0x%X bg=0x%X bpp=%u fmt=%s blend=%s\n",
            g_blitCount, drawX, drawY, clipL, clipT, clipR, clipB,
            flags, fgColor, bgColor, rdi.bpp,
            g_cfg.renderMode ? "GRAY8" : "BITMAP",
            useAlpha ? "alpha" : "direct");
    }

    // ★ 残影修复 (v14.0 方案保留): 全屏脏矩形
    //   每次文字渲染时，调用 sub_673FB0 把整个可见区域标记为脏矩形
    //   游戏同帧在 sub_454500 中读取脏矩形列表并调用 sub_5D7720 全屏重绘，覆盖残影
    //   v14.1: 统计 mgr null 率 + 由 SafeSubmitFullscreenDirty 内做 delta 抽样
    {
        uint32_t dirtyMgr = 0;
        int layerIdx = 0;
        if (SafeRead32(ADDR_DIRTY_MGR, &dirtyMgr) && dirtyMgr) {
            g_dirtyMgrNonNull++;
            if (SafeRead32(ADDR_LAYER_IDX, (uint32_t*)&layerIdx)) {
                SafeSubmitFullscreenDirty(dirtyMgr, (int)rdi.width, (int)rdi.height, layerIdx);
            }
        } else {
            g_dirtyMgrNull++;
        }
    }

    // ★ v15.0 方案 C: 画字前擦除同位置旧字 + 缓存干净背景
    //   顺序: V15EraseOldText(恢复同位置背景→擦旧字) 必须先于任何画像素
    {
        // 用文本实际落地区域(与画字裁剪一致), 略外扩描边余量
        int eL = clipL, eT = clipT, eR = clipR, eB = clipB;
        int ow = outlined ? (g_cfg.outlineWidth > 0 ? g_cfg.outlineWidth : 1) : 0;
        if (eL - ow > 0) eL -= ow;
        if (eT - ow > 0) eT -= ow;
        if (eR + ow < (int)rdi.width) eR += ow;
        if (eB + ow < (int)rdi.height) eB += ow;
        // 注意: 这里不能包 __try(DirectBlitText 有 std::vector 需对象展开 → C2712)
        //       V15EraseOldText 内部已有 __try 保护
        V15EraseOldText(eL, eT, eR, eB, rdi);
    }

    // ★ 渲染：使用换行位置逐行绘制
    int curX = drawX;
    int curY = drawY;
    int lineH = g_tmHeight + g_cfg.lineSpacing;
    int renderStart = 0;

    for (size_t wi = 0; wi < wrapPositions.size(); wi++) {
        int lineEnd = wrapPositions[wi];
        curX = drawX;
        // 居中/右对齐按行重新计算
        if (centered) {
            int lw = 0;
            for (int j = renderStart; j < lineEnd; j++) {
                if (wstr[j] != L'\n' && wstr[j] != L'\r')
                    lw += charWidths[j];
            }
            curX = startX + (endX - startX - lw) / 2;
        } else if (rightAlign) {
            int lw = 0;
            for (int j = renderStart; j < lineEnd; j++) {
                if (wstr[j] != L'\n' && wstr[j] != L'\r')
                    lw += charWidths[j];
            }
            curX = endX - lw;
        }

    for (int i = renderStart; i < lineEnd; i++) {
        if (wstr[i] == L'\n') {
            break;
        }
        if (wstr[i] == L'\r') continue;
        if (wstr[i] == L'\t') {
            curX += g_tmHeight * 4;
            continue;
        }

        GLYPHMETRICS gm;
        MAT2 mat = {{0,1},{0,0},{0,0},{0,1}};
        DWORD glyphSize = GetGlyphOutlineW(mdc, wstr[i], glyphFormat, &gm, 0, nullptr, &mat);
        if (glyphSize == GDI_ERROR || glyphSize == 0) {
            GLYPHMETRICS gm2;
            DWORD ret = GetGlyphOutlineW(mdc, wstr[i], GGO_METRICS, &gm2, 0, nullptr, &mat);
            if (ret != GDI_ERROR) curX += gm2.gmCellIncX;
            else curX += g_tmHeight;
            continue;
        }

        std::vector<uint8_t> glyphBuf(glyphSize, 0);
        if (GetGlyphOutlineW(mdc, wstr[i], glyphFormat, &gm, glyphSize, glyphBuf.data(), &mat) == GDI_ERROR) {
            curX += gm.gmCellIncX;
            continue;
        }

        int glyphW = gm.gmBlackBoxX;
        int glyphH = gm.gmBlackBoxY;
        int glyphStride;
        bool isBitmap = (g_cfg.renderMode == 0);

        if (isBitmap) {
            glyphStride = ((glyphW + 7) / 8 + 3) & ~3;  // 1bpp DWORD 对齐
        } else {
            glyphStride = (glyphW + 3) & ~3;  // 8bpp DWORD 对齐
            if ((DWORD)(glyphStride * glyphH) > glyphSize) {
                glyphStride = glyphSize / glyphH;
            }
        }

        int originX = gm.gmptGlyphOrigin.x;
        int originY = gm.gmptGlyphOrigin.y;

        // ★ Y 位置修复: originY 是从基线向上的偏移
        //   基线 = curY + tmAscent
        //   字形顶部 = 基线 - originY = curY + tmAscent - originY
        int glyphTopY = curY + g_tmAscent - originY;

        auto blitGlyph = [&](int offX, int offY, uint32_t color) {
            for (int py = 0; py < glyphH; py++) {
                int dstY = glyphTopY + py + offY;
                if (dstY < clipT || dstY >= clipB) continue;
                if (dstY < 0 || dstY >= (int)rdi.height) continue;

                uint8_t* dstRow = (uint8_t*)rdi.pixelBuf + dstY * rdi.stride;
                uint8_t* srcRow = glyphBuf.data() + py * glyphStride;

                for (int px = 0; px < glyphW; px++) {
                    bool isOn;
                    int alpha = alphaMax;

                    if (isBitmap) {
                        isOn = (srcRow[px >> 3] & (0x80 >> (px & 7))) != 0;
                        if (!isOn) continue;
                    } else {
                        uint8_t gray = srcRow[px];
                        if (gray == 0) continue;
                        if (gray > alphaMax) gray = (uint8_t)alphaMax;
                        alpha = gray;
                    }

                    int dstX = curX + originX + px + offX;
                    if (dstX < clipL || dstX >= clipR) continue;
                    if (dstX < 0 || dstX >= (int)rdi.width) continue;

                    __try {
                        if (useAlpha && !isBitmap) {
                            int invA = alphaMax - alpha;
                            switch (rdi.bpp) {
                                case 16: {
                                    uint16_t* p = (uint16_t*)(dstRow + dstX * 2);
                                    uint16_t d = *p;
                                    int dR=(d>>11)&0x1F, dG=(d>>5)&0x3F, dB=d&0x1F;
                                    int sR=(color>>11)&0x1F, sG=(color>>5)&0x3F, sB=color&0x1F;
                                    *p=(uint16_t)(((sR*alpha+dR*invA)/alphaMax)<<11|((sG*alpha+dG*invA)/alphaMax)<<5|((sB*alpha+dB*invA)/alphaMax));
                                    break;
                                }
                                case 32: {
                                    uint32_t* p = (uint32_t*)(dstRow + dstX * 4);
                                    uint32_t d = *p;
                                    *p = (uint32_t)((((color&0xFF)*alpha+(d&0xFF)*invA)/alphaMax)|((((color>>8)&0xFF)*alpha+((d>>8)&0xFF)*invA)/alphaMax)<<8|((((color>>16)&0xFF)*alpha+((d>>16)&0xFF)*invA)/alphaMax)<<16);
                                    break;
                                }
                                case 24: {
                                    uint8_t* p = dstRow + dstX * 3;
                                    p[0]=(uint8_t)(((color&0xFF)*alpha+p[0]*invA)/alphaMax);
                                    p[1]=(uint8_t)(((color>>8&0xFF)*alpha+p[1]*invA)/alphaMax);
                                    p[2]=(uint8_t)(((color>>16&0xFF)*alpha+p[2]*invA)/alphaMax);
                                    break;
                                }
                                case 8:
                                    if (alpha > alphaMax/2) dstRow[dstX] = (uint8_t)color;
                                    break;
                            }
                        } else {
                            // 直接写入
                            switch (rdi.bpp) {
                                case 8:  dstRow[dstX] = (uint8_t)color; break;
                                case 16: *(uint16_t*)(dstRow + dstX * 2) = (uint16_t)color; break;
                                case 24:
                                    dstRow[dstX * 3]     = (uint8_t)(color);
                                    dstRow[dstX * 3 + 1] = (uint8_t)(color >> 8);
                                    dstRow[dstX * 3 + 2] = (uint8_t)(color >> 16);
                                    break;
                                case 32: *(uint32_t*)(dstRow + dstX * 4) = color; break;
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
        };

        // 描边
        if (outlined) {
            int ow = g_cfg.outlineWidth;
            if (ow < 1) ow = 1;
            for (int dy = -ow; dy <= ow; dy++) {
                for (int dx = -ow; dx <= ow; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    blitGlyph(dx, dy, bgColor);
                }
            }
        }

        // 前景色
        blitGlyph(0, 0, fgColor);

        curX += gm.gmCellIncX;
    }

    // 移动到下一行
    curY += lineH;
    renderStart = lineEnd;
    if (renderStart < wlen && wstr[renderStart] == L'\n') renderStart++;
    } // end wrap loop

    SelectObject(mdc, oldFont);
    DeleteDC(mdc);

    g_blitCount++;
    if (g_blitCount <= 50) {
        LogWrite("[Blit %d] done: text='%.*ls' pos=(%d,%d) chars=%d wraps=%d\n",
            g_blitCount, (wlen < 80 ? wlen : 80), wstr, drawX, drawY, wlen, (int)wrapPositions.size());
    }
    // v14.1: 每秒汇总报告（DirectBlit 是稳定节拍源）
    MaybeReportPerSecond();

    return true;
}

// ===================== 诊断日志辅助 =====================
static void SafeDiagLog(int ecx_this, int a2, int a3, int a4, int diagCount) {
    __try {
        uint32_t* db = (uint32_t*)ecx_this;
        uint32_t renderObj = (uint32_t)a4;
        uint32_t pixelBuf = 0;
        if (renderObj) {
            __try { pixelBuf = *(uint32_t*)(renderObj + OFF_PIXELBUF); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        fprintf(g_logFile, "[DIAG %d] this=0x%X flags=[7]=0x%X pos=[3]=%d [4]=%d [5]=%d [6]=%d fg=[25]=0x%X bg=[26]=0x%X a2=%d a3=%d a4=0x%X pixelBuf=0x%X\n",
            diagCount, ecx_this, db[7], (int)db[3], (int)db[4], (int)db[5], (int)db[6], db[25], db[26], a2, a3, a4, pixelBuf);
        fflush(g_logFile);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ===================== Hook sub_66E7B0 =====================
int __fastcall Hooked_66E7B0(int ecx_this, int edx_unused, int a2, int a3, int a4) {
    g_callCount++;

    // 诊断日志：前 50 次调用的 this 布局信息
    static int s_diagCount = 0;
    if (s_diagCount < 50 && g_logFile) {
        s_diagCount++;
        SafeDiagLog(ecx_this, a2, a3, a4, s_diagCount);
    }

    std::string enText;
    bool wide = false;
    int chLen = 0;
    bool readOk = ReadFullString(ecx_this, enText, &chLen, &wide);

    if (!readOk || enText.empty()) {
        return g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);
    }

    // 归一化: \n -> [newline] 以匹配 dict key
    std::string normText = NormalizeText(enText);
    auto it = g_dict.find(normText);
    if (it == g_dict.end()) {
        // ★ MISS: 尝试 rollback.json 词汇替换
        if (g_rollbackCount > 0) {
            std::string replacedUtf8;
            if (RollbackReplace(normText, replacedUtf8)) {
                // rollback 替换后，将残留的 [newline] 字面量转回 \n
                ReplaceNewlineLiteral(replacedUtf8);
                // rollback 成功，渲染替换后的文本
                int wlen = MultiByteToWideChar(CP_UTF8, 0, replacedUtf8.c_str(), (int)replacedUtf8.size(), nullptr, 0);
                if (wlen > 0) {
                    std::vector<uint8_t> utf16buf((wlen + 1) * 2, 0);
                    MultiByteToWideChar(CP_UTF8, 0, replacedUtf8.c_str(), (int)replacedUtf8.size(),
                        (wchar_t*)utf16buf.data(), wlen);
                    LogRollback(normText.c_str(), replacedUtf8.c_str(), wlen);
                    const wchar_t* wstr = (const wchar_t*)utf16buf.data();
                    bool drawn = DirectBlitText(ecx_this, a2, a3, a4, wstr, wlen);
                    if (drawn) {
                        g_replacedCount++;
                        return wlen;
                    }
                    // 渲染失败，回退原版
                    LogWrite("[Rollback] Blit FAILED for \"%s\"\n", normText.substr(0, 100).c_str());
                    return g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);
                }
            }
        }
        // rollback 未命中或未加载，输出 miss.log
        LogMiss(normText.c_str(), wide, chLen);
        return g_orig66E7B0(ecx_this, edx_unused, a2, a3, a4);
    }

    const DictEntry& entry = it->second;

    // HIT 日志（去重输出到 hit.log）
    {
        int u8len = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)entry.cnUtf16LE.data(), entry.cnWcharCount, nullptr, 0, nullptr, nullptr);
        char cnUtf8[1024] = {0};
        if (u8len > 0 && u8len < 1024) {
            WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)entry.cnUtf16LE.data(), entry.cnWcharCount, cnUtf8, u8len, nullptr, nullptr);
        }
        LogHit(normText.c_str(), cnUtf8, entry.cnWcharCount);
    }

    const wchar_t* wstr = (const wchar_t*)entry.cnUtf16LE.data();
    int wlen = entry.cnWcharCount;

    bool drawn = DirectBlitText(ecx_this, a2, a3, a4, wstr, wlen);
    if (!drawn) {
        LogWrite("[Blit] FAILED hit=%d text=\"%s\"\n", g_hitCount, normText.substr(0, 100).c_str());
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

    // v14.1: hook sub_5D7720 数消费次数
    status = MH_CreateHook((LPVOID)(uintptr_t)0x005D7720, (LPVOID)&Hooked_5D7720, (LPVOID*)&g_orig5D7720);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_CreateHook(5D7720) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook((LPVOID)(uintptr_t)0x005D7720);
    if (status != MH_OK) {
        LogWrite("[Hook] MH_EnableHook(5D7720) failed: %s\n", MH_StatusToString(status));
        return false;
    }
    LogWrite("[Hook] sub_5D7720 hooked, trampoline=%p\n", g_orig5D7720);
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

        // 路径设置
        char dllPath[MAX_PATH];
        GetModuleFileNameA(hModule, dllPath, MAX_PATH);
        char* p = strrchr(dllPath, '\\');
        char dllDir[MAX_PATH];
        strncpy_s(dllDir, dllPath, (size_t)(p - dllPath + 1));
        dllDir[p - dllPath + 1] = '\0';

        char logPath[MAX_PATH];
        sprintf_s(logPath, "%sMajestyI_TextFix.log", dllDir);
        char dictPath[MAX_PATH];
        sprintf_s(dictPath, "%sdict.json", dllDir);
        char dictTxtPath[MAX_PATH];
        sprintf_s(dictTxtPath, "%sdict.txt", dllDir);
        char rollbackPath[MAX_PATH];
        sprintf_s(rollbackPath, "%srollback.json", dllDir);
        char iniPath[MAX_PATH];
        sprintf_s(iniPath, "%sMajestyI_TextFix.ini", dllDir);

        g_logFile = fopen(logPath, "w");
        char missLogPath[MAX_PATH];
        sprintf_s(missLogPath, "%smiss.log", dllDir);
        char hitLogPath[MAX_PATH];
        sprintf_s(hitLogPath, "%shit.log", dllDir);
        char rollbackLogPath[MAX_PATH];
        sprintf_s(rollbackLogPath, "%srollback.log", dllDir);
        g_missLogFile = fopen(missLogPath, "w");
        g_hitLogFile = fopen(hitLogPath, "w");
        g_rollbackLogFile = fopen(rollbackLogPath, "w");
        char motionLogPath[MAX_PATH];
        sprintf_s(motionLogPath, "%smotion.log", dllDir);
        g_motionFile = fopen(motionLogPath, "w");
        if (g_logFile) {
            fprintf(g_logFile, "[MajestyHD Runtime Localization v16.0 Motion Sampling] DllMain ATTACH\n");
            fprintf(g_logFile, "  Exe path: %s\n", path);
            fprintf(g_logFile, "  Log path: %s\n", logPath);
            fprintf(g_logFile, "  Motion log: %s\n", motionLogPath);
            fprintf(g_logFile, "  Miss log: %s\n", missLogPath);
            fprintf(g_logFile, "  Hit log: %s\n", hitLogPath);
            fprintf(g_logFile, "  Rollback log: %s\n", rollbackLogPath);
            fprintf(g_logFile, "  Dict path: %s (json) / %s (txt fallback)\n", dictPath, dictTxtPath);
            fprintf(g_logFile, "  Rollback path: %s\n", rollbackPath);
            fprintf(g_logFile, "  INI path: %s\n", iniPath);
            fprintf(g_logFile, "  Strategy: hook sub_66E7B0 + CYOffportIMP pixel buffer + INI config\n\n");
            fflush(g_logFile);
        }

        // 加载 INI 配置
        LoadConfig(iniPath);

        // 加载词典: 优先 dict.json, 再加载 dict_0.json ~ dict_9.json 分片
        bool dictLoaded = false;
        if (LoadDictJson(dictPath)) {
            LogWrite("[Dict] Loaded %d entries from JSON: %s\n", g_dictCount, dictPath);
            dictLoaded = true;
        } else {
            LogWrite("[Dict] dict.json not found or invalid, trying txt fallback: %s\n", dictTxtPath);
            if (LoadDictTxt(dictTxtPath)) {
                LogWrite("[Dict] Loaded %d entries from txt: %s\n", g_dictCount, dictTxtPath);
                dictLoaded = true;
            }
        }
        // 加载分片词典 dict_0.json ~ dict_9.json (追加到同一个 g_dict)
        for (int i = 0; i < 10; i++) {
            char shardPath[MAX_PATH];
            sprintf_s(shardPath, "%sdict_%d.json", dllDir, i);
            int before = g_dictCount;
            if (LoadDictJson(shardPath)) {
                LogWrite("[Dict] Loaded %d entries from shard: %s (total: %d)\n",
                    g_dictCount - before, shardPath, g_dictCount);
                dictLoaded = true;
            } else {
                break; // 文件不存在，停止扫描
            }
        }
        if (!dictLoaded) {
            LogWrite("[ERROR] Failed to load any dict\n");
        }

        // 加载 rollback 词汇表
        if (LoadRollbackJson(rollbackPath)) {
            LogWrite("[Rollback] Loaded %d entries from %s\n", g_rollbackCount, rollbackPath);
            // 按 key 长度降序排列，长短语优先匹配
            std::sort(g_rollback.begin(), g_rollback.end(), RollbackSortByLengthDesc);
            LogWrite("[Rollback] Sorted by key length (descending) for longest-match-first\n");
        } else {
            LogWrite("[Rollback] No rollback.json found or empty: %s\n", rollbackPath);
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
            fprintf(g_logFile, "\n[DllMain] DETACH (v16.0)\n");
            fprintf(g_logFile, "  Calls=%d Replaced=%d Hits=%d (unique=%d) Rollback=%d (unique=%d) Misses=%d (unique=%d)\n",
                g_callCount, g_replacedCount, g_hitCount, (int)g_hitSeen.size(),
                g_rollbackHitCount, (int)g_rollbackSeen.size(),
                g_missCount, (int)g_missSeen.size());
            fprintf(g_logFile, "  Blits=%d BlitFails=%d FullScreenDirty=%d\n",
                g_blitCount, g_blitFailCount, g_dirtyRectCount);
            // v14.1 总账
            fprintf(g_logFile, "  Compose(sub_5D7720) total=%d devMatch=%d devDiffer=%d\n",
                g_composeCallsTotal, g_composeDevMatch, g_composeDevDiffer);
            fprintf(g_logFile, "  SubmitDelta: ok=%d zero=%d weird=%d  DirtyMgr: nonNull=%d null=%d\n",
                g_dirtyDeltaOK, g_dirtyDeltaZero, g_dirtyDeltaWeird,
                g_dirtyMgrNonNull, g_dirtyMgrNull);
            // v15.0 总账
            fprintf(g_logFile, "  BG-LRU(v15): hits=%d new=%d clean=%d skip=%d full=%d oor=%d\n",
                g_v15Hits, g_v15New, g_v15Clean, g_v15Skip, g_v15Full, g_v15OOR);
            // v16.0 总账
            fprintf(g_logFile, "  Motion: lines=%d\n", g_motionLines);
            fclose(g_logFile);
            g_logFile = nullptr;
        }
        if (g_motionFile) { fclose(g_motionFile); g_motionFile = nullptr; }
        if (g_missLogFile) { fclose(g_missLogFile); g_missLogFile = nullptr; }
        if (g_hitLogFile) { fclose(g_hitLogFile); g_hitLogFile = nullptr; }
        if (g_rollbackLogFile) { fclose(g_rollbackLogFile); g_rollbackLogFile = nullptr; }
        if (g_cjkFont) { DeleteObject(g_cjkFont); g_cjkFont = nullptr; }
        if (g_cfg.fontFile[0]) {
            std::string dllDir = GetDllDir();
            std::string fontPath = dllDir + g_cfg.fontFile;
            RemoveFontResourceExA(fontPath.c_str(), FR_PRIVATE, nullptr);
        }
    }
    return TRUE;
}
